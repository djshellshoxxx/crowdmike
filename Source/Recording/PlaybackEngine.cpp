#include "PlaybackEngine.h"
#include "AudioFileLoader.h"
#include "StreamSource.h"
#include <cmath>

namespace crowdmike::rec {

namespace {
constexpr int kVoicesPerSlot = 3;
constexpr double kBedLookaheadSec = 1.0;    // streamed bed: next region is requested this long before its crossfade
float dbToGain(float db) { return db <= -100.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }

struct Rng {   // xorshift32, deterministic across platforms
    std::uint32_t s = 1;
    void seed(unsigned v) { s = v != 0 ? (std::uint32_t) v : 0x9E3779B9u; }
    double next01() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) s / 4294967296.0;
    }
};
} // namespace

struct PlaybackEngine::ClipData : juce::ReferenceCountedObject {
    juce::AudioBuffer<float> audio;   // resident clips: the whole clip at the engine rate
    juce::AudioBuffer<float> head;    // streamed clips: first headFrames frames of the playback sequence (stereo)
    bool streamed = false;
    juce::int64 frames = 0;           // whole clip, engine-rate frames
    int channels = 1;
    juce::uint64 sourceId = 0;        // StreamPool registry id (streamed)
    juce::int64 headS = 0, headLen = 0;   // head key: trimmed region [headS, headS + headLen)
    bool headRev = false;
    int headFrames = 0;
    double sourceRate = 0.0;
    int sourceChannels = 0;
    double durationSec = 0.0;
    std::vector<float> wmin, wmax;
    juce::String name;
    std::atomic<juce::int64>* residentCtr = nullptr;
    std::atomic<juce::int64>* headCtr = nullptr;
    juce::int64 rBytes = 0, hBytes = 0;

    std::size_t bytes() const {
        return (size_t) audio.getNumChannels() * (size_t) audio.getNumSamples() * sizeof(float)
               + (size_t) head.getNumChannels() * (size_t) head.getNumSamples() * sizeof(float);
    }
    void account(std::atomic<juce::int64>& resident, std::atomic<juce::int64>& headC) {
        residentCtr = &resident;
        headCtr = &headC;
        rBytes = (juce::int64) audio.getNumChannels() * audio.getNumSamples() * (juce::int64) sizeof(float);
        hBytes = (juce::int64) head.getNumChannels() * head.getNumSamples() * (juce::int64) sizeof(float);
        resident += rBytes;
        headC += hBytes;
    }
    ~ClipData() override {
        if (residentCtr != nullptr) *residentCtr -= rBytes;
        if (headCtr != nullptr) *headCtr -= hBytes;
    }
    JUCE_LEAK_DETECTOR(ClipData)
};

struct PlaybackEngine::Voice {
    ClipData* clip = nullptr;
    bool active = false, stopping = false, firstPass = true;
    std::int64_t pos = 0, played = 0;
    std::uint64_t seq = 0;            // streamed: index in the playback sequence
    float env = 1.0f, envStep = 0.0f, gain = 1.0f;
    int route = 0, delay = 0;
    // streamed
    int ring = -1;
    bool waiting = false;             // no pre-buffered head: wait for the ring to fill before sounding
    int waited = 0;
    std::int64_t sS = 0, sLen = 0;    // region snapshot taken when the ring was requested
    int nextRoute = -1;               // route change of a streamed voice: duck, switch, un-duck
    float duck = 1.0f;
    // tempo-synced loop
    bool hasBeat = false;
    double startBeat = 0.0;
    std::int64_t lastK = 0;
    int rampIn = 0;
};

struct PlaybackEngine::AudioSlot {
    std::atomic<ClipData*> pending { nullptr };
    ClipData* current = nullptr;
    ClipData* previous = nullptr;   // fading out after a swap; retired once unused
    Params params;
    Voice voices[kVoicesPerSlot];
    bool pendActive = false;        // tempo-synced trigger waiting for its grid line
    double pendBeat = 0.0;
    int pendOffset = 0;
    std::atomic<bool> syncPending { false };
    std::atomic<bool> playing { false };
    std::atomic<float> pos01 { 0.0f };
};

struct PlaybackEngine::ControlSlot {
    juce::File file;
    juce::String error;
    bool loading = false;
    juce::ReferenceCountedObjectPtr<ClipData> loaded;
    ClipSettings settings;
    juce::uint64 generation = 0;
};

struct PlaybackEngine::Bed {
    struct Grain { std::int64_t start = 0, len = 0, pos = 0, xf = 1; int ring = -1; };
    BedSettings s;
    ClipData* clip = nullptr;
    bool running = false, xfading = false, nextPicked = false;
    Grain cur, next;
    std::int64_t xfPos = 0;
    float env = 0.0f;
    Rng rng;
    double levelDb = 0.0, levelTarget = 0.0;
    std::int64_t levelCountdown = 0;
    float lp[2] {};
};

class PlaybackEngine::LoaderThread : public juce::Thread {
public:
    explicit LoaderThread(PlaybackEngine& o) : juce::Thread("CrowdMike clip loader"), owner(o) {}
    void run() override {
        while (! threadShouldExit()) {
            Job job;
            bool have = false;
            {
                std::lock_guard<std::mutex> l(owner.controlLock);
                if (! owner.jobs.empty()) { job = owner.jobs.front(); owner.jobs.pop_front(); have = true; }
            }
            if (have) {
                owner.runJob(job);
                owner.jobsPending.fetch_sub(1);
            } else {
                wait(20);
            }
            owner.releaseRetired();
        }
    }
private:
    PlaybackEngine& owner;
};

//==================================================================================================================
PlaybackEngine::PlaybackEngine()
    : audioSlots(std::make_unique<std::array<AudioSlot, kMaxClips>>()),
      bed(std::make_unique<Bed>()),
      controlSlots(std::make_unique<std::array<ControlSlot, kMaxClips>>()) {}

PlaybackEngine::~PlaybackEngine() { releaseResources(); }

PlaybackEngine::Params PlaybackEngine::toParams(const ClipSettings& s) {
    Params p;
    p.gainDb = juce::jlimit(-100.0f, 24.0f, std::isfinite(s.gainDb) ? s.gainDb : 0.0f);
    p.fadeInMs = juce::jlimit(0.0f, 600000.0f, std::isfinite(s.fadeInMs) ? s.fadeInMs : 0.0f);
    p.fadeOutMs = juce::jlimit(0.0f, 600000.0f, std::isfinite(s.fadeOutMs) ? s.fadeOutMs : 0.0f);
    p.startFrac = juce::jlimit(0.0, 1.0, std::isfinite(s.startFrac) ? s.startFrac : 0.0);
    p.endFrac = juce::jlimit(p.startFrac, 1.0, std::isfinite(s.endFrac) ? s.endFrac : 1.0);
    p.mode = s.mode == ClipSettings::Loop ? 1 : 0;
    p.reverse = s.reverse;
    p.route = juce::jlimit(0, kNumDestinations - 1, s.route);
    p.tempoSync = s.tempoSync;
    p.syncQuantise = juce::jlimit(0, kNumQuantise - 1, s.syncQuantise);
    p.syncLoopBeats = juce::jlimit(0.0, 256.0, std::isfinite(s.syncLoopBeats) ? s.syncLoopBeats : 0.0);
    return p;
}

