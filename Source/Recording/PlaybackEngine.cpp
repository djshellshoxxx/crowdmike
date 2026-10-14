#include "PlaybackEngine.h"
#include "AudioFileLoader.h"
#include <cmath>

namespace crowdmike::rec {

namespace {
constexpr int kVoicesPerSlot = 3;
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
    juce::AudioBuffer<float> audio;
    double sourceRate = 0.0;
    int sourceChannels = 0;
    double durationSec = 0.0;
    std::vector<float> wmin, wmax;
    juce::String name;
    std::size_t bytes() const { return (size_t) audio.getNumChannels() * (size_t) audio.getNumSamples() * sizeof(float); }
    JUCE_LEAK_DETECTOR(ClipData)
};

struct PlaybackEngine::Voice {
    ClipData* clip = nullptr;
    bool active = false, stopping = false, firstPass = true;
    std::int64_t pos = 0, played = 0;
    float env = 1.0f, envStep = 0.0f, gain = 1.0f;
    int route = 0, delay = 0;
};

struct PlaybackEngine::AudioSlot {
    std::atomic<ClipData*> pending { nullptr };
    ClipData* current = nullptr;
    ClipData* previous = nullptr;   // fading out after a swap; retired once unused
    Params params;
    Voice voices[kVoicesPerSlot];
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
    struct Grain { std::int64_t start = 0, len = 0, pos = 0, xf = 1; };
    BedSettings s;
    ClipData* clip = nullptr;
    bool running = false, xfading = false;
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
            std::tuple<int, juce::File, juce::uint64> job;
            bool have = false;
            {
                std::lock_guard<std::mutex> l(owner.controlLock);
                if (! owner.jobs.empty()) { job = owner.jobs.front(); owner.jobs.pop_front(); have = true; }
            }
            if (have) {
                owner.runJob(std::get<0>(job), std::get<1>(job), std::get<2>(job));
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
    return p;
}

void PlaybackEngine::prepare(double sr, int maxBlock) {
    const bool rateChanged = std::abs(sr - sampleRate) > 1.0e-6;
    maxBlockSize = std::max(1, maxBlock);
    if (rateChanged) clearAudioState();
    sampleRate = sr;
    rampFrames = std::max(1, (int) std::lround(kClickRampMs * sr / 1000.0));
    for (auto& slot : *audioSlots)
        for (auto& v : slot.voices) v = Voice {};
    bed->running = false;
    bed->env = 0.0f;
    muted = false;
    cancelLoads = false;
    {
        std::lock_guard<std::mutex> l(controlLock);
        if (rateChanged)
            for (int i = 0; i < kMaxClips; ++i) {
                auto& cs = (*controlSlots)[(size_t) i];
                if (cs.file == juce::File()) continue;
                ++cs.generation;
                cs.loaded = nullptr;
                cs.loading = true;
                jobs.emplace_back(i, cs.file, cs.generation);
                jobsPending.fetch_add(1);
            }
    }
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
    {
        std::lock_guard<std::mutex> l(controlLock);
        jobsPending.fetch_sub((int) jobs.size());
        jobs.clear();
        for (auto& cs : *controlSlots) cs.loading = false;
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
        slot.pos01 = 0.0f;
    }
    bed->clip = nullptr;
    bed->running = false;
    bed->env = 0.0f;
}

void PlaybackEngine::releaseRetired() {
    retire.drain([](ClipData* c) { c->decReferenceCount(); });
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
        jobs.emplace_back(slot, file, cs.generation);
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

void PlaybackEngine::runJob(int slot, const juce::File& file, juce::uint64 gen) {
    juce::int64 budget = kMaxTotalClipBytes;
    double sr = 0.0;
    {
        std::lock_guard<std::mutex> l(controlLock);
        if ((*controlSlots)[(size_t) slot].generation != gen) return;
        for (int i = 0; i < kMaxClips; ++i)
            if (i != slot && (*controlSlots)[(size_t) i].loaded != nullptr)
                budget -= (juce::int64) (*controlSlots)[(size_t) i].loaded->bytes();
        sr = sampleRate;
    }
    AudioFileLoader::Result r;
    juce::String err;
    juce::ReferenceCountedObjectPtr<ClipData> clip;
    if (budget <= 0) err = "Clip memory budget exhausted";
    else if (AudioFileLoader::load(file, sr, 2, kMaxClipSeconds, budget, r, err, &cancelLoads)) {
        clip = new ClipData();
        clip->audio = std::move(r.audio);
        clip->sourceRate = r.sourceSampleRate;
        clip->sourceChannels = r.sourceChannels;
        clip->durationSec = (double) clip->audio.getNumSamples() / sr;
        clip->name = file.getFileNameWithoutExtension();
        const int len = clip->audio.getNumSamples();
        const int pts = std::max(1, std::min(kWaveformPoints, len));
        clip->wmin.assign((size_t) pts, 0.0f);
        clip->wmax.assign((size_t) pts, 0.0f);
        for (int p = 0; p < pts; ++p) {
            const int a = (int) ((juce::int64) p * len / pts), b = std::max(a + 1, (int) ((juce::int64) (p + 1) * len / pts));
            float mn = 0.0f, mx = 0.0f;
            for (int c = 0; c < clip->audio.getNumChannels(); ++c) {
                const auto range = juce::FloatVectorOperations::findMinAndMax(clip->audio.getReadPointer(c, a), b - a);
                mn = c == 0 ? range.getStart() : std::min(mn, range.getStart());
                mx = c == 0 ? range.getEnd() : std::max(mx, range.getEnd());
            }
            clip->wmin[(size_t) p] = mn;
            clip->wmax[(size_t) p] = mx;
        }
    }
    std::lock_guard<std::mutex> l(controlLock);
    auto& cs = (*controlSlots)[(size_t) slot];
    if (cs.generation != gen) return;   // superseded (clip released here, on the loader thread)
    cs.loading = false;
    if (clip != nullptr) {
        cs.loaded = clip;
        cs.error = {};
        publish(slot, clip.get());
    } else {
        cs.error = err.isNotEmpty() ? err : juce::String("Load failed");
    }
}

void PlaybackEngine::publish(int slot, ClipData* c) {
    c->incReferenceCount();
    if (auto* old = (*audioSlots)[(size_t) slot].pending.exchange(c, std::memory_order_acq_rel)) old->decReferenceCount();
}

bool PlaybackEngine::trigger(int slot, int offset) {
    if ((unsigned) slot >= (unsigned) kMaxClips) return false;
    Command c;
    c.type = Command::Trigger;
    c.slot = slot;
    c.offset = std::max(0, offset);
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
        (*controlSlots)[(size_t) slot].settings = s;
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
        i.numChannels = cs.loaded->audio.getNumChannels();
        i.sourceSampleRate = cs.loaded->sourceRate;
    }
    i.name = cs.settings.name.isNotEmpty() ? cs.settings.name
                                            : (cs.loaded != nullptr ? cs.loaded->name : cs.file.getFileNameWithoutExtension());
    const auto& as = (*audioSlots)[(size_t) slot];
    i.playing = as.playing.load(std::memory_order_relaxed);
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
void PlaybackEngine::startVoice(AudioSlot& slot, int offset, std::int64_t pos, std::int64_t played, bool firstPass,
                                bool fadeFromZero, int route, int n) noexcept {
    Voice* v = nullptr;
    for (auto& x : slot.voices)
        if (! x.active) { v = &x; break; }
    if (v == nullptr)   // steal the quietest fading voice
        for (auto& x : slot.voices)
            if (x.stopping && (v == nullptr || x.env < v->env)) v = &x;
    if (v == nullptr) v = &slot.voices[0];
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
    switch (c.type) {
        case Command::Trigger: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            if (muted || slot.current == nullptr) return;
            for (auto& v : slot.voices) stopVoice(v, (float) rampFrames);
            const bool fadeFromZero = slot.params.fadeInMs * sampleRate / 1000.0 < rampFrames;
            startVoice(slot, c.offset, 0, 0, true, fadeFromZero, slot.params.route, n);
            break;
        }
        case Command::Stop: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            for (auto& v : slot.voices) stopVoice(v, stopFrames(slot.params));
            break;
        }
        case Command::StopAll:
            for (auto& slot : *audioSlots)
                for (auto& v : slot.voices) stopVoice(v, stopFrames(slot.params));
            break;
        case Command::SetParams: {
            auto& slot = (*audioSlots)[(size_t) c.slot];
            slot.params = c.params;
            for (auto& v : slot.voices) {
                if (! v.active || v.stopping || v.route == c.params.route || v.clip != slot.current) continue;
                const Voice old = v;   // crossfade to the new route at the same position
                stopVoice(v, (float) rampFrames);
                startVoice(slot, 0, old.pos, old.played, old.firstPass, true, c.params.route, n);
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
            if (slot.previous != nullptr) {   // still fading from an earlier swap: cut it now
                for (auto& v : slot.voices)
                    if (v.clip == slot.previous) v.active = false;
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

void PlaybackEngine::renderVoice(AudioSlot& slot, Voice& v, float* const* dL, float* const* dR, int n) noexcept {
    const ClipData* c = v.clip;
    const int len = c->audio.getNumSamples();
    const auto& P = slot.params;
    const auto s = juce::jlimit<std::int64_t>(0, len, std::llround(P.startFrac * len));
    const auto e = juce::jlimit<std::int64_t>(0, len, std::llround(P.endFrac * len));
    const auto regionLen = e - s;
    if (regionLen < 1) { v.active = false; return; }
    const bool loop = P.mode == 1;
    float* L = dL != nullptr ? dL[v.route] : nullptr;
    float* R = dR != nullptr ? dR[v.route] : nullptr;
    const float target = dbToGain(P.gainDb);
    const float gstep = (target - v.gain) / (float) n;
    const double fadeIn = P.fadeInMs * sampleRate / 1000.0;
    const double fadeOutEnd = std::max(P.fadeOutMs * sampleRate / 1000.0, (double) rampFrames);
    const float* c0 = c->audio.getReadPointer(0);
    const float* c1 = c->audio.getNumChannels() > 1 ? c->audio.getReadPointer(1) : c0;
    if (! loop && v.pos > regionLen) v.pos = regionLen;   // trim changed under a one-shot
    int i = v.delay;
    v.delay = 0;
    for (; i < n; ++i) {
        if (v.pos >= regionLen) {
            if (loop) { v.pos = 0; v.firstPass = false; }
            else { v.active = false; break; }
        }
        const auto idx = P.reverse ? e - 1 - v.pos : s + v.pos;
        float a = 1.0f;
        if (v.firstPass && fadeIn > 0.0 && (double) v.played < fadeIn) a = (float) ((double) v.played / fadeIn);
        if (! loop) {
            const auto rem = (double) (regionLen - v.pos);
            if (rem < fadeOutEnd) a *= (float) (rem / fadeOutEnd);
        }
        if (v.envStep != 0.0f) {
            v.env += v.envStep;
            if (v.env >= 1.0f) { v.env = 1.0f; v.envStep = 0.0f; }
            else if (v.env <= 0.0f) { v.env = 0.0f; v.active = false; break; }
        }
        v.gain += gstep;
        const float g = v.gain * a * v.env;
        if (L != nullptr) L[i] += c0[idx] * g;
        if (R != nullptr) R[i] += c1[idx] * g;
        ++v.pos;
        ++v.played;
    }
    v.gain = target;
}

void PlaybackEngine::renderBed(float* const* dL, float* const* dR, int n, bool mute) noexcept {
    auto& b = *bed;
    auto& slot = (*audioSlots)[(size_t) b.s.slot];
    bool want = b.s.enabled && ! mute && slot.current != nullptr;
    bool switching = false;
    if (b.clip != slot.current) {
        if (b.clip != nullptr && b.env > 0.0f) { want = false; switching = true; }
        else { b.clip = slot.current; b.running = false; }
    }
    if (b.clip == nullptr) { b.env = 0.0f; return; }
    if (! want && b.env <= 0.0f) { b.running = false; return; }

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

void PlaybackEngine::process(float* const* dL, float* const* dR, int n, bool mute) noexcept {
    if (n <= 0 || sampleRate <= 0.0) return;
    auto& slots = *audioSlots;
    for (auto& slot : slots) {   // adopt newly loaded clips
        if (slot.previous != nullptr || retire.isFull() || slot.pending.load(std::memory_order_acquire) == nullptr) continue;
        if (auto* p = slot.pending.exchange(nullptr, std::memory_order_acq_rel)) {
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

    if (mute && ! wasMuted)
        for (auto& slot : slots)
            for (auto& v : slot.voices)
                if (v.active && ! v.stopping) { v.stopping = true; v.envStep = -std::max(v.env, 1.0e-6f) / (float) rampFrames; }

    for (auto& slot : slots) {
        float pos01 = slot.pos01.load(std::memory_order_relaxed);
        bool playing = false;
        for (auto& v : slot.voices) {
            if (! v.active) continue;
            renderVoice(slot, v, dL, dR, n);
            if (v.active && ! v.stopping) {
                playing = true;
                const auto len = v.clip->audio.getNumSamples();
                const auto region = std::max<double>(1.0, (slot.params.endFrac - slot.params.startFrac) * len);
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
}

} // namespace crowdmike::rec
