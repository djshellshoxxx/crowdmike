#include "SoundcheckPlayer.h"
#include "AudioFileLoader.h"
#include <algorithm>

namespace crowdmike::rec {

struct SoundcheckPlayer::Data {
    juce::AudioBuffer<float> audio;     // one channel per mapped input
    std::array<int, kMaxInputs> chanOf {};   // input -> audio channel, -1 = none
    std::int64_t frames = 0;
    JUCE_LEAK_DETECTOR(Data)
};

class SoundcheckPlayer::Loader : public juce::Thread {
public:
    Loader(std::function<void()> f) : juce::Thread("CrowdMike soundcheck loader"), fn(std::move(f)) {}
    void run() override { fn(); }
private:
    std::function<void()> fn;
};

SoundcheckPlayer::SoundcheckPlayer() = default;

SoundcheckPlayer::~SoundcheckPlayer() { releaseResources(); }

void SoundcheckPlayer::prepare(double sr, int) {
    const bool changed = std::abs(sr - sampleRate) > 1.0e-6;
    sampleRate = sr;
    rampFrames = std::max(1, (int) std::lround(0.005 * sr));
    playing = false; armedA = false; env = 0.0f; dec = decTarget = 0.0f; pendingAction = 0; xfLen = xfK = 0;
    replacing = false; playingState = false;
    if (changed) {
        std::vector<Source> srcs;
        {
            std::lock_guard<std::mutex> l(controlLock);
            srcs = lastSources;
        }
        clearAudioData();
        if (! srcs.empty()) startLoad(srcs, {});
    }
}

void SoundcheckPlayer::releaseResources() {
    cancel = true;
    if (loader != nullptr) { loader->stopThread(10000); loader.reset(); }
    {
        std::lock_guard<std::mutex> l(controlLock);
        loadState.loading = false;
    }
    clearAudioData();
    sampleRate = 0.0;
}

void SoundcheckPlayer::releaseRetired() const {
    retire.drain([](Data* d) { delete d; });
}

void SoundcheckPlayer::clearAudioData() {
    Command c;
    while (commands.pop(c)) {}
    releaseRetired();
    delete current;
    current = nullptr;
    delete pending.exchange(nullptr);
    pos = 0; playing = false; armedA = false; env = 0.0f;
    positionFrames = 0;
    durationFrames = 0;
    replacing = false;
    playingState = false;
    armedState = false;
}

void SoundcheckPlayer::push(const Command& c) {
    releaseRetired();
    commands.push(c);
}

bool SoundcheckPlayer::loadSession(const juce::File& json, MappingFunction mapFn) {
    SessionInfo info;
    if (! SessionMetadata::read(json, info)) {
        std::lock_guard<std::mutex> l(controlLock);
        loadState = {};
        loadState.error = "Cannot read session file " + json.getFullPathName();
        return false;
    }
    auto tracks = info.tracks;
    std::sort(tracks.begin(), tracks.end(), [](auto& a, auto& b) { return a.track < b.track; });
    std::array<bool, kMaxInputs> used {};
    std::vector<Source> srcs;
    juce::String warning;
    for (auto& t : tracks) {
        const int nch = juce::jlimit(1, 2, t.numChannels);
        auto fits = [&](int i) { return i >= 0 && i + nch <= kMaxInputs && ! used[(size_t) i] && (nch == 1 || ! used[(size_t) i + 1]); };
        int in = mapFn ? mapFn(t) : -1;
        if (! fits(in)) {
            in = -1;
            if (! mapFn && fits(t.sourceId)) in = t.sourceId;
        }
        for (int i = 0; in < 0 && i < kMaxInputs; ++i)
            if (fits(i)) in = i;
        if (in < 0) { warning << "Track '" << t.name << "' skipped (no free input). "; continue; }
        for (int c = 0; c < nch; ++c) used[(size_t) (in + c)] = true;
        Source s { in, {}, info.sampleRate, t.track, t.sourceId, t.name };
        for (auto& f : info.filesForTrack(t.track)) s.parts.emplace_back(f.file, f.startFrame);
        if (s.parts.empty()) { warning << "Track '" << t.name << "' has no files. "; continue; }
        srcs.push_back(std::move(s));
    }
    if (srcs.empty()) {
        std::lock_guard<std::mutex> l(controlLock);
        loadState = {};
        loadState.error = "Session has no loadable tracks. " + warning;
        return false;
    }
    startLoad(std::move(srcs), warning);
    return true;
}

bool SoundcheckPlayer::loadFiles(const std::vector<InputFile>& files) {
    std::vector<Source> srcs;
    for (auto& f : files) {
        if (f.inputIndex < 0 || f.inputIndex >= kMaxInputs) continue;
        srcs.push_back({ f.inputIndex, { { f.file, 0 } }, 0.0, -1, -1, f.file.getFileNameWithoutExtension() });
    }
    if (srcs.empty()) {
        std::lock_guard<std::mutex> l(controlLock);
        loadState = {};
        loadState.error = "No valid input files";
        return false;
    }
    startLoad(std::move(srcs), {});
    return true;
}

void SoundcheckPlayer::startLoad(std::vector<Source> srcs, juce::String warning) {
    cancel = true;
    if (loader != nullptr) { loader->stopThread(10000); loader.reset(); }
    cancel = false;
    juce::uint64 gen;
    {
        std::lock_guard<std::mutex> l(controlLock);
        lastSources = srcs;
        gen = ++generation;
        loadState = {};
        loadState.loading = true;
        loadState.error = warning;
        mapping.clear();
    }
    if (sampleRate <= 0.0) return;   // loads at prepare()
    loader = std::make_unique<Loader>([this, s = std::move(srcs), gen] { runLoad(s, gen); });
    loader->startThread();
}

void SoundcheckPlayer::runLoad(const std::vector<Source>& srcs, juce::uint64 gen) {
    struct Decoded { const Source* src; int nch; std::vector<std::pair<juce::AudioBuffer<float>, std::int64_t>> parts; };
    std::vector<Decoded> decoded;
    juce::String errors;
    juce::int64 bytes = 0;
    std::int64_t total = 0;
    for (auto& s : srcs) {
        Decoded d { &s, 1, {} };
        bool ok = true;
        std::int64_t running = 0;
        for (auto& [file, start] : s.parts) {
            AudioFileLoader::Result r;
            juce::String err;
            if (! AudioFileLoader::load(file, sampleRate, 2, 0.0, kMaxMemoryBytes - bytes, r, err, &cancel)) {
                if (cancel.load()) return;
                errors << err << ". ";
                ok = false;
                break;
            }
            bytes += (juce::int64) r.audio.getNumChannels() * r.audio.getNumSamples() * (juce::int64) sizeof(float);
            const auto off = s.partRate > 0.0 ? (std::int64_t) std::llround((double) start * sampleRate / s.partRate) : running;
            d.nch = std::max(d.nch, r.audio.getNumChannels());
            running = off + r.audio.getNumSamples();
            total = std::max(total, running);
            d.parts.emplace_back(std::move(r.audio), off);
        }
        if (ok) {
            if (s.inputIndex + d.nch > kMaxInputs) d.nch = kMaxInputs - s.inputIndex;
            decoded.push_back(std::move(d));
        }
    }
    auto data = std::make_unique<Data>();
    data->chanOf.fill(-1);
    std::vector<InputMapping> map;
    int k = 0;
    for (auto& d : decoded)
        for (int c = 0; c < d.nch; ++c) {
            const int in = d.src->inputIndex + c;
            if (data->chanOf[(size_t) in] < 0) data->chanOf[(size_t) in] = k++;
        }
    bool memOk = true;
    try { data->audio.setSize(std::max(1, k), (int) std::min<std::int64_t>(std::max<std::int64_t>(1, total), std::numeric_limits<int>::max() - 1)); }
    catch (const std::bad_alloc&) { memOk = false; }
    if (memOk) {
        data->audio.clear();
        data->frames = total;
        for (auto& d : decoded)
            for (int c = 0; c < d.nch; ++c) {
                const int in = d.src->inputIndex + c;
                const int ch = data->chanOf[(size_t) in];
                for (auto& [buf, off] : d.parts) {
                    const int src = std::min(c, buf.getNumChannels() - 1);
                    const int n = (int) std::min<std::int64_t>(buf.getNumSamples(), total - off);
                    if (n > 0) data->audio.copyFrom(ch, (int) off, buf, src, 0, n);
                }
                map.push_back({ in, d.src->track, d.src->sourceId, c, d.src->name, d.src->parts.front().first });
            }
    }
    std::lock_guard<std::mutex> l(controlLock);
    if (gen != generation || cancel.load()) return;
    loadState.loading = false;
    if (! memOk) { loadState.error = "Out of memory loading soundcheck"; return; }
    if (decoded.empty()) { loadState.error = errors.isNotEmpty() ? errors : juce::String("Nothing loaded"); return; }
    loadState.error = (loadState.error + errors).trim();
    loadState.loaded = true;
    loadState.numInputs = (int) map.size();
    loadState.durationSeconds = (double) total / sampleRate;
    mapping = std::move(map);
    durationFrames = total;
    delete pending.exchange(data.release());   // a never-adopted previous result is freed here, off the audio thread
}

SoundcheckPlayer::LoadState SoundcheckPlayer::getLoadState() const {
    releaseRetired();
    std::lock_guard<std::mutex> l(controlLock);
    return loadState;
}

std::vector<SoundcheckPlayer::InputMapping> SoundcheckPlayer::getInputMapping() const {
    std::lock_guard<std::mutex> l(controlLock);
    return mapping;
}

bool SoundcheckPlayer::waitForLoad(int timeoutMs) const {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;) {
        {
            std::lock_guard<std::mutex> l(controlLock);
            if (! loadState.loading) return true;
        }
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep(2);
    }
}