namespace {
// Trimmed region [s, e) of a clip of `len` frames for the given fractions (shared by the audio and loader threads).
inline void regionOf(double startFrac, double endFrac, std::int64_t len, std::int64_t& s, std::int64_t& e) {
    s = juce::jlimit<std::int64_t>(0, len, std::llround(startFrac * (double) len));
    e = juce::jlimit<std::int64_t>(0, len, std::llround(endFrac * (double) len));
}
} // namespace

void PlaybackEngine::prepare(double sr, int maxBlock, bool startStreamThread) {
    const bool rateChanged = std::abs(sr - sampleRate) > 1.0e-6;
    maxBlockSize = std::max(1, maxBlock);
    pool.stop();
    if (rateChanged) clearAudioState();
    sampleRate = sr;
    rampFrames = std::max(1, (int) std::lround(kClickRampMs * sr / 1000.0));
    pool.prepare(sr, maxBlockSize);
    for (auto& slot : *audioSlots) {
        for (auto& v : slot.voices) v = Voice {};
        slot.pendActive = false;
        slot.syncPending = false;
    }
    bed->running = false;
    bed->nextPicked = false;
    bed->cur.ring = bed->next.ring = -1;
    bed->env = 0.0f;
    muted = false;
    clockValid = wasPlaying = beatKnown = false;
    tempoSet = false;
    cancelLoads = false;
    {
        std::lock_guard<std::mutex> l(controlLock);
        if (rateChanged)
            for (int i = 0; i < kMaxClips; ++i) {
                auto& cs = (*controlSlots)[(size_t) i];
                if (cs.loaded != nullptr && cs.loaded->streamed) pool.unregisterSource(cs.loaded->sourceId);
                if (cs.file == juce::File()) continue;
                ++cs.generation;
                cs.loaded = nullptr;
                cs.loading = true;
                jobs.push_back({ i, cs.file, cs.generation, false });
                jobsPending.fetch_add(1);
            }
    }
    pool.start(startStreamThread);
    if (loader == nullptr) {
        loader = std::make_unique<LoaderThread>(*this);
        loader->startThread();
    }
    loader->notify();
}

void PlaybackEngine::releaseResources() {
    cancelLoads = true;
    if (loader != nullptr) {
        loader->signalThreadShouldExit();
        loader->notify();
        loader->stopThread(10000);
        loader.reset();
    }
    pool.stop();
    {
        std::lock_guard<std::mutex> l(controlLock);
        jobsPending.fetch_sub((int) jobs.size());
        jobs.clear();
        for (auto& cs : *controlSlots) {
            cs.loading = false;
            if (cs.loaded != nullptr && cs.loaded->streamed) pool.unregisterSource(cs.loaded->sourceId);
        }
    }
    clearAudioState();
    releaseRetired();
    sampleRate = 0.0;   // force a reload at the next prepare()
}

void PlaybackEngine::clearAudioState() {
    Command c;
    while (commands.pop(c)) {}
    releaseRetired();
    for (auto& slot : *audioSlots) {
        for (auto& v : slot.voices) v = Voice {};
        if (slot.current != nullptr) slot.current->decReferenceCount();
        if (slot.previous != nullptr) slot.previous->decReferenceCount();
        if (auto* p = slot.pending.exchange(nullptr)) p->decReferenceCount();
        slot.current = slot.previous = nullptr;
        slot.playing = false;
        slot.pendActive = false;
        slot.syncPending = false;
        slot.pos01 = 0.0f;
    }
    bed->clip = nullptr;
    bed->running = false;
    bed->nextPicked = false;
    bed->cur.ring = bed->next.ring = -1;
    bed->env = 0.0f;
}

void PlaybackEngine::releaseRetired() {
    retire.drain([](ClipData* c) { c->decReferenceCount(); });
}

void PlaybackEngine::setResidentLimitSeconds(double seconds) {
    residentLimitSeconds.store(std::isfinite(seconds) ? std::max(0.0, seconds) : kDefaultResidentSeconds);
}

PlaybackEngine::MemoryStats PlaybackEngine::getMemoryStats() const {
    MemoryStats m;
    m.residentBytes = clipBytesLive.load();
    m.headBytes = headBytesLive.load();
    m.ringBytes = pool.getStats().ringBytes;
    m.totalBytes = m.residentBytes + m.headBytes + m.ringBytes;
    std::lock_guard<std::mutex> l(controlLock);
    for (const auto& cs : *controlSlots)
        if (cs.loaded != nullptr) (cs.loaded->streamed ? m.streamedClips : m.residentClips)++;
    return m;
}

bool PlaybackEngine::loadClip(int slot, const juce::File& file) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return false;
    std::lock_guard<std::mutex> l(controlLock);
    auto& cs = (*controlSlots)[(size_t) slot];
    cs.file = file;
    cs.error = {};
    cs.loading = true;
    ++cs.generation;
    if (sampleRate > 0.0 && loader != nullptr) {
        jobs.push_back({ slot, file, cs.generation, false });
        jobsPending.fetch_add(1);
        loader->notify();
    }
    return true;
}

void PlaybackEngine::unloadClip(int slot) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return;
    {
        std::lock_guard<std::mutex> l(controlLock);
        auto& cs = (*controlSlots)[(size_t) slot];
        ++cs.generation;
        if (cs.loaded != nullptr && cs.loaded->streamed) pool.unregisterSource(cs.loaded->sourceId);
        cs.file = juce::File();
        cs.loaded = nullptr;
        cs.loading = false;
        cs.error = {};
    }
    if (auto* p = (*audioSlots)[(size_t) slot].pending.exchange(nullptr)) p->decReferenceCount();
    Command c;
    c.type = Command::Unload;
    c.slot = slot;
    commands.push(c);
}

//==== loader thread ===============================================================================================
void PlaybackEngine::runJob(const Job& job) {
    if (job.rehead) runRehead(job);
    else runLoad(job);
}

namespace {
// Min/max of channels [a, b) of a resident buffer.
void residentWaveform(const juce::AudioBuffer<float>& audio, std::vector<float>& wmin, std::vector<float>& wmax, int kPoints) {
    const int len = audio.getNumSamples();
    const int pts = std::max(1, std::min(kPoints, len));
    wmin.assign((size_t) pts, 0.0f);
    wmax.assign((size_t) pts, 0.0f);
    for (int p = 0; p < pts; ++p) {
        const int a = (int) ((juce::int64) p * len / pts), b = std::max(a + 1, (int) ((juce::int64) (p + 1) * len / pts));
        float mn = 0.0f, mx = 0.0f;
        for (int c = 0; c < audio.getNumChannels(); ++c) {
            const auto range = juce::FloatVectorOperations::findMinAndMax(audio.getReadPointer(c, a), b - a);
            mn = c == 0 ? range.getStart() : std::min(mn, range.getStart());
            mx = c == 0 ? range.getEnd() : std::max(mx, range.getEnd());
        }
        wmin[(size_t) p] = mn;
        wmax[(size_t) p] = mx;
    }
}
} // namespace

