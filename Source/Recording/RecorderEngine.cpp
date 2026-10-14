#include "RecorderEngine.h"
#include "FileNaming.h"
#include <algorithm>
#include <cstring>

namespace crowdmike::rec {

namespace {
constexpr std::int64_t kNoFrame = std::numeric_limits<std::int64_t>::max();
constexpr int kChunk = 4096;
constexpr int kMaxMarkers = 100000;
constexpr int kMaxDropoutEvents = 10000;
} // namespace

//==================================================================================================================
struct RecorderEngine::Fifo {
    explicit Fifo(int cap) : capacity(cap), mask(cap - 1) {
        for (auto& c : ch) c.calloc((size_t) cap);
    }
    struct Header { std::uint64_t index; std::int64_t frame; };
    const int capacity, mask;
    juce::HeapBlock<float> ch[2];
    std::atomic<std::uint64_t> writeIdx { 0 }, readIdx { 0 };
    SpscQueue<Header, 1024> headers;
    std::atomic<int> dropouts { 0 };
    // audio thread only
    std::int64_t expectedNext = -1;
    bool needHeader = true;
    // writer only
    std::uint64_t segIndex = 0;
    std::int64_t segFrame = 0;
};

struct RecorderEngine::PreRoll {
    PreRoll(int channels, int frames) : buf(channels, frames), capacity(frames) { buf.clear(); }
    juce::AudioBuffer<float> buf;
    int capacity;
    std::int64_t end = -1, first = -1;
    std::int64_t validStart() const { return std::max(first, end - (std::int64_t) capacity); }

    void writeFrames(std::int64_t f0, const float* const* data, int n) {   // data == nullptr -> zeros
        int done = 0;
        while (done < n) {
            const int off = (int) ((f0 + done) % capacity);
            const int k = std::min(n - done, capacity - off);
            for (int c = 0; c < buf.getNumChannels(); ++c) {
                if (data != nullptr) std::memcpy(buf.getWritePointer(c, off), data[c] + done, sizeof(float) * (size_t) k);
                else juce::FloatVectorOperations::clear(buf.getWritePointer(c, off), k);
            }
            done += k;
        }
    }