void SoundcheckPlayer::setLocked(bool l) {
    locked = l;
    if (l) armedState = false;
}

bool SoundcheckPlayer::arm() {
    if (locked.load()) return false;
    {
        std::lock_guard<std::mutex> l(controlLock);
        if (! loadState.loaded) return false;
    }
    armedState = true;
    push({ Command::Arm });
    return true;
}

void SoundcheckPlayer::disarm() {
    armedState = false;
    push({ Command::Disarm });
}

void SoundcheckPlayer::setArmFadeSeconds(double s) { armFadeSeconds = std::max(kMinArmFadeSeconds, s); }

void SoundcheckPlayer::play() { push({ Command::Play }); }
void SoundcheckPlayer::pause() { push({ Command::Pause }); }
void SoundcheckPlayer::stop() { push({ Command::Stop }); }

void SoundcheckPlayer::seekSeconds(double s) {
    push({ Command::Seek, (std::int64_t) std::llround(std::max(0.0, s) * sampleRate) });
}

void SoundcheckPlayer::setLoopRange(double a, double b, bool enabled) {
    push({ Command::Loop, (std::int64_t) std::llround(std::max(0.0, a) * sampleRate),
           (std::int64_t) std::llround(std::max(0.0, b) * sampleRate), enabled });
}