void PlaybackEngine::runLoad(const Job& job) {
    const int slot = job.slot;
    const auto& file = job.file;
    juce::int64 residentUsed = 0;
    double sr = 0.0;
    ClipSettings st;
    {
        std::lock_guard<std::mutex> l(controlLock);
        if ((*controlSlots)[(size_t) slot].generation != job.generation) return;
        for (int i = 0; i < kMaxClips; ++i)
            if (i != slot && (*controlSlots)[(size_t) i].loaded != nullptr && ! (*controlSlots)[(size_t) i].loaded->streamed)
                residentUsed += (juce::int64) (*controlSlots)[(size_t) i].loaded->bytes();
        sr = sampleRate;
        st = (*controlSlots)[(size_t) slot].settings;
    }
    juce::String err;
    juce::ReferenceCountedObjectPtr<ClipData> clip;
    StreamSource src;
    if (src.open(file, sr, err)) {
        const auto& inf = src.info();
        const double srcSeconds = (double) inf.sourceFrames / inf.sourceRate;
        const double seconds = (double) inf.frames / sr;
        const juce::int64 residentBytes = inf.frames * inf.channels * (juce::int64) sizeof(float);
        if (srcSeconds > kMaxClipSeconds) {
            err = "File too long (" + juce::String(srcSeconds / 3600.0, 1) + " h, limit " + juce::String(kMaxClipSeconds / 3600.0, 0)
                  + " h): " + file.getFileName();
        } else if (seconds <= residentLimitSeconds.load() && residentUsed + residentBytes <= kMaxResidentBytes) {
            AudioFileLoader::Result r;
            if (AudioFileLoader::load(file, sr, 2, 0.0, 0, r, err, &cancelLoads)) {
                clip = new ClipData();
                clip->audio = std::move(r.audio);
                clip->frames = clip->audio.getNumSamples();
                clip->channels = clip->audio.getNumChannels();
                clip->sourceRate = r.sourceSampleRate;
                clip->sourceChannels = r.sourceChannels;
                clip->durationSec = (double) clip->audio.getNumSamples() / sr;
                clip->name = file.getFileNameWithoutExtension();
                residentWaveform(clip->audio, clip->wmin, clip->wmax, kWaveformPoints);
            }
        } else {
            // ---- streamed clip: nothing but a waveform sketch and the pre-buffered head is kept in memory
            clip = new ClipData();
            clip->streamed = true;
            clip->frames = inf.frames;
            clip->channels = inf.channels;
            clip->sourceRate = inf.sourceRate;
            clip->sourceChannels = inf.sourceChannels;
            clip->durationSec = seconds;
            clip->name = file.getFileNameWithoutExtension();
            const int pts = (int) std::max<juce::int64>(1, std::min<juce::int64>(kWaveformPoints, inf.frames));
            clip->wmin.assign((size_t) pts, 0.0f);
            clip->wmax.assign((size_t) pts, 0.0f);
            std::vector<float> bl(512), br(512);
            for (int p = 0; p < pts && err.isEmpty(); ++p) {
                if (cancelLoads.load()) { err = "Cancelled"; break; }
                const juce::int64 a = (juce::int64) p * inf.frames / pts, b = std::max(a + 1, (juce::int64) (p + 1) * inf.frames / pts);
                const int probe = (int) std::min<juce::int64>(512, b - a);
                float mn = 0.0f, mx = 0.0f;
                bool first = true;
                for (int k = 0; k < 4; ++k) {   // four spread probes per point: an approximate envelope of a huge file
                    const juce::int64 at = a + ((b - a - probe) * k) / 3;
                    src.read(at, probe, bl.data(), br.data());
                    for (int c = 0; c < clip->channels; ++c) {
                        const auto r = juce::FloatVectorOperations::findMinAndMax(c == 0 ? bl.data() : br.data(), probe);
                        mn = first ? r.getStart() : std::min(mn, r.getStart());
                        mx = first ? r.getEnd() : std::max(mx, r.getEnd());
                        first = false;
                    }
                }
                clip->wmin[(size_t) p] = mn;
                clip->wmax[(size_t) p] = mx;
            }
            if (err.isEmpty()) {
                const auto P = toParams(st);
                std::int64_t s = 0, e = 0;
                regionOf(P.startFrac, P.endFrac, clip->frames, s, e);
                clip->headS = s;
                clip->headLen = e - s;
                clip->headRev = P.reverse;
                const int h = (int) std::min<std::int64_t>(e - s, std::llround(kHeadSeconds * sr));
                clip->head.setSize(2, std::max(0, h));
                clip->headFrames = std::max(0, h);
                if (h > 0) src.readSequence(s, e - s, P.reverse, false, 0, h, clip->head.getWritePointer(0), clip->head.getWritePointer(1));
                clip->sourceId = pool.registerSource(file);
            } else {
                clip = nullptr;
            }
        }
    }
    if (clip != nullptr) clip->account(clipBytesLive, headBytesLive);
    std::lock_guard<std::mutex> l(controlLock);
    auto& cs = (*controlSlots)[(size_t) slot];
    if (cs.generation != job.generation) {   // superseded (clip released here, on the loader thread)
        if (clip != nullptr && clip->streamed) pool.unregisterSource(clip->sourceId);
        return;
    }
    cs.loading = false;
    if (clip != nullptr) {
        if (cs.loaded != nullptr && cs.loaded->streamed) pool.unregisterSource(cs.loaded->sourceId);
        cs.loaded = clip;
        cs.error = {};
        publish(slot, clip.get());
    } else {
        cs.error = err.isNotEmpty() ? err : juce::String("Load failed");
    }
}

void PlaybackEngine::runRehead(const Job& job) {
    // A trim / reverse change of a streamed clip: rebuild only the pre-buffered head (same registered source).
    juce::ReferenceCountedObjectPtr<ClipData> old;
    ClipSettings st;
    double sr = 0.0;
    {
        std::lock_guard<std::mutex> l(controlLock);
        auto& cs = (*controlSlots)[(size_t) job.slot];
        if (cs.generation != job.generation || cs.loaded == nullptr || ! cs.loaded->streamed) return;
        old = cs.loaded;
        st = cs.settings;
        sr = sampleRate;
    }
    const auto P = toParams(st);
    std::int64_t s = 0, e = 0;
    regionOf(P.startFrac, P.endFrac, old->frames, s, e);
    if (old->headS == s && old->headLen == e - s && old->headRev == P.reverse) return;   // already current
    juce::ReferenceCountedObjectPtr<ClipData> clip(new ClipData());
    clip->streamed = true;
    clip->frames = old->frames;
    clip->channels = old->channels;
    clip->sourceId = old->sourceId;
    clip->sourceRate = old->sourceRate;
    clip->sourceChannels = old->sourceChannels;
    clip->durationSec = old->durationSec;
    clip->wmin = old->wmin;
    clip->wmax = old->wmax;
    clip->name = old->name;
    clip->headS = s;
    clip->headLen = e - s;
    clip->headRev = P.reverse;
    const int h = (int) std::max<std::int64_t>(0, std::min<std::int64_t>(e - s, std::llround(kHeadSeconds * sr)));
    clip->head.setSize(2, h);
    clip->headFrames = h;
    if (h > 0) {
        StreamSource src;
        juce::String err;
        if (! src.open(job.file, sr, err) || src.info().frames != clip->frames) return;
        src.readSequence(s, e - s, P.reverse, false, 0, h, clip->head.getWritePointer(0), clip->head.getWritePointer(1));
    }
    clip->account(clipBytesLive, headBytesLive);
    std::lock_guard<std::mutex> l(controlLock);
    auto& cs = (*controlSlots)[(size_t) job.slot];
    if (cs.generation != job.generation || cs.loaded != old) return;
    cs.loaded = clip;
    publish(job.slot, clip.get());
}

void PlaybackEngine::publish(int slot, ClipData* c) {
    c->incReferenceCount();
    if (auto* old = (*audioSlots)[(size_t) slot].pending.exchange(c, std::memory_order_acq_rel)) old->decReferenceCount();
}

//==== control =====================================================================================================
bool PlaybackEngine::trigger(int slot, int offset) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return false;
    Command c;
    c.type = Command::Trigger;
    c.slot = slot;
    c.offset = std::max(0, offset);
    return commands.push(c);
}