    void append(std::int64_t f0, const float* const* data, int n) {
        if (end >= 0 && f0 < end) {
            const auto skip = end - f0;
            if (skip >= n) return;
            const float* adv[2] = { data[0] + skip, data[1] + skip };
            append(end, adv, n - (int) skip);
            return;
        }
        if (end < 0) first = f0;
        else if (f0 > end) {
            const auto gapStart = std::max(end, f0 - (std::int64_t) capacity);
            writeFrames(gapStart, nullptr, (int) (f0 - gapStart));
        }
        if (n > capacity) {
            const float* adv[2] = { data[0] + (n - capacity), data[1] + (n - capacity) };
            writeFrames(f0 + (n - capacity), adv, capacity);
        } else {
            writeFrames(f0, data, n);
        }
        end = f0 + n;
    }
};

struct RecorderEngine::SessionTrack {
    int track = 0, sourceId = 0, numChannels = 1;
    juce::String name, baseName;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File file;
    bool realFile = true;
    int part = 1;
    std::int64_t partStartFrame = 0, writtenInPart = 0, nextFrame = 0;
    bool error = false;
};

struct RecorderEngine::Session {
    enum class State { Starting, Running } state = State::Starting;
    RecordOptions opts;
    std::vector<SessionTrack> tracks;
    std::array<int, kMaxTracks> trackIndex {};      // engine track -> index in tracks, -1
    std::int64_t F = 0;                              // first frame of the take (pre-roll included)
    std::vector<std::int64_t> splitPoints;
    std::int64_t maxPartFrames = 0, preRollFramesWanted = 0;
    SessionInfo info;
    juce::File jsonFile;
    juce::StringArray takenPaths;
    size_t markerStart = 0;
    int bits = 24;
    bool isFloat = false;
    bool rotatedThisPass = false;
    std::int64_t lastPos = -1;
    juce::uint32 lastPosChangeMs = 0;
};

class RecorderEngine::WriterThread : public juce::Thread {
public:
    explicit WriterThread(RecorderEngine& o) : juce::Thread("CrowdMike recorder writer"), owner(o) {}
    void run() override {
        while (! threadShouldExit()) {
            {
                const juce::ScopedLock sl(owner.writerLock);
                owner.processOnceLocked();
            }
            wait(10);
        }
    }
private:
    RecorderEngine& owner;
};

//==================================================================================================================
RecorderEngine::RecorderEngine() {
    for (int t = 0; t < kMaxTracks; ++t) configs[(size_t) t].name = "Track " + juce::String(t + 1);
}

RecorderEngine::~RecorderEngine() { shutdown(); }

void RecorderEngine::prepare(double sr, int maxBlock, int fifoFrames, bool startThread) {
    shutdown();
    const juce::ScopedLock sl(writerLock);
    sampleRate = sr > 0 ? sr : 48000.0;
    maxBlockSize = std::max(1, maxBlock);
    const int cap = juce::nextPowerOfTwo(std::max({ fifoFrames, 256 }));
    for (auto& f : fifos) f = std::make_unique<Fifo>(cap);
    zeros.assign((size_t) kChunk, 0.0f);
    framePosition.store(0);
    startRequested = false; startLatched = false; recActive = false;
    stopFrame.store(kNoFrame);
    totalDropouts = 0; totalErrors = 0;
    for (auto& e : trackErrors) e = false;
    for (auto& p : peaks) p = 0.0f;
    { DropoutEvent ev; while (dropoutEvents.pop(ev)) {} }
    preRollDirty = true;
    lastFlushMs = lastDiskCheckMs = juce::Time::getMillisecondCounter();
    prepared = true;
    if (startThread) {
        writerThread = std::make_unique<WriterThread>(*this);
        writerThread->startThread();
    }
}

void RecorderEngine::stopWriterThread() {
    if (writerThread != nullptr) {
        writerThread->signalThreadShouldExit();
        writerThread->notify();
        writerThread->stopThread(5000);
        writerThread.reset();
    }
}

void RecorderEngine::shutdown() {
    if (! prepared.load()) return;
    stopRecordingNow();
    stopWriterThread();
    const juce::ScopedLock sl(writerLock);
    prepared = false;
    for (auto& f : fifos) f.reset();
    for (auto& p : preRolls) p.reset();
    preRollEffective = 0.0;
}

//==== audio thread ================================================================================================
bool RecorderEngine::isTrackActive(int t) const noexcept {
    if ((unsigned) t >= (unsigned) kMaxTracks || ! armed[(size_t) t].load(std::memory_order_relaxed)) return false;
    if (preRollWanted.load(std::memory_order_relaxed) > 0) return true;
    return recActive.load(std::memory_order_acquire)
           && framePosition.load(std::memory_order_relaxed) < stopFrame.load(std::memory_order_relaxed);
}

void RecorderEngine::pushTap(int t, const float* const* channels, int numChannels, int n) noexcept {
    if ((unsigned) t >= (unsigned) kMaxTracks || n <= 0 || ! prepared.load(std::memory_order_relaxed)) return;
    auto* fp = fifos[(size_t) t].get();
    if (fp == nullptr) return;
    auto& f = *fp;
    const auto pos = framePosition.load(std::memory_order_relaxed);
    const auto w = f.writeIdx.load(std::memory_order_relaxed);
    const auto r = f.readIdx.load(std::memory_order_acquire);
    const auto freeFrames = (std::uint64_t) f.capacity - (w - r);
    const bool needHeader = f.needHeader || f.expectedNext != pos;
    if ((std::uint64_t) n > freeFrames || (needHeader && f.headers.isFull())) {
        f.dropouts.fetch_add(1, std::memory_order_relaxed);
        totalDropouts.fetch_add(1, std::memory_order_relaxed);
        dropoutEvents.push({ pos, t, n });   // if the event log is full only the counters record it
        f.needHeader = true;
        return;
    }
    if (needHeader) {
        f.headers.push({ w, pos });
        f.needHeader = false;
    }
    const int off = (int) (w & (std::uint64_t) f.mask);
    const int first = std::min(n, f.capacity - off);
    for (int c = 0; c < 2; ++c) {
        const float* src = nullptr;
        if (channels != nullptr && numChannels > 0) src = channels[std::min(c, numChannels - 1)];
        float* dst = f.ch[c].get();
        if (src != nullptr) {
            std::memcpy(dst + off, src, sizeof(float) * (size_t) first);
            if (first < n) std::memcpy(dst, src + first, sizeof(float) * (size_t) (n - first));
        } else {
            std::memset(dst + off, 0, sizeof(float) * (size_t) first);
            if (first < n) std::memset(dst, 0, sizeof(float) * (size_t) (n - first));
        }
    }
    f.writeIdx.store(w + (std::uint64_t) n, std::memory_order_release);
    f.expectedNext = pos + n;
}

void RecorderEngine::advance(int n) noexcept {
    if (n <= 0) return;
    const auto pos = framePosition.load(std::memory_order_relaxed);
    if (startRequested.load(std::memory_order_acquire) && startRequested.exchange(false, std::memory_order_acq_rel)) {
        startFrame.store(pos + n, std::memory_order_relaxed);
        recActive.store(true, std::memory_order_release);
        startLatched.store(true, std::memory_order_release);
    }
    framePosition.store(pos + n, std::memory_order_release);
}

//==== control =====================================================================================================
bool RecorderEngine::setTrack(int t, const TrackConfig& c) {
    if ((unsigned) t >= (unsigned) kMaxTracks) return false;
    {
        const juce::ScopedLock sl(configLock);
        auto& d = configs[(size_t) t];
        d = c;
        d.numChannels = juce::jlimit(1, 2, c.numChannels);
        if (d.name.trim().isEmpty()) d.name = "Track " + juce::String(t + 1);
    }
    armed[(size_t) t].store(c.armed, std::memory_order_release);
    preRollDirty = true;
    return true;
}

RecorderEngine::TrackConfig RecorderEngine::getTrack(int t) const {
    if ((unsigned) t >= (unsigned) kMaxTracks) return {};
    const juce::ScopedLock sl(configLock);
    return configs[(size_t) t];
}

void RecorderEngine::setPreRollMemoryLimit(juce::int64 bytes) {
    preRollLimitBytes.store(std::max<juce::int64>(0, bytes));
    preRollDirty = true;
}

void RecorderEngine::setPreRollSeconds(int s) {
    preRollWanted.store(juce::jlimit(0, 60, s));
    preRollDirty = true;
    if (writerThread) writerThread->notify();
}

void RecorderEngine::setError(const juce::String& e) {
    const juce::ScopedLock sl(errorLock);
    lastError = e;
}

void RecorderEngine::failTrack(SessionTrack& st, const juce::String& msg) {
    if (! st.error) {
        totalErrors.fetch_add(1);
        setError(msg);
    }
    st.error = true;
    trackErrors[(size_t) st.track] = true;
    closePart(st);
}

bool RecorderEngine::openPartFile(SessionTrack& st, juce::String& err) {
    auto& s = *session;
    auto base = st.baseName;
    if (st.part > 1) base << "_part" << juce::String(st.part).paddedLeft('0', 3);
    const auto file = FileNaming::uniqueFile(s.opts.folder, base, ".wav", s.takenPaths);
    s.takenPaths.add(file.getFullPathName());

    std::unique_ptr<juce::OutputStream> os;
    if (streamFactory) {
        os = streamFactory(file);
        st.realFile = false;
    } else {
        auto fos = std::make_unique<juce::FileOutputStream>(file);
        if (! fos->openedOk()) {
            err = "Cannot create " + file.getFullPathName() + ": " + fos->getStatus().getErrorMessage();
            return false;
        }
        fos->setPosition(0);
        fos->truncate();
        os = std::move(fos);
        st.realFile = true;
    }
    if (os == nullptr) { err = "Cannot create " + file.getFullPathName(); return false; }

    using SF = juce::AudioFormatWriterOptions::SampleFormat;
    const auto opts = juce::AudioFormatWriterOptions {}
                          .withSampleRate(sampleRate)
                          .withNumChannels(st.numChannels)
                          .withBitsPerSample(s.bits)
                          .withSampleFormat(s.isFloat ? SF::floatingPoint : SF::integral);
    juce::WavAudioFormat wav;
    st.writer = wav.createWriterFor(os, opts);
    if (st.writer == nullptr) {
        os.reset();
        if (st.realFile) file.deleteFile();
        err = "Cannot create WAV writer for " + file.getFullPathName();
        return false;
    }
    st.file = file;
    st.writtenInPart = 0;
    st.partStartFrame = st.nextFrame;
    return true;
}

void RecorderEngine::closePart(SessionTrack& st) {
    if (st.writer == nullptr) return;
    st.writer.reset();   // writes the final header and closes the stream
    if (st.writtenInPart > 0 || ! st.realFile) {
        SessionInfo::FileEntry e;
        e.fileName = st.file.getFileName();
        e.file = st.file;
        e.track = st.track;
        e.sourceId = st.sourceId;
        e.trackName = st.name;
        e.numChannels = st.numChannels;
        e.part = st.part;
        e.startFrame = st.partStartFrame - session->F;
        e.numFrames = st.writtenInPart;
        session->info.files.push_back(e);
    } else {
        st.file.deleteFile();
    }
}

std::int64_t RecorderEngine::currentPartEnd(const SessionTrack& st) const {
    const auto& s = *session;
    const auto idx = (size_t) (st.part - 1);
    if (idx < s.splitPoints.size()) return s.splitPoints[idx];
    if (s.maxPartFrames > 0) {
        const auto start = idx == 0 ? s.F : s.splitPoints[idx - 1];
        return start + s.maxPartFrames;
    }
    return kNoFrame;
}

void RecorderEngine::rotate(SessionTrack& st) {
    auto& s = *session;
    const auto idx = (size_t) (st.part - 1);
    if (idx == s.splitPoints.size()) s.splitPoints.push_back(st.nextFrame);   // automatic split: first track registers it
    closePart(st);
    ++st.part;
    juce::String err;
    if (openPartFile(st, err)) {
        if (st.error) { st.error = false; trackErrors[(size_t) st.track] = false; }   // recovered
    } else {
        st.error = false;   // report the new failure
        failTrack(st, err);
    }
    s.rotatedThisPass = true;
}

void RecorderEngine::writeSpan(SessionTrack& st, const float* const* data, int n) {
    int done = 0;
    while (done < n) {
        const auto pe = currentPartEnd(st);
        if (st.nextFrame >= pe) { rotate(st); continue; }
        const int k = (int) std::min<std::int64_t>(n - done, pe - st.nextFrame);
        if (st.writer != nullptr && ! st.error) {
            const float* p[2];
            for (int c = 0; c < 2; ++c) p[c] = data != nullptr ? data[c] + done : zeros.data();
            if (! st.writer->writeFromFloatArrays(p, st.numChannels, k)) {
                failTrack(st, "Write failed for " + st.file.getFullPathName() + " (disk full or removed?)");
            } else {
                st.writtenInPart += k;
                if (data != nullptr) {
                    float pk = 0.0f;
                    for (int c = 0; c < st.numChannels; ++c)
                        pk = std::max(pk, juce::FloatVectorOperations::findMaximum(p[c], k));
                    for (int c = 0; c < st.numChannels; ++c)
                        pk = std::max(pk, -juce::FloatVectorOperations::findMinimum(p[c], k));
                    auto& a = peaks[(size_t) st.track];
                    if (pk > a.load(std::memory_order_relaxed)) a.store(pk, std::memory_order_relaxed);
                }
            }
        }
        st.nextFrame += k;
        done += k;
    }
}

void RecorderEngine::writeTimeline(SessionTrack& st, std::int64_t frame, const float* const* data, int n) {
    const auto stop = stopFrame.load(std::memory_order_acquire);
    const auto end = std::min(frame + n, stop);
    if (end <= st.nextFrame) return;
    while (st.nextFrame < std::min(frame, end))   // gap -> silence
        writeSpan(st, nullptr, (int) std::min<std::int64_t>(kChunk, std::min(frame, end) - st.nextFrame));
    const auto from = std::max(frame, st.nextFrame);
    if (from >= end) return;
    const auto off = from - frame;
    const float* p[2] = { data[0] + off, data[1] + off };
    writeSpan(st, p, (int) (end - from));
}

void RecorderEngine::handleChunk(int t, std::int64_t frame, const float* const* data, int n) {
    if (auto* pr = preRolls[(size_t) t].get()) pr->append(frame, data, n);
    if (session != nullptr && session->state == Session::State::Running) {
        const int i = session->trackIndex[(size_t) t];
        if (i >= 0) writeTimeline(session->tracks[(size_t) i], frame, data, n);
    }
}

void RecorderEngine::drainTrack(int t, std::int64_t) {
    auto* fp = fifos[(size_t) t].get();
    if (fp == nullptr) return;
    auto& f = *fp;
    const auto W = f.writeIdx.load(std::memory_order_acquire);
    auto R = f.readIdx.load(std::memory_order_relaxed);
    if (R == W) return;
    // While waiting for the start latch keep un-buffered data in the FIFO (it may belong to the take).
    if (session != nullptr && session->state == Session::State::Starting && preRolls[(size_t) t] == nullptr
        && session->trackIndex[(size_t) t] >= 0)
        return;
    while (R < W) {
        Fifo::Header h {};
        while (f.headers.peek(h) && h.index <= R) {
            f.segIndex = h.index;
            f.segFrame = h.frame;
            f.headers.pop(h);
        }
        auto end = W;
        if (f.headers.peek(h) && h.index < end) end = h.index;
        while (R < end) {
            const int off = (int) (R & (std::uint64_t) f.mask);
            const int n = (int) std::min<std::uint64_t>({ end - R, (std::uint64_t) (f.capacity - off), (std::uint64_t) kChunk });
            const float* p[2] = { f.ch[0].get() + off, f.ch[1].get() + off };
            handleChunk(t, f.segFrame + (std::int64_t) (R - f.segIndex), p, n);
            R += (std::uint64_t) n;
            f.readIdx.store(R, std::memory_order_release);
        }
    }
}

void RecorderEngine::reallocatePreRollLocked() {
    const int secs = preRollWanted.load();
    std::array<int, kMaxTracks> ch {};
    std::int64_t totalCh = 0;
    {
        const juce::ScopedLock sl(configLock);
        for (int t = 0; t < kMaxTracks; ++t)
            if (armed[(size_t) t].load()) { ch[(size_t) t] = configs[(size_t) t].numChannels; totalCh += ch[(size_t) t]; }
    }
    // The ring also covers data still waiting in the FIFO when the take starts (slack), so the full pre-roll is
    // available even if the writer lags behind the audio thread.
    const std::int64_t slack = (fifos[0] != nullptr ? fifos[0]->capacity : 0) + 2 * (std::int64_t) maxBlockSize;
    std::int64_t frames = (std::int64_t) std::llround(secs * sampleRate);
    std::int64_t capacity = frames + slack;
    bool clipped = false;
    const auto limit = preRollLimitBytes.load();
    if (secs > 0 && totalCh > 0 && capacity * totalCh * (std::int64_t) sizeof(float) > limit) {
        capacity = limit / (totalCh * (std::int64_t) sizeof(float));
        frames = std::max<std::int64_t>(0, capacity - slack);
        clipped = true;
    }
    for (int t = 0; t < kMaxTracks; ++t) {
        auto& p = preRolls[(size_t) t];
        if (secs <= 0 || ch[(size_t) t] == 0 || frames <= 0) { p.reset(); continue; }
        if (p == nullptr || p->capacity != (int) capacity || p->buf.getNumChannels() != ch[(size_t) t]) {
            p.reset();
            try { p = std::make_unique<PreRoll>(ch[(size_t) t], (int) capacity); }
            catch (const std::bad_alloc&) { p.reset(); clipped = true; setError("Out of memory for pre-roll"); }
        }
    }
    preRollEffective = secs > 0 ? (double) frames / sampleRate : 0.0;
    preRollClipped = clipped;
    if (clipped) setError("Pre-roll limited to " + juce::String(preRollEffective.load(), 1) + " s (memory cap)");
}

void RecorderEngine::beginTakeLocked() {
    auto& s = *session;
    const auto S = startFrame.load(std::memory_order_acquire);
    std::int64_t F = S;
    if (s.preRollFramesWanted > 0) {
        std::int64_t earliest = kNoFrame;
        for (auto& st : s.tracks)
            if (auto* pr = preRolls[(size_t) st.track].get(); pr != nullptr && pr->end >= 0)
                earliest = std::min(earliest, pr->validStart());
        if (earliest != kNoFrame) F = std::min(S, std::max(S - s.preRollFramesWanted, earliest));
    }
    F = std::max<std::int64_t>(F, 0);
    s.F = F;
    s.info.startFrame = F;
    s.info.preRollFrames = S - F;
    s.state = Session::State::Running;
    for (auto& st : s.tracks) {
        st.nextFrame = F;
        st.partStartFrame = F;
        auto* pr = preRolls[(size_t) st.track].get();
        if (pr == nullptr || pr->end < 0) continue;
        for (auto f = std::max(F, pr->validStart()); f < pr->end;) {
            const int off = (int) (f % pr->capacity);
            const int n = (int) std::min<std::int64_t>({ pr->end - f, (std::int64_t) (pr->capacity - off), (std::int64_t) kChunk });
            const float* p[2] = { pr->buf.getReadPointer(0, off), pr->buf.getReadPointer(pr->buf.getNumChannels() - 1, off) };
            writeTimeline(st, f, p, n);
            f += n;
        }
    }
}

void RecorderEngine::writeSessionJsonLocked() {
    auto& s = *session;
    auto info = s.info;
    std::int64_t maxNext = s.F;
    for (auto& st : s.tracks) {
        maxNext = std::max(maxNext, st.nextFrame);
        if (st.writer != nullptr) {   // still-open parts (mid-take snapshot)
            SessionInfo::FileEntry e;
            e.fileName = st.file.getFileName();
            e.file = st.file;
            e.track = st.track; e.sourceId = st.sourceId; e.trackName = st.name;
            e.numChannels = st.numChannels; e.part = st.part;
            e.startFrame = st.partStartFrame - s.F;
            e.numFrames = st.writtenInPart;
            info.files.push_back(e);
        }
    }
    std::sort(info.files.begin(), info.files.end(),
              [](auto& a, auto& b) { return a.track != b.track ? a.track < b.track : a.part < b.part; });
    info.totalFrames = s.state == Session::State::Running ? maxNext - s.F : 0;
    for (auto& d : info.dropouts) d.frame = std::max<std::int64_t>(0, d.frame - s.F);
    {
        const juce::ScopedLock sl(markerLock);
        for (size_t i = s.markerStart; i < markers.size(); ++i) {
            auto m = markers[i];
            m.frame = std::max<std::int64_t>(0, m.frame - s.F);
            m.seconds = (double) m.frame / sampleRate;
            info.markers.push_back(m);
        }
    }
    juce::String err;
    if (! SessionMetadata::write(s.jsonFile, info, err)) { totalErrors.fetch_add(1); setError(err); }
    lastSessionFile = s.jsonFile;
}

void RecorderEngine::finaliseTakeLocked() {
    if (session == nullptr) return;
    for (auto& st : session->tracks) closePart(st);
    writeSessionJsonLocked();
    recActive.store(false, std::memory_order_release);
    stopFrame.store(kNoFrame, std::memory_order_release);
    startLatched = false;
    session.reset();
    sessionOpen = false;
    postRollRunning = false;
}

void RecorderEngine::processOnceLocked() {
    if (! prepared.load()) return;
    const auto pos = framePosition.load(std::memory_order_acquire);
    const auto now = juce::Time::getMillisecondCounter();

    if (preRollDirty.load() && session == nullptr) {
        preRollDirty = false;
        reallocatePreRollLocked();
    }
    {
        DropoutEvent ev;
        while (dropoutEvents.pop(ev))
            if (session != nullptr && (int) session->info.dropouts.size() < kMaxDropoutEvents)
                session->info.dropouts.push_back(ev);
    }
    if (session != nullptr && session->state == Session::State::Starting && startLatched.exchange(false))
        beginTakeLocked();
    if (session != nullptr) session->rotatedThisPass = false;

    for (int t = 0; t < kMaxTracks; ++t) drainTrack(t, pos);

    if (session != nullptr && session->state == Session::State::Running) {
        auto& s = *session;
        auto stop = stopFrame.load(std::memory_order_acquire);
        // Every block before `pos` has been pushed (or dropped): fill what is missing with silence.
        const auto target = std::min(pos, stop);
        for (auto& st : s.tracks)
            while (st.nextFrame < target)
                writeSpan(st, nullptr, (int) std::min<std::int64_t>(kChunk, target - st.nextFrame));

        std::int64_t maxNext = s.F;
        for (auto& st : s.tracks) maxNext = std::max(maxNext, st.nextFrame);
        statusFrames.store(maxNext - s.F);

        if (stop != kNoFrame) {
            if (pos != s.lastPos) { s.lastPos = pos; s.lastPosChangeMs = now; }
            if (now - s.lastPosChangeMs > 1500 && pos < stop) {   // audio stopped during post-roll: end now
                stop = std::max(maxNext, std::min(pos, stop));
                stopFrame.store(stop);
                for (auto& st : s.tracks)
                    while (st.nextFrame < stop) writeSpan(st, nullptr, (int) std::min<std::int64_t>(kChunk, stop - st.nextFrame));
            }
            bool done = true;
            for (auto& st : s.tracks) done = done && st.nextFrame >= stop;
            if (done) { finaliseTakeLocked(); return; }
        }
        if (s.rotatedThisPass) writeSessionJsonLocked();

        if (now - lastFlushMs >= 1000) {
            lastFlushMs = now;
            for (auto& st : s.tracks) {
                if (st.writer == nullptr) continue;
                if (st.realFile && ! st.file.existsAsFile()) { failTrack(st, "Destination removed: " + st.file.getFullPathName()); continue; }
                st.writer->flush();
            }
        }
        if (now - lastDiskCheckMs >= 2000) {
            lastDiskCheckMs = now;
            freeDiskGB = s.opts.folder.isDirectory() ? (double) s.opts.folder.getBytesFreeOnVolume() / 1.0e9 : 0.0;
        }
    }
}

bool RecorderEngine::startRecording(const RecordOptions& o) {
    const juce::ScopedLock sl(writerLock);
    auto fail = [this](const juce::String& e) { totalErrors.fetch_add(1); setError(e); return false; };
    if (! prepared.load()) return fail("Recorder not prepared");
    if (session != nullptr) return fail("Already recording");

    std::vector<std::pair<int, TrackConfig>> armedTracks;
    {
        const juce::ScopedLock cl(configLock);
        for (int t = 0; t < kMaxTracks; ++t)
            if (armed[(size_t) t].load()) armedTracks.emplace_back(t, configs[(size_t) t]);
    }
    if (armedTracks.empty()) return fail("No tracks armed");
    if (o.folder == juce::File()) return fail("No record folder set");
    if (o.folder.existsAsFile()) return fail("Record destination is a file: " + o.folder.getFullPathName());
    if (! o.folder.isDirectory()) {
        const auto r = o.folder.createDirectory();
        if (r.failed() || ! o.folder.isDirectory())
            return fail("Cannot create record folder " + o.folder.getFullPathName() + ": " + r.getErrorMessage());
    }

    session = std::make_unique<Session>();
    auto& s = *session;
    s.opts = o;
    s.trackIndex.fill(-1);
    s.bits = o.format == RecordOptions::Wav16 ? 16 : o.format == RecordOptions::Wav24 ? 24 : 32;
    s.isFloat = o.format == RecordOptions::Wav32f;
    const auto nowTime = juce::Time::getCurrentTime();
    const int take = takeCounter + 1;
    NameTokens tok { nowTime, o.scene, {}, o.show, o.venue, take };
    const auto pattern = o.namePattern.trim().isEmpty() ? juce::String("{date}_{time}_{scene}_{source}") : o.namePattern;

    s.info.appVersion = o.appVersion;
    s.info.startTimeIso = nowTime.toISO8601(true);
    s.info.sampleRate = sampleRate;
    s.info.take = take;
    s.info.scene = o.scene; s.info.show = o.show; s.info.venue = o.venue;
    s.info.fileFormat = s.isFloat ? "wav32f" : (s.bits == 16 ? "wav16" : "wav24");
    s.info.routeSnapshot = o.routeSnapshot;

    int maxCh = 1;
    for (auto& [t, cfg] : armedTracks) {
        SessionTrack st;
        st.track = t; st.sourceId = cfg.sourceId; st.numChannels = cfg.numChannels; st.name = cfg.name;
        tok.source = cfg.name;
        st.baseName = FileNaming::expandPattern(pattern, tok);
        maxCh = std::max(maxCh, st.numChannels);
        juce::String err;
        if (! openPartFile(st, err)) {
            for (auto& other : s.tracks) { other.writer.reset(); if (other.realFile) other.file.deleteFile(); }
            session.reset();
            return fail(err);
        }
        s.trackIndex[(size_t) t] = (int) s.tracks.size();
        s.info.tracks.push_back({ t, cfg.sourceId, cfg.name, cfg.numChannels });
        s.tracks.push_back(std::move(st));
    }
    tok.source = {};
    s.jsonFile = FileNaming::uniqueFile(o.folder, FileNaming::expandPattern(pattern, tok) + "_session", ".json", s.takenPaths);

    std::int64_t partFrames = 0;
    if (o.maxFileSeconds > 0.0) partFrames = (std::int64_t) std::llround(o.maxFileSeconds * sampleRate);
    if (o.maxFileBytes > 0) {
        const auto bytesPerFrame = (std::int64_t) maxCh * s.bits / 8;
        const auto f = std::max<std::int64_t>(0, o.maxFileBytes - 4096) / bytesPerFrame;
        partFrames = partFrames > 0 ? std::min(partFrames, f) : f;
    }
    if (o.maxFileSeconds > 0.0 || o.maxFileBytes > 0) partFrames = std::max<std::int64_t>(partFrames, 1024);
    s.maxPartFrames = partFrames;
    s.preRollFramesWanted = (std::int64_t) std::llround(std::min((double) juce::jlimit(0, 60, o.preRollSeconds),
                                                                  preRollEffective.load()) * sampleRate);
    {
        const juce::ScopedLock ml(markerLock);
        s.markerStart = markers.size();
        markerScene = o.scene;
    }
    for (auto& e : trackErrors) e = false;
    takeCounter = take;
    currentTake = take;
    freeDiskGB = (double) o.folder.getBytesFreeOnVolume() / 1.0e9;
    sessionOpen = true;
    postRollRunning = false;
    statusFrames = 0;
    startLatched = false;
    stopFrame.store(kNoFrame);
    startRequested.store(true, std::memory_order_release);
    if (writerThread) writerThread->notify();
    return true;
}

void RecorderEngine::stopRecording() {
    const juce::ScopedLock sl(writerLock);
    if (session == nullptr || stopFrame.load() != kNoFrame) return;
    if (session->state == Session::State::Starting && startRequested.exchange(false)) {   // never started: discard
        for (auto& st : session->tracks) { st.writer.reset(); if (st.realFile) st.file.deleteFile(); }
        session.reset();
        sessionOpen = false;
        return;
    }
    const auto pos = framePosition.load(std::memory_order_acquire);
    const auto post = (std::int64_t) std::llround(juce::jlimit(0, 30, session->opts.postRollSeconds) * sampleRate);
    stopFrame.store(std::max(pos + post, startFrame.load()), std::memory_order_release);
    postRollRunning = post > 0;
    session->lastPos = pos;
    session->lastPosChangeMs = juce::Time::getMillisecondCounter();
    if (writerThread) writerThread->notify();
}

void RecorderEngine::stopRecordingNow() {
    const juce::ScopedLock sl(writerLock);
    if (session == nullptr) return;
    if (session->state == Session::State::Starting && startRequested.exchange(false)) {
        for (auto& st : session->tracks) { st.writer.reset(); if (st.realFile) st.file.deleteFile(); }
        session.reset();
        sessionOpen = false;
        return;
    }
    if (session->state == Session::State::Starting) {   // latched but not yet seen by the writer
        startLatched = false;
        beginTakeLocked();
    }
    processOnceLocked();   // drain what has been pushed
    if (session == nullptr) return;
    std::int64_t stop = std::max(framePosition.load(), startFrame.load());
    for (auto& st : session->tracks) stop = std::max(stop, st.nextFrame);
    stop = std::min(stop, stopFrame.load());
    stopFrame.store(stop);
    for (auto& st : session->tracks)
        while (st.nextFrame < stop) writeSpan(st, nullptr, (int) std::min<std::int64_t>(kChunk, stop - st.nextFrame));
    finaliseTakeLocked();
}

void RecorderEngine::splitFile() {
    const juce::ScopedLock sl(writerLock);
    if (session == nullptr || session->state != Session::State::Running) return;
    auto& s = *session;
    std::int64_t p = s.F;
    for (auto& st : s.tracks) p = std::max(p, st.nextFrame);
    if (p > s.F && (s.splitPoints.empty() || p > s.splitPoints.back()) && p < stopFrame.load())
        s.splitPoints.push_back(p);
}

void RecorderEngine::addMarker(const juce::String& label, MarkerType type) {
    Marker m;
    m.frame = framePosition.load(std::memory_order_acquire);
    m.label = label;
    m.type = type;
    m.wallClock = juce::Time::getCurrentTime();
    const juce::ScopedLock sl(markerLock);
    m.scene = markerScene;
    if ((int) markers.size() < kMaxMarkers) markers.push_back(m);
}

std::vector<Marker> RecorderEngine::getMarkers() const {
    const juce::ScopedLock sl(markerLock);
    return markers;
}

void RecorderEngine::clearMarkers() {
    const juce::ScopedLock wl(writerLock);
    const juce::ScopedLock sl(markerLock);
    markers.clear();
    if (session != nullptr) session->markerStart = 0;
}

RecorderEngine::Status RecorderEngine::getStatus() const {
    Status s;
    s.recording = sessionOpen.load();
    s.postRoll = s.recording && postRollRunning.load();
    s.framesWritten = statusFrames.load();
    s.elapsedSeconds = (double) s.framesWritten / sampleRate;
    s.dropouts = totalDropouts.load();
    s.errors = totalErrors.load();
    s.freeDiskGB = freeDiskGB.load();
    for (auto& f : fifos)
        if (f != nullptr)
            s.fifoFill = std::max(s.fifoFill, (double) (f->writeIdx.load() - f->readIdx.load()) / f->capacity);
    s.take = currentTake.load();
    s.preRollSeconds = preRollEffective.load();
    s.preRollClipped = preRollClipped.load();
    const juce::ScopedLock sl(errorLock);
    s.lastError = lastError;
    return s;
}

float RecorderEngine::getTrackPeak(int t) {
    if ((unsigned) t >= (unsigned) kMaxTracks) return 0.0f;
    return peaks[(size_t) t].exchange(0.0f);
}

bool RecorderEngine::hasTrackError(int t) const {
    return (unsigned) t < (unsigned) kMaxTracks && trackErrors[(size_t) t].load();
}

int RecorderEngine::getTrackDropouts(int t) const {
    if ((unsigned) t >= (unsigned) kMaxTracks || fifos[(size_t) t] == nullptr) return 0;
    return fifos[(size_t) t]->dropouts.load();
}

juce::File RecorderEngine::getLastSessionFile() const {
    const juce::ScopedLock sl(writerLock);
    return session != nullptr ? session->jsonFile : lastSessionFile;
}

void RecorderEngine::drainForTesting() {
    const juce::ScopedLock sl(writerLock);
    processOnceLocked();
}

void RecorderEngine::setOutputStreamFactoryForTesting(OutputStreamFactory f) {
    const juce::ScopedLock sl(writerLock);
    streamFactory = std::move(f);
}

std::size_t RecorderEngine::getAllocatedBytes() const {
    const juce::ScopedLock sl(writerLock);
    std::size_t b = 0;
    for (auto& f : fifos)
        if (f != nullptr) b += (size_t) f->capacity * 2 * sizeof(float);
    for (auto& p : preRolls)
        if (p != nullptr) b += (size_t) p->capacity * (size_t) p->buf.getNumChannels() * sizeof(float);
    return b;
}

} // namespace crowdmike::rec