double SoundcheckPlayer::getPositionSeconds() const noexcept {
    return sampleRate > 0 ? (double) positionFrames.load() / sampleRate : 0.0;
}

double SoundcheckPlayer::getDurationSeconds() const noexcept {
    return sampleRate > 0 ? (double) durationFrames.load() / sampleRate : 0.0;
}

//==== audio thread ================================================================================================
void SoundcheckPlayer::process(float* const* inputs, int numInputChannels, int n) noexcept {
    if (n <= 0 || sampleRate <= 0.0) { replacing.store(false, std::memory_order_relaxed); return; }
    if (pending.load(std::memory_order_acquire) != nullptr && ! retire.isFull()) {
        if (auto* p = pending.exchange(nullptr, std::memory_order_acq_rel)) {
            if (current != nullptr) retire.push(current);
            current = p;
            pos = 0; playing = false; dec = decTarget = 0.0f; pendingAction = 0; xfLen = xfK = 0;
            loopStart = 0; loopEnd = p->frames; loopOn = false;
        }
    }
    const std::int64_t frames = current != nullptr ? current->frames : 0;
    Command c;
    while (commands.pop(c)) {
        switch (c.type) {
            case Command::Play:
                if (current == nullptr) break;
                if (! playing) {
                    if (pos >= frames) pos = loopOn ? loopStart : 0;
                    playing = true;
                    dec = 0.0f;
                }
                decTarget = 1.0f; decStep = 1.0f / (float) rampFrames; pendingAction = 0;
                break;
            case Command::Pause:
                if (playing) { decTarget = 0.0f; decStep = 1.0f / (float) rampFrames; pendingAction = 1; }
                break;
            case Command::Stop:
                if (playing) { decTarget = 0.0f; decStep = (float) (1.0 / (0.25 * sampleRate)); pendingAction = 2; }
                else { pos = loopOn ? loopStart : 0; xfLen = 0; }
                break;
            case Command::Seek: {
                const auto t = juce::jlimit<std::int64_t>(0, frames, c.a);
                if (playing) { seekTarget = t; decTarget = 0.0f; decStep = 1.0f / (float) rampFrames; pendingAction = 3; }
                else { pos = t; xfLen = 0; }
                break;
            }
            case Command::Loop:
                loopStart = juce::jlimit<std::int64_t>(0, frames, c.a);
                loopEnd = juce::jlimit<std::int64_t>(0, frames, c.b);
                loopOn = c.flag && loopEnd > loopStart;
                break;
            case Command::Arm:
                if (! locked.load() && current != nullptr) armedA = true;
                break;
            case Command::Disarm:
                armedA = false;
                break;
        }
    }
    positionFrames.store(pos, std::memory_order_relaxed);
    if (locked.load(std::memory_order_acquire)) {
        armedA = false;
        env = 0.0f;
        replacing.store(false, std::memory_order_relaxed);
        return;
    }
    if (! armedA && env <= 0.0f) { replacing.store(false, std::memory_order_relaxed); return; }
    replacing.store(true, std::memory_order_relaxed);

    const float inStep = (float) (1.0 / (armFadeSeconds * sampleRate));
    const float outStep = (float) (1.0 / (0.5 * sampleRate));
    const int nIn = std::min(numInputChannels, kMaxInputs);
    for (int i = 0; i < n; ++i) {
        env = armedA ? std::min(1.0f, env + inStep) : std::max(0.0f, env - outStep);
        if (dec != decTarget) {
            dec = dec < decTarget ? std::min(decTarget, dec + decStep) : std::max(decTarget, dec - decStep);
            if (dec <= 0.0f && pendingAction != 0) {
                if (pendingAction == 1) playing = false;
                else if (pendingAction == 2) { playing = false; pos = loopOn ? loopStart : 0; }
                else if (pendingAction == 3) { pos = seekTarget; decTarget = 1.0f; }
                xfLen = 0;
                pendingAction = 0;
            }
        }
        const float g = env * dec;
        if (playing && current != nullptr && pos < frames) {
            float a = 1.0f;
            std::int64_t ib = -1;
            if (xfK < xfLen) { a = (float) xfK / (float) xfLen; ib = xfFrom + xfK; }
            for (int ch = 0; ch < nIn; ++ch) {
                float* dst = inputs != nullptr ? inputs[ch] : nullptr;
                if (dst == nullptr) continue;
                const int k = current->chanOf[(size_t) ch];
                if (k < 0) { dst[i] = 0.0f; continue; }
                const float* d = current->audio.getReadPointer(k);
                float x = d[pos];
                if (ib >= 0) x = x * a + (ib < frames ? d[ib] : 0.0f) * (1.0f - a);
                dst[i] = x * g;
            }
            ++pos;
            if (xfK < xfLen) ++xfK;
            if (loopOn && pos >= loopEnd) {
                xfFrom = loopEnd; xfK = 0;
                xfLen = std::min<std::int64_t>(256, (loopEnd - loopStart) / 4);
                pos = loopStart;
            } else if (pos >= frames) {
                playing = false;
            }
        } else {
            for (int ch = 0; ch < nIn; ++ch)
                if (inputs != nullptr && inputs[ch] != nullptr) inputs[ch][i] = 0.0f;
        }
    }
    positionFrames.store(pos, std::memory_order_relaxed);
    playingState.store(playing, std::memory_order_relaxed);
}

} // namespace crowdmike::rec