bool PlaybackEngine::triggerNow(int slot, int offset) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return false;
    Command c;
    c.type = Command::Trigger;
    c.slot = slot;
    c.offset = std::max(0, offset);
    c.immediate = true;
    return commands.push(c);
}

bool PlaybackEngine::stop(int slot) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return false;
    Command c;
    c.type = Command::Stop;
    c.slot = slot;
    return commands.push(c);
}

bool PlaybackEngine::stopAll() {
    Command c;
    c.type = Command::StopAll;
    return commands.push(c);
}

bool PlaybackEngine::setSettings(int slot, const ClipSettings& s) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return false;
    {
        std::lock_guard<std::mutex> l(controlLock);
        auto& cs = (*controlSlots)[(size_t) slot];
        cs.settings = s;
        if (cs.loaded != nullptr && cs.loaded->streamed && sampleRate > 0.0 && loader != nullptr) {
            const auto P = toParams(s);
            std::int64_t a = 0, b = 0;
            regionOf(P.startFrac, P.endFrac, cs.loaded->frames, a, b);
            if (cs.loaded->headS != a || cs.loaded->headLen != b - a || cs.loaded->headRev != P.reverse) {
                // newest request wins: drop queued head rebuilds of this slot
                for (auto it = jobs.begin(); it != jobs.end();) {
                    if (it->rehead && it->slot == slot) { it = jobs.erase(it); jobsPending.fetch_sub(1); }
                    else ++it;
                }
                jobs.push_back({ slot, cs.file, cs.generation, true });
                jobsPending.fetch_add(1);
                loader->notify();
            }
        }
    }
    Command c;
    c.type = Command::SetParams;
    c.slot = slot;
    c.params = toParams(s);
    return commands.push(c);
}

PlaybackEngine::ClipSettings PlaybackEngine::getSettings(int slot) const {
    if ((unsigned) slot >= (unsigned) kMaxClips) return {};
    std::lock_guard<std::mutex> l(controlLock);
    return (*controlSlots)[(size_t) slot].settings;
}

bool PlaybackEngine::setBed(const BedSettings& b) {
    BedSettings s = b;
    s.slot = juce::jlimit(0, kMaxClips - 1, s.slot);
    s.minLoopSec = juce::jlimit(0.05, 3600.0, std::isfinite(s.minLoopSec) ? s.minLoopSec : 20.0);
    s.maxLoopSec = juce::jlimit(s.minLoopSec, 3600.0, std::isfinite(s.maxLoopSec) ? s.maxLoopSec : s.minLoopSec);
    s.crossfadeSec = juce::jlimit(0.001, 60.0, std::isfinite(s.crossfadeSec) ? s.crossfadeSec : 4.0);
    s.levelVariationDb = juce::jlimit(0.0f, 12.0f, std::isfinite(s.levelVariationDb) ? s.levelVariationDb : 0.0f);
    s.lowpassHz = std::isfinite(s.lowpassHz) ? std::max(0.0f, s.lowpassHz) : 0.0f;
    s.gainDb = juce::jlimit(-100.0f, 12.0f, std::isfinite(s.gainDb) ? s.gainDb : -12.0f);
    s.route = juce::jlimit(0, kNumDestinations - 1, s.route);
    {
        std::lock_guard<std::mutex> l(controlLock);
        bedSettings = s;
    }
    Command c;
    c.type = Command::SetBed;
    c.bed = s;
    return commands.push(c);
}

PlaybackEngine::BedSettings PlaybackEngine::getBed() const {
    std::lock_guard<std::mutex> l(controlLock);
    return bedSettings;
}

PlaybackEngine::ClipInfo PlaybackEngine::getClipInfo(int slot) const {
    ClipInfo i;
    if ((unsigned) slot >= (unsigned) kMaxClips) return i;
    std::lock_guard<std::mutex> l(controlLock);
    const auto& cs = (*controlSlots)[(size_t) slot];
    i.loaded = cs.loaded != nullptr;
    i.loading = cs.loading;
    i.error = cs.error;
    if (cs.loaded != nullptr) {
        i.durationSeconds = cs.loaded->durationSec;
        i.numChannels = cs.loaded->streamed ? cs.loaded->channels : cs.loaded->audio.getNumChannels();
        i.sourceSampleRate = cs.loaded->sourceRate;
        i.streaming = cs.loaded->streamed;
        i.headFrames = cs.loaded->headFrames;
    }
    i.name = cs.settings.name.isNotEmpty() ? cs.settings.name
                                            : (cs.loaded != nullptr ? cs.loaded->name : cs.file.getFileNameWithoutExtension());
    const auto& as = (*audioSlots)[(size_t) slot];
    i.playing = as.playing.load(std::memory_order_relaxed);
    i.syncPending = as.syncPending.load(std::memory_order_relaxed);
    i.playPosition01 = as.pos01.load(std::memory_order_relaxed);
    return i;
}

bool PlaybackEngine::getWaveform(int slot, float* minOut, float* maxOut, int points) const {
    if ((unsigned) slot >= (unsigned) kMaxClips || points <= 0 || minOut == nullptr || maxOut == nullptr) return false;
    std::lock_guard<std::mutex> l(controlLock);
    const auto& clip = (*controlSlots)[(size_t) slot].loaded;
    if (clip == nullptr || clip->wmin.empty()) return false;
    const int K = (int) clip->wmin.size();
    for (int p = 0; p < points; ++p) {
        const int a = (int) ((juce::int64) p * K / points);
        const int b = std::max(a + 1, (int) ((juce::int64) (p + 1) * K / points));
        float mn = clip->wmin[(size_t) a], mx = clip->wmax[(size_t) a];
        for (int k = a + 1; k < b && k < K; ++k) { mn = std::min(mn, clip->wmin[(size_t) k]); mx = std::max(mx, clip->wmax[(size_t) k]); }
        minOut[p] = mn;
        maxOut[p] = mx;
    }
    return true;
}

bool PlaybackEngine::waitForLoads(int timeoutMs) const {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (jobsPending.load() > 0) {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep(2);
    }
    return true;
}

//==== audio thread ================================================================================================
void PlaybackEngine::endVoice(Voice& v) noexcept {
    if (v.ring >= 0) pool.release(v.ring);
    v.ring = -1;
    v.active = false;
}

// Claims a read-ahead ring for a streamed voice starting its playback sequence at 0. The pre-buffered head is used
// when its key matches the current trim/reverse; otherwise the voice waits (silently) until the ring has data.
bool PlaybackEngine::attachRing(Voice& v, const ClipData& c, const Params& P) noexcept {
    std::int64_t s = 0, e = 0;
    regionOf(P.startFrac, P.endFrac, c.frames, s, e);
    const std::int64_t len = e - s;
    if (len < 1) return false;
    const bool headOk = c.headS == s && c.headLen == len && c.headRev == P.reverse;
    const int headFrames = headOk ? c.headFrames : 0;
    v.sS = s;
    v.sLen = len;
    v.seq = 0;
    v.waiting = ! headOk || headFrames == 0;
    v.waited = 0;
    const bool syncLoop = P.mode == 1 && P.tempoSync && P.syncLoopBeats > 0.0 && beatKnown;
    v.ring = pool.acquire(c.sourceId, s, len, P.reverse, P.mode == 1 && ! syncLoop, (juce::uint64) headFrames);
    if (v.ring < 0 && v.waiting) return false;
    if (v.ring < 0) v.waiting = false;
    return true;
}

void PlaybackEngine::startVoice(AudioSlot& slot, int offset, std::int64_t pos, std::int64_t played, bool firstPass,
                                bool fadeFromZero, int route, int n, double startBeat, bool hasBeat) noexcept {
    Voice* v = nullptr;
    for (auto& x : slot.voices)
        if (! x.active) { v = &x; break; }
    if (v == nullptr)   // steal the quietest fading voice
        for (auto& x : slot.voices)
            if (x.stopping && (v == nullptr || x.env < v->env)) v = &x;
    if (v == nullptr) v = &slot.voices[0];
    endVoice(*v);
    *v = Voice {};
    v->clip = slot.current;
    v->active = true;
    v->pos = pos;
    v->played = played;
    v->firstPass = firstPass;
    v->env = fadeFromZero ? 0.0f : 1.0f;
    v->envStep = fadeFromZero ? 1.0f / (float) rampFrames : 0.0f;
    v->gain = dbToGain(slot.params.gainDb);
    v->route = route;
    v->delay = juce::jlimit(0, std::max(0, n - 1), offset);
    v->hasBeat = hasBeat;
    v->startBeat = startBeat;
    if (slot.current->streamed && ! attachRing(*v, *slot.current, slot.params)) v->active = false;
}

void PlaybackEngine::handleCommand(const Command& c, int n) noexcept {
    auto stopVoice = [](Voice& v, float frames) {
        if (! v.active || v.stopping) return;
        v.stopping = true;
        if (v.env <= 0.0f) { v.active = false; return; }
        v.envStep = -v.env / std::max(1.0f, frames);
    };
    auto stopFrames = [this](const Params& p) {
        return std::max((float) rampFrames, p.fadeOutMs * (float) sampleRate / 1000.0f);
    };
    auto cancelPending = [](AudioSlot& s) { s.pendActive = false; s.syncPending.store(false, std::memory_order_relaxed); };
    switch (c.type) {
        case Command::Trigger: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            if (muted || slot.current == nullptr) return;
            if (! c.immediate && slot.params.tempoSync && beatKnown) {
                // quantise: wait for the next grid line at/after the requested start point (latest request wins)
                const double g = quantiseBeats(slot.params.syncQuantise);
                const double b = beat0 + (double) c.offset * bps;
                slot.pendBeat = std::ceil(b / g - 1.0e-9) * g;
                slot.pendOffset = c.offset;
                slot.pendActive = true;
                slot.syncPending.store(true, std::memory_order_relaxed);
                return;
            }
            cancelPending(slot);
            for (auto& v : slot.voices) stopVoice(v, (float) rampFrames);
            const bool fadeFromZero = slot.params.fadeInMs * sampleRate / 1000.0 < rampFrames;
            startVoice(slot, c.offset, 0, 0, true, fadeFromZero, slot.params.route, n, beat0 + (double) c.offset * bps, beatKnown);
            break;
        }
        case Command::Stop: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            cancelPending(slot);
            for (auto& v : slot.voices) stopVoice(v, stopFrames(slot.params));
            break;
        }
        case Command::StopAll:
            for (auto& slot : *audioSlots) {
                cancelPending(slot);
                for (auto& v : slot.voices) stopVoice(v, stopFrames(slot.params));
            }
            break;
        case Command::SetParams: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            slot.params = c.params;
            for (auto& v : slot.voices) {
                if (! v.active || v.stopping || v.clip != slot.current) continue;
                if (v.clip->streamed) {   // no restart (a restart needs a ring at an arbitrary position): duck to the new route
                    if (v.nextRoute >= 0 ? v.nextRoute != c.params.route : v.route != c.params.route) v.nextRoute = c.params.route;
                    continue;
                }
                if (v.route == c.params.route) continue;
                const Voice old = v;   // crossfade to the new route at the same position
                stopVoice(v, (float) rampFrames);
                startVoice(slot, 0, old.pos, old.played, old.firstPass, true, c.params.route, n, old.startBeat, old.hasBeat);
                break;
            }
            break;
        }
        case Command::SetBed: {
            auto& b = *bed;
            const auto old = b.s;
            b.s = c.bed;
            if ((! old.enabled && c.bed.enabled && b.env <= 0.0f) || old.slot != c.bed.slot) b.running = false;
            else if (old.seed != c.bed.seed) b.rng.seed(c.bed.seed);
            break;
        }
        case Command::Unload: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            cancelPending(slot);
            if (slot.previous != nullptr) {   // still fading from an earlier swap: cut it now
                for (auto& v : slot.voices)
                    if (v.clip == slot.previous) endVoice(v);
                if (bed->clip == slot.previous) { bed->clip = nullptr; bed->running = false; bed->env = 0.0f; }
                if (retire.push(slot.previous)) slot.previous = nullptr;
                else break;   // retire queue full: try again on the next unload (never free here)
            }
            for (auto& v : slot.voices) stopVoice(v, (float) rampFrames);
            slot.previous = slot.current;
            slot.current = nullptr;
            break;
        }
    }
}

void PlaybackEngine::updateBeatClock(int n) noexcept {
    beatKnown = false;
    if (! tempoSet || ! tempoIn.hasTempo || ! (tempoIn.bpm > 1.0 && tempoIn.bpm < 1000.0) || ! std::isfinite(tempoIn.ppqPosition)) {
        clockValid = false;
        wasPlaying = false;
        return;
    }
    bps = tempoIn.bpm / (60.0 * sampleRate);
    if (tempoIn.playing) beat0 = tempoIn.ppqPosition;
    else if (clockValid && ! wasPlaying) beat0 = freeBeat;   // transport stopped: the grid keeps running
    else beat0 = tempoIn.ppqPosition;
    wasPlaying = tempoIn.playing;
    clockValid = true;
    beatKnown = true;
    freeBeat = beat0 + (double) n * bps;
}

void PlaybackEngine::fireDueTriggers(int n) noexcept {
    for (auto& slot : *audioSlots) {
        if (! slot.pendActive) continue;
        if (slot.current == nullptr || muted) { slot.pendActive = false; slot.syncPending.store(false, std::memory_order_relaxed); continue; }
        int off = slot.pendOffset;
        double startBeat = beat0;
        if (beatKnown) {
            const double g = quantiseBeats(slot.params.syncQuantise);
            if (slot.pendBeat - beat0 > g + 1.0e-6)   // transport relocated backwards: re-target the next grid line
                slot.pendBeat = std::ceil(beat0 / g - 1.0e-9) * g;
            if (slot.pendBeat >= beat0 + (double) n * bps - 1.0e-12) continue;   // not in this block
            off = juce::jlimit(0, n - 1, (int) std::ceil((slot.pendBeat - beat0) / bps - 1.0e-9));
            startBeat = slot.pendBeat;
        }   // tempo vanished while waiting: start now
        slot.pendActive = false;
        slot.syncPending.store(false, std::memory_order_relaxed);
        for (auto& v : slot.voices)
            if (v.active && ! v.stopping) {
                v.stopping = true;
                v.envStep = -std::max(v.env, 1.0e-6f) / (float) rampFrames;
            }
        const bool fadeFromZero = slot.params.fadeInMs * sampleRate / 1000.0 < rampFrames;
        startVoice(slot, off, 0, 0, true, fadeFromZero, slot.params.route, n, startBeat, beatKnown);
    }
}

void PlaybackEngine::renderVoice(AudioSlot& slot, Voice& v, float* const* dL, float* const* dR, int n) noexcept {
    const ClipData* c = v.clip;
    const bool streamed = c->streamed;
    const auto& P = slot.params;
    std::int64_t s = 0, e = 0, regionLen = 0;
    if (streamed) {
        s = v.sS;
        regionLen = v.sLen;
        e = s + regionLen;
    } else {
        regionOf(P.startFrac, P.endFrac, c->audio.getNumSamples(), s, e);
        regionLen = e - s;
    }
    if (regionLen < 1) { endVoice(v); return; }
    const bool loop = P.mode == 1;
    const bool syncActive = loop && P.tempoSync && P.syncLoopBeats > 0.0 && beatKnown && v.hasBeat;

    // ---- streamed voices: ring bookkeeping for this block
    StreamPool::Ring* rg = streamed && v.ring >= 0 ? &pool.ring(v.ring) : nullptr;
    juce::uint64 w = 0;
    int hN = 0;
    const float* h0 = nullptr;
    const float* h1 = nullptr;
    if (streamed) {
        hN = c->headFrames;
        if (hN > 0) { h0 = c->head.getReadPointer(0); h1 = c->head.getReadPointer(1); }
        if (rg != nullptr) {
            rg->loop.store(loop && ! syncActive, std::memory_order_relaxed);
            rg->consumeSeq.store(v.seq, std::memory_order_release);
            w = rg->writeSeq.load(std::memory_order_acquire);
        }
        if (v.waiting) {
            const auto need = (juce::uint64) std::min<std::int64_t>(regionLen, std::max(4096, 2 * maxBlockSize));
            if (rg != nullptr && w >= need) {
                v.waiting = false;
            } else {
                v.waited += n;
                if (rg == nullptr || (double) v.waited > 2.0 * sampleRate) endVoice(v);
                return;
            }
        }
    }

    // ---- tempo-synced loop: where (if anywhere) the loop restarts inside this block
    int restartAt = -1;
    if (syncActive) {
        const double period = P.syncLoopBeats;
        const double beat1 = beat0 + (double) n * bps;
        if (v.startBeat + (double) (v.lastK + 1) * period < beat0 - 1.0e-9)   // relocation / long gap: skip passed boundaries
            v.lastK = (std::int64_t) std::floor((beat0 - v.startBeat) / period);
        const double b = v.startBeat + (double) (v.lastK + 1) * period;
        if (b < beat1 - 1.0e-12) {
            restartAt = juce::jlimit(v.delay, n - 1, (int) std::ceil((b - beat0) / bps - 1.0e-9));
            ++v.lastK;
        }
    }

    float* L = dL != nullptr ? dL[v.route] : nullptr;
    float* R = dR != nullptr ? dR[v.route] : nullptr;
    const float target = dbToGain(P.gainDb);
    const float gstep = (target - v.gain) / (float) n;
    const double fadeIn = P.fadeInMs * sampleRate / 1000.0;
    const double fadeOutEnd = std::max(P.fadeOutMs * sampleRate / 1000.0, (double) rampFrames);
    const float* c0 = nullptr;
    const float* c1 = nullptr;
    if (! streamed) {
        c0 = c->audio.getReadPointer(0);
        c1 = c->audio.getNumChannels() > 1 ? c->audio.getReadPointer(1) : c0;
    }
    if (! loop && v.pos > regionLen) v.pos = regionLen;   // trim changed under a one-shot
    const float duckStep = 1.0f / (float) rampFrames;
    juce::uint64 missed = 0;
    int i = v.delay;
    v.delay = 0;
    for (; i < n; ++i) {
        if (i == restartAt) {
            v.pos = 0;
            v.firstPass = false;
            v.rampIn = rampFrames;
            restartAt = -1;
            if (streamed) {   // new pass = new playback sequence: re-attach a ring at sequence 0 (head covers the start)
                if (v.ring >= 0) pool.release(v.ring);
                v.ring = -1;
                rg = nullptr;
                if (! attachRing(v, *c, P)) { endVoice(v); break; }
                if (v.ring >= 0) rg = &pool.ring(v.ring);
                w = rg != nullptr ? rg->writeSeq.load(std::memory_order_acquire) : 0;
                if (v.waiting) break;   // no head: resumes once the ring is ready (next block)
            }
        }
        if (v.pos >= regionLen) {
            if (syncActive) continue;   // the content ended before the next grid line: silence until the restart
            if (loop) { v.pos = 0; v.firstPass = false; }
            else { endVoice(v); break; }
        }
        float a = 1.0f;
        if (v.firstPass && fadeIn > 0.0 && (double) v.played < fadeIn) a = (float) ((double) v.played / fadeIn);
        if (! loop) {
            const auto rem = (double) (regionLen - v.pos);
            if (rem < fadeOutEnd) a *= (float) (rem / fadeOutEnd);
        }
        if (restartAt >= 0) {   // declick the cut at the grid line
            const int toBoundary = restartAt - i;
            if (toBoundary < rampFrames) a *= (float) toBoundary / (float) rampFrames;
        }
        if (v.rampIn > 0) { a *= 1.0f - (float) v.rampIn / (float) rampFrames; --v.rampIn; }
        float xl, xr;
        if (! streamed) {
            const auto idx = P.reverse ? e - 1 - v.pos : s + v.pos;
            xl = c0[idx];
            xr = c1[idx];
        } else if (v.seq < (juce::uint64) hN) {
            xl = h0[v.seq];
            xr = h1[v.seq];
        } else if (rg != nullptr && v.seq < w) {
            const auto ix = (size_t) (v.seq & (juce::uint64) rg->mask);
            xl = rg->L[(int) ix];
            xr = rg->R[(int) ix];
        } else {
            xl = xr = 0.0f;
            ++missed;
        }
        if (v.envStep != 0.0f) {
            v.env += v.envStep;
            if (v.env >= 1.0f) { v.env = 1.0f; v.envStep = 0.0f; }
            else if (v.env <= 0.0f) { v.env = 0.0f; endVoice(v); break; }
        }
        if (v.nextRoute >= 0) {
            v.duck -= duckStep;
            if (v.duck <= 0.0f) {
                v.duck = 0.0f;
                v.route = v.nextRoute;
                v.nextRoute = -1;
                L = dL != nullptr ? dL[v.route] : nullptr;
                R = dR != nullptr ? dR[v.route] : nullptr;
            }
        } else if (v.duck < 1.0f) {
            v.duck = std::min(1.0f, v.duck + duckStep);
        }
        v.gain += gstep;
        const float g = v.gain * a * v.env * v.duck;
        if (L != nullptr) L[i] += xl * g;
        if (R != nullptr) R[i] += xr * g;
        ++v.pos;
        ++v.played;
        ++v.seq;
    }
    v.gain = target;
    if (missed > 0) pool.noteUnderrun(missed);
}

void PlaybackEngine::renderBed(float* const* dL, float* const* dR, int n, bool mute) noexcept {
    auto& b = *bed;
    auto& slot = (*audioSlots)[(size_t) b.s.slot];
    bool want = b.s.enabled && ! mute && slot.current != nullptr;
    bool switching = false;
    if (b.clip != slot.current) {
        if (b.clip != nullptr && slot.current != nullptr && b.clip->streamed && slot.current->streamed
            && b.clip->sourceId == slot.current->sourceId) {
            b.clip = slot.current;   // only the pre-buffered head changed (trim edit): the bed does not use it
        } else if (b.clip != nullptr && b.env > 0.0f) {
            want = false;
            switching = true;
        } else {
            b.clip = slot.current;
            b.running = false;
        }
    }
    if (b.clip == nullptr) { b.env = 0.0f; return; }
    if (! want && b.env <= 0.0f) { b.running = false; return; }
    if (b.clip->streamed) { renderBedStreamed(want, switching, dL, dR, n, mute); return; }
    if (b.cur.ring >= 0 || b.next.ring >= 0) {   // the bed was streaming before: give its rings back
        if (b.cur.ring >= 0) pool.release(b.cur.ring);
        if (b.next.ring >= 0) pool.release(b.next.ring);
        b.cur.ring = b.next.ring = -1;
        b.nextPicked = false;
    }

    const ClipData& c = *b.clip;
    const auto clipLen = (std::int64_t) c.audio.getNumSamples();
    if (clipLen < 3) return;
    const double sr = sampleRate;
    auto pick = [&](Bed::Grain& g) {
        const auto minLen = juce::jlimit<std::int64_t>(3, clipLen, (std::int64_t) (b.s.minLoopSec * sr));
        const auto maxLen = juce::jlimit<std::int64_t>(minLen, clipLen, (std::int64_t) (b.s.maxLoopSec * sr));
        g.len = std::min(maxLen, minLen + (std::int64_t) (b.rng.next01() * (double) (maxLen - minLen + 1)));
        g.start = std::min(clipLen - g.len, (std::int64_t) (b.rng.next01() * (double) (clipLen - g.len + 1)));
        g.pos = 0;
        g.xf = juce::jlimit<std::int64_t>(1, std::max<std::int64_t>(1, minLen / 3), (std::int64_t) (b.s.crossfadeSec * sr));
    };
    if (! b.running) {
        b.rng.seed(b.s.seed);
        b.levelDb = b.levelTarget = 0.0;
        b.levelCountdown = 0;
        b.lp[0] = b.lp[1] = 0.0f;
        b.xfading = false;
        pick(b.cur);
        b.running = true;
    }
    const float inStep = (float) (1.0 / (std::max(b.s.crossfadeSec, 1.0) * sr));
    const float outStep = mute ? 1.0f / (float) rampFrames : (float) (1.0 / ((switching ? 0.05 : 0.5) * sr));
    const double levelCoef = 1.0 - std::exp(-1.0 / (1.5 * sr));
    const bool lpOn = b.s.lowpassHz > 0.0f && b.s.lowpassHz < 0.45 * sr;
    const float lpCoef = lpOn ? (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * b.s.lowpassHz / sr)) : 1.0f;
    const float baseGain = dbToGain(b.s.gainDb);
    const float* c0 = c.audio.getReadPointer(0);
    const float* c1 = c.audio.getNumChannels() > 1 ? c.audio.getReadPointer(1) : c0;
    float* L = dL != nullptr ? dL[b.s.route] : nullptr;
    float* R = dR != nullptr ? dR[b.s.route] : nullptr;
    constexpr double halfPi = juce::MathConstants<double>::halfPi;

    for (int i = 0; i < n; ++i) {
        b.env = want ? std::min(1.0f, b.env + inStep) : std::max(0.0f, b.env - outStep);
        if (b.levelCountdown <= 0) {
            b.levelTarget = (b.rng.next01() * 2.0 - 1.0) * b.s.levelVariationDb;
            b.levelCountdown = (std::int64_t) ((3.0 + 5.0 * b.rng.next01()) * sr);
        }
        --b.levelCountdown;
        b.levelDb += (b.levelTarget - b.levelDb) * levelCoef;

        if (! b.xfading && b.cur.pos >= b.cur.len - b.cur.xf) {
            pick(b.next);
            b.next.xf = std::min(b.next.xf, b.next.len / 3 > 0 ? b.next.len / 3 : 1);
            b.xfading = true;
            b.xfPos = 0;
        }
        const auto ia = b.cur.start + std::min(b.cur.pos, b.cur.len - 1);
        float l = c0[ia], r = c1[ia];
        if (b.xfading) {
            const double th = halfPi * ((double) b.xfPos + 0.5) / (double) b.cur.xf;
            const float ga = (float) std::cos(th), gb = (float) std::sin(th);
            const auto ib = b.next.start + std::min(b.next.pos, b.next.len - 1);
            l = l * ga + c0[ib] * gb;
            r = r * ga + c1[ib] * gb;
            ++b.next.pos;
            if (++b.xfPos >= b.cur.xf) { b.cur = b.next; b.xfading = false; }
            else ++b.cur.pos;
        } else {
            ++b.cur.pos;
        }
        b.lp[0] += lpCoef * (l - b.lp[0]);
        b.lp[1] += lpCoef * (r - b.lp[1]);
        const float g = baseGain * (float) std::pow(10.0, b.levelDb * 0.05) * b.env;
        if (L != nullptr) L[i] += b.lp[0] * g;
        if (R != nullptr) R[i] += b.lp[1] * g;
    }
    if (! want && b.env <= 0.0f) b.running = false;
}

// Crowd bed on a streamed clip. Same musical behaviour as the resident bed (random regions, equal-power crossfades,
// slow level drift, low-pass) but each region is a read-ahead ring: the NEXT region is requested kBedLookaheadSec
// before its crossfade starts. Regions are at least kMaxMinLoopStreamSeconds long so the look-ahead always fits.
void PlaybackEngine::renderBedStreamed(bool want, bool switching, float* const* dL, float* const* dR, int n, bool mute) noexcept {
    auto& b = *bed;
    const ClipData& c = *b.clip;
    const auto clipLen = c.frames;
    if (clipLen < 3) return;
    const double sr = sampleRate;
    auto releaseGrain = [this](Bed::Grain& g) { if (g.ring >= 0) pool.release(g.ring); g.ring = -1; };
    const std::int64_t lookahead = (std::int64_t) (kBedLookaheadSec * sr);
    auto pick = [&](Bed::Grain& g) {
        const auto minLen = juce::jlimit<std::int64_t>(3, clipLen, (std::int64_t) (std::max(b.s.minLoopSec, (double) kMaxMinLoopStreamSeconds) * sr));
        const auto maxLen = juce::jlimit<std::int64_t>(minLen, clipLen, (std::int64_t) (b.s.maxLoopSec * sr));
        g.len = std::min(maxLen, minLen + (std::int64_t) (b.rng.next01() * (double) (maxLen - minLen + 1)));
        g.start = std::min<std::int64_t>(clipLen - g.len, (std::int64_t) (b.rng.next01() * (double) (clipLen - g.len + 1)));
        g.pos = 0;
        g.xf = juce::jlimit<std::int64_t>(1, std::max<std::int64_t>(1, minLen / 3), (std::int64_t) (b.s.crossfadeSec * sr));
        releaseGrain(g);
        g.ring = pool.acquire(c.sourceId, g.start, g.len, false, false, 0);
    };
    if (! b.running) {
        releaseGrain(b.cur);
        releaseGrain(b.next);
        b.rng.seed(b.s.seed);
        b.levelDb = b.levelTarget = 0.0;
        b.levelCountdown = 0;
        b.lp[0] = b.lp[1] = 0.0f;
        b.xfading = false;
        b.nextPicked = false;
        pick(b.cur);
        b.running = true;
    }
    const float inStep = (float) (1.0 / (std::max(b.s.crossfadeSec, 1.0) * sr));
    const float outStep = mute ? 1.0f / (float) rampFrames : (float) (1.0 / ((switching ? 0.05 : 0.5) * sr));
    const double levelCoef = 1.0 - std::exp(-1.0 / (1.5 * sr));
    const bool lpOn = b.s.lowpassHz > 0.0f && b.s.lowpassHz < 0.45 * sr;
    const float lpCoef = lpOn ? (float) (1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * b.s.lowpassHz / sr)) : 1.0f;
    const float baseGain = dbToGain(b.s.gainDb);
    float* L = dL != nullptr ? dL[b.s.route] : nullptr;
    float* R = dR != nullptr ? dR[b.s.route] : nullptr;
    constexpr double halfPi = juce::MathConstants<double>::halfPi;

    auto ringState = [&](Bed::Grain& g, StreamPool::Ring*& rg, juce::uint64& w) {
        rg = g.ring >= 0 ? &pool.ring(g.ring) : nullptr;
        w = 0;
        if (rg != nullptr) {
            rg->consumeSeq.store((juce::uint64) g.pos, std::memory_order_release);
            w = rg->writeSeq.load(std::memory_order_acquire);
        }
    };
    StreamPool::Ring *rc = nullptr, *rn = nullptr;
    juce::uint64 wc = 0, wn = 0;
    ringState(b.cur, rc, wc);
    ringState(b.next, rn, wn);
    juce::uint64 missed = 0;
    auto fetch = [&](const Bed::Grain& g, StreamPool::Ring* rg, juce::uint64 w, float& l, float& r) {
        if (rg != nullptr && (juce::uint64) g.pos < w) {
            const int ix = (int) ((juce::uint64) g.pos & (juce::uint64) rg->mask);
            l = rg->L[ix];
            r = rg->R[ix];
        } else {
            l = r = 0.0f;
            ++missed;
        }
    };

    // a fresh region whose first frames have not been produced yet holds still instead of counting underruns
    const bool starting = b.cur.pos == 0 && wc == 0 && ! b.xfading;
    if (starting && rc != nullptr) {
        b.env = want ? b.env : std::max(0.0f, b.env - outStep * (float) n);
        if (! want && b.env <= 0.0f) { b.running = false; releaseGrain(b.cur); releaseGrain(b.next); }
        return;
    }

    for (int i = 0; i < n; ++i) {
        b.env = want ? std::min(1.0f, b.env + inStep) : std::max(0.0f, b.env - outStep);
        if (b.levelCountdown <= 0) {
            b.levelTarget = (b.rng.next01() * 2.0 - 1.0) * b.s.levelVariationDb;
            b.levelCountdown = (std::int64_t) ((3.0 + 5.0 * b.rng.next01()) * sr);
        }
        --b.levelCountdown;
        b.levelDb += (b.levelTarget - b.levelDb) * levelCoef;

        if (! b.xfading && ! b.nextPicked && b.cur.pos >= b.cur.len - b.cur.xf - lookahead) {
            pick(b.next);
            b.next.xf = std::min(b.next.xf, b.next.len / 3 > 0 ? b.next.len / 3 : 1);
            b.nextPicked = true;
            rn = b.next.ring >= 0 ? &pool.ring(b.next.ring) : nullptr;
            wn = 0;
        }
        if (! b.xfading && b.nextPicked && b.cur.pos >= b.cur.len - b.cur.xf) {
            b.xfading = true;
            b.xfPos = 0;
        }
        float l, r;
        fetch(b.cur, rc, wc, l, r);
        if (b.xfading) {
            const double th = halfPi * ((double) b.xfPos + 0.5) / (double) b.cur.xf;
            const float ga = (float) std::cos(th), gb = (float) std::sin(th);
            float nl, nr;
            fetch(b.next, rn, wn, nl, nr);
            l = l * ga + nl * gb;
            r = r * ga + nr * gb;
            ++b.next.pos;
            if (++b.xfPos >= b.cur.xf) {
                releaseGrain(b.cur);
                b.cur = b.next;
                b.next = {};
                rc = rn; wc = wn; rn = nullptr; wn = 0;
                b.xfading = false;
                b.nextPicked = false;
            } else {
                ++b.cur.pos;
            }
        } else {
            ++b.cur.pos;
        }
        b.lp[0] += lpCoef * (l - b.lp[0]);
        b.lp[1] += lpCoef * (r - b.lp[1]);
        const float g = baseGain * (float) std::pow(10.0, b.levelDb * 0.05) * b.env;
        if (L != nullptr) L[i] += b.lp[0] * g;
        if (R != nullptr) R[i] += b.lp[1] * g;
    }
    if (missed > 0) pool.noteUnderrun(missed);
    if (! want && b.env <= 0.0f) {
        b.running = false;
        releaseGrain(b.cur);
        releaseGrain(b.next);
        b.nextPicked = false;
    }
}

void PlaybackEngine::process(float* const* dL, float* const* dR, int n, bool mute) noexcept {
    if (n <= 0 || sampleRate <= 0.0) return;
    updateBeatClock(n);
    auto& slots = *audioSlots;
    for (auto& slot : slots) {   // adopt newly loaded clips
        if (slot.previous != nullptr || retire.isFull() || slot.pending.load(std::memory_order_acquire) == nullptr) continue;
        if (auto* p = slot.pending.exchange(nullptr, std::memory_order_acq_rel)) {
            const bool sameSource = slot.current != nullptr && p->streamed && slot.current->streamed
                                    && p->sourceId == slot.current->sourceId;   // head rebuild: playing voices carry on
            if (! sameSource)
                for (auto& v : slot.voices)
                    if (v.active && ! v.stopping) { v.stopping = true; v.envStep = -std::max(v.env, 1.0e-6f) / (float) rampFrames; }
            slot.previous = slot.current;
            slot.current = p;
        }
    }
    const bool wasMuted = muted;
    muted = mute;   // commands of this block see the current panic state
    Command c;
    while (commands.pop(c)) handleCommand(c, n);

    if (mute && ! wasMuted) {
        for (auto& slot : slots) {
            slot.pendActive = false;
            slot.syncPending.store(false, std::memory_order_relaxed);
            for (auto& v : slot.voices)
                if (v.active && ! v.stopping) { v.stopping = true; v.envStep = -std::max(v.env, 1.0e-6f) / (float) rampFrames; }
        }
    }
    fireDueTriggers(n);

    for (auto& slot : slots) {
        float pos01 = slot.pos01.load(std::memory_order_relaxed);
        bool playing = false;
        for (auto& v : slot.voices) {
            if (! v.active) { if (v.ring >= 0) { pool.release(v.ring); v.ring = -1; } continue; }
            renderVoice(slot, v, dL, dR, n);
            if (! v.active && v.ring >= 0) { pool.release(v.ring); v.ring = -1; }
            if (v.active && ! v.stopping) {
                playing = true;
                double region;
                if (v.clip->streamed) region = std::max<double>(1.0, (double) v.sLen);
                else region = std::max<double>(1.0, (slot.params.endFrac - slot.params.startFrac) * v.clip->audio.getNumSamples());
                pos01 = (float) juce::jlimit(0.0, 1.0, (double) v.pos / region);
            }
        }
        slot.playing.store(playing, std::memory_order_relaxed);
        slot.pos01.store(playing ? pos01 : 0.0f, std::memory_order_relaxed);
        if (slot.previous != nullptr) {
            bool used = bed->clip == slot.previous;
            for (auto& v : slot.voices) used = used || (v.active && v.clip == slot.previous);
            if (! used && retire.push(slot.previous)) slot.previous = nullptr;
        }
    }
    renderBed(dL, dR, n, mute);
    tempoSet = false;
}

} // namespace crowdmike::rec
