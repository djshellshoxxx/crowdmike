#include "RecTestUtil.h"
#include "Recording/RecorderEngine.h"
#include "Recording/FileNaming.h"
#include <thread>

using namespace crowdmike::rec;
using rectest::ramp;

namespace {

/** Simulates the engine: per block, push every active track, then advance. */
struct Driver {
    RecorderEngine& rec;
    int block;
    std::vector<float> data[2];
    std::vector<std::pair<std::int64_t, int>> dropped;   // (frame, track) of blocks that were dropped
    std::function<float(int, std::int64_t, int)> signal = [](int t, std::int64_t f, int c) { return ramp(t, f, c); };
    double maxPushMs = 0.0;

    Driver(RecorderEngine& r, int b) : rec(r), block(b) { data[0].resize((size_t) b); data[1].resize((size_t) b); }

    void runBlock() {
        const auto pos = rec.getFramePosition();
        for (int t = 0; t < RecorderEngine::kMaxTracks; ++t) {
            if (! rec.isTrackActive(t)) continue;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i) data[c][(size_t) i] = signal(t, pos + i, c);
            const float* p[2] = { data[0].data(), data[1].data() };
            const int before = rec.getTrackDropouts(t);
            const auto t0 = rectest::nowMs();
            rec.pushTap(t, p, 2, block);
            maxPushMs = std::max(maxPushMs, rectest::nowMs() - t0);
            if (rec.getTrackDropouts(t) != before) dropped.emplace_back(pos, t);
        }
        rec.advance(block);
    }
    void run(int blocks, int drainEvery = 8) {
        for (int b = 0; b < blocks; ++b) {
            runBlock();
            if (drainEvery > 0 && b % drainEvery == drainEvery - 1) rec.drainForTesting();
        }
    }
    bool wasDropped(int t, std::int64_t frame) const {
        for (auto& [f, tr] : dropped)
            if (tr == t && frame >= f && frame < f + block) return true;
        return false;
    }
};

void armTracks(RecorderEngine& rec, int n, int stereoFrom = 1000, const juce::String& prefix = "Track") {
    for (int t = 0; t < n; ++t)
        rec.setTrack(t, { 100 + t, t >= stereoFrom ? 2 : 1, prefix + juce::String(t + 1), true });
}

/** Concatenates all parts of each track, checks the frame count and compares with the expected signal. */
void verifyTake(const SessionInfo& info, int numTracks, double tol, const Driver* drv = nullptr, int expectParts = -1) {
    CM_EXPECT((int) info.tracks.size() == numTracks);
    for (auto& tr : info.tracks) {
        const auto files = info.filesForTrack(tr.track);
        if (expectParts >= 0) CM_EXPECT((int) files.size() == expectParts);
        std::int64_t frame = 0;
        double maxErr = 0.0;
        for (auto& fe : files) {
            juce::AudioBuffer<float> b;
            CM_EXPECT(rectest::readWav(fe.file, b));
            CM_EXPECT(b.getNumChannels() == tr.numChannels);
            CM_EXPECT(fe.startFrame == frame);
            CM_EXPECT(fe.numFrames == b.getNumSamples());
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < b.getNumSamples(); ++i) {
                    const auto abs = info.startFrame + frame + i;
                    const float expect = (drv != nullptr && drv->wasDropped(tr.track, abs)) ? 0.0f : ramp(tr.track, abs, c);
                    maxErr = std::max(maxErr, (double) std::abs(b.getSample(c, i) - expect));
                }
            frame += b.getNumSamples();
        }
        CM_EXPECT(frame == info.totalFrames);
        CM_EXPECT_NEAR(maxErr, 0.0, tol);
    }
}

RecorderEngine::RecordOptions opts(const juce::File& folder, RecorderEngine::RecordOptions::Format fmt) {
    RecorderEngine::RecordOptions o;
    o.folder = folder;
    o.format = fmt;
    o.scene = "Scene";
    o.appVersion = "test-1.0";
    return o;
}

} // namespace

CM_TEST(Recorder_Multitrack8_32f_BitExact) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    armTracks(rec, 8, 6);
    Driver d(rec, 128);
    CM_EXPECT(rec.startRecording(opts(tmp.dir, RecorderEngine::RecordOptions::Wav32f)));
    CM_EXPECT(rec.getStatus().recording);
    d.run(200);
    rec.stopRecordingNow();
    CM_EXPECT(! rec.getStatus().recording);
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.startFrame == 128);               // latched on the block boundary after startRecording()
    CM_EXPECT(info.totalFrames == 200 * 128 - 128);  // exact frame count
    CM_EXPECT(info.files.size() == 8);
    CM_EXPECT(info.appVersion == "test-1.0");
    CM_EXPECT_NEAR(info.sampleRate, 48000.0, 0.0);
    verifyTake(info, 8, 0.0, nullptr, 1);
    CM_EXPECT(rec.getStatus().dropouts == 0);
    CM_EXPECT(rec.getStatus().errors == 0);
}

CM_TEST(Recorder_Quantisation16And24) {
    for (auto fmt : { RecorderEngine::RecordOptions::Wav16, RecorderEngine::RecordOptions::Wav24 }) {
        rectest::TempDir tmp;
        RecorderEngine rec;
        rec.prepare(44100.0, 64, RecorderEngine::kDefaultFifoFrames, false);
        armTracks(rec, 2, 1);
        Driver d(rec, 64);
        CM_EXPECT(rec.startRecording(opts(tmp.dir, fmt)));
        d.run(300);
        rec.stopRecordingNow();
        SessionInfo info;
        CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
        CM_EXPECT(info.totalFrames == 299 * 64);
        const double lsb = fmt == RecorderEngine::RecordOptions::Wav16 ? 1.0 / 32767.0 : 1.0 / 8388607.0;
        verifyTake(info, 2, 1.01 * lsb);
    }
}

CM_TEST(Recorder_PushNeverBlocksWhenWriterStalled) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, 1024, false);   // tiny FIFO, writer "stalled" (no thread, no drain)
    armTracks(rec, 2);
    Driver d(rec, 128);
    CM_EXPECT(rec.startRecording(opts(tmp.dir, RecorderEngine::RecordOptions::Wav32f)));
    d.run(1, 0);      // latch
    d.run(40, 0);     // 8 blocks fit, the rest is dropped
    CM_EXPECT(rec.getStatus().dropouts > 0);
    CM_EXPECT(rec.getTrackDropouts(0) == 32 && rec.getTrackDropouts(1) == 32);
    CM_EXPECT(rec.getStatus().fifoFill >= 0.99);
    CM_EXPECT(d.maxPushMs < 5.0);
    rec.drainForTesting();   // writer recovers
    d.run(40, 4);
    rec.stopRecordingNow();
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.totalFrames == 80 * 128);   // dropped blocks are kept as silence: timeline intact
    CM_EXPECT(! info.dropouts.empty());
    CM_EXPECT(info.dropouts.front().frame == 8 * 128);   // approximate location, relative to the take
    verifyTake(info, 2, 0.0, &d);
}

CM_TEST(Recorder_LongRecordingSplitByDuration_RealThread) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128);   // real writer thread
    armTracks(rec, 2, 1);
    Driver d(rec, 128);
    auto o = opts(tmp.dir, RecorderEngine::RecordOptions::Wav32f);
    o.maxFileSeconds = 7.3;
    CM_EXPECT(rec.startRecording(o));
    const int blocks = 60 * 48000 / 128;   // 60 s, compressed time
    for (int b = 0; b < blocks; ++b) {
        d.runBlock();
        while (rec.getStatus().fifoFill > 0.5) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CM_EXPECT(rec.getStatus().dropouts == 0);
    rec.stopRecording();   // no post-roll: the writer finalises asynchronously
    for (int i = 0; i < 1000 && rec.getStatus().recording; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CM_EXPECT(! rec.getStatus().recording);
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.totalFrames == (std::int64_t) blocks * 128 - 128);
    const auto partFrames = (std::int64_t) std::llround(7.3 * 48000.0);
    const int parts = (int) ((info.totalFrames + partFrames - 1) / partFrames);
    for (auto& f : info.files)
        if (f.part < parts) CM_EXPECT(f.numFrames == partFrames);
    verifyTake(info, 2, 0.0, nullptr, parts);   // concatenated parts == original, no lost frames
    rec.shutdown();
}

CM_TEST(Recorder_ManualSplitAndMaxBytes) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 100, RecorderEngine::kDefaultFifoFrames, false);
    armTracks(rec, 3, 2);
    Driver d(rec, 100);
    auto o = opts(tmp.dir, RecorderEngine::RecordOptions::Wav16);
    o.maxFileBytes = 4096 + 2 * 2 * 30000;   // ~30000 stereo 16-bit frames per file
    CM_EXPECT(rec.startRecording(o));
    d.run(250, 7);
    rec.splitFile();
    d.run(333, 5);
    rec.splitFile();
    rec.splitFile();   // second split at the same point is ignored
    d.run(10, 3);
    rec.stopRecordingNow();
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.totalFrames == 592 * 100);
    for (auto& f : info.files) CM_EXPECT(f.numFrames <= 30000);
    verifyTake(info, 3, 1.01 / 32767.0);
    CM_EXPECT(info.filesForTrack(0).size() == info.filesForTrack(2).size());
}

CM_TEST(Recorder_PreRollIncludesHistory) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 256, RecorderEngine::kDefaultFifoFrames, false);
    armTracks(rec, 2, 1);
    rec.setPreRollSeconds(2);
    rec.drainForTesting();   // writer allocates the rings
    CM_EXPECT_NEAR(rec.getStatus().preRollSeconds, 2.0, 1e-9);
    CM_EXPECT(rec.isTrackActive(0));   // armed + pre-roll => taps flow while not recording
    Driver d(rec, 256);
    d.run(3 * 48000 / 256, 4);   // 3 s of history
    auto o = opts(tmp.dir, RecorderEngine::RecordOptions::Wav32f);
    o.preRollSeconds = 2;
    CM_EXPECT(rec.startRecording(o));
    d.run(1);   // latch: S = start of the next block
    const auto S = rec.getFramePosition();
    d.run(48000 / 256, 3);
    rec.stopRecordingNow();
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.preRollFrames == 2 * 48000);
    CM_EXPECT(info.startFrame == S - 2 * 48000);
    verifyTake(info, 2, 0.0);
    // Pre-roll still running after the take; memory bounded across sessions.
    const auto bytes = rec.getAllocatedBytes();
    for (int i = 0; i < 5; ++i) {
        CM_EXPECT(rec.startRecording(o));
        d.run(20, 4);
        rec.stopRecordingNow();
    }
    CM_EXPECT(rec.getAllocatedBytes() == bytes);
}

CM_TEST(Recorder_PostRoll) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 480, RecorderEngine::kDefaultFifoFrames, false);
    armTracks(rec, 1);
    Driver d(rec, 480);
    auto o = opts(tmp.dir, RecorderEngine::RecordOptions::Wav32f);
    o.postRollSeconds = 1;
    CM_EXPECT(rec.startRecording(o));
    d.run(100, 4);   // 1 s
    const auto stopPos = rec.getFramePosition();
    rec.stopRecording();
    rec.drainForTesting();
    CM_EXPECT(rec.getStatus().recording && rec.getStatus().postRoll);
    CM_EXPECT(rec.isTrackActive(0));
    d.run(50, 4);   // 0.5 s: still in post-roll
    CM_EXPECT(rec.getStatus().recording);
    d.run(60, 4);   // past the post-roll
    rec.drainForTesting();
    CM_EXPECT(! rec.getStatus().recording);
    CM_EXPECT(! rec.isTrackActive(0));
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.totalFrames == stopPos + 48000 - 480);
    verifyTake(info, 1, 0.0);
}

CM_TEST(Recorder_MarkersAndSidecarUnicodeRoundTrip) {
    rectest::TempDir tmpRoot("uni");
    struct { juce::File dir; } tmp { tmpRoot.dir.getChildFile(juce::String::fromUTF8("Aufn\xc3\xa4hme \xe6\x97\xa5")) };
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    const juce::String uni = juce::String::fromUTF8("Crowd \xc3\x9c \xe6\x97\xa5\xe6\x9c\xac \xe2\x9c\x93");   // "Crowd Ü 日本 ✓"
    rec.setTrack(0, { 7, 2, uni, true });
    rec.setTrack(3, { 9, 1, "Ambience:L/R?", true });
    Driver d(rec, 128);
    auto o = opts(tmp.dir, RecorderEngine::RecordOptions::Wav24);
    o.scene = juce::String::fromUTF8("Hauptb\xc3\xbchne");   // "Hauptbühne"
    o.show = "Show"; o.venue = "Arena";
    auto* route = new juce::DynamicObject();
    route->setProperty("in1", "G1");
    o.routeSnapshot = juce::var(route);
    CM_EXPECT(rec.startRecording(o));
    d.run(50);
    const auto markerPos = rec.getFramePosition();
    rec.addMarker(juce::String::fromUTF8("Gro\xc3\x9f" "er Jubel"), MarkerType::BigCheer);
    d.run(20);
    rec.addMarker("Feedback 2k", MarkerType::Problem);
    d.run(10);
    rec.stopRecordingNow();
    CM_EXPECT(rec.getMarkers().size() == 2);

    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.scene == o.scene);
    CM_EXPECT(info.show == "Show" && info.venue == "Arena");
    CM_EXPECT(info.routeSnapshot["in1"].toString() == "G1");
    CM_EXPECT(info.startTimeIso.isNotEmpty() && juce::Time::fromISO8601(info.startTimeIso).toMilliseconds() > 0);
    CM_EXPECT(info.markers.size() == 2);
    if (info.markers.size() == 2) {
        CM_EXPECT(info.markers[0].label == juce::String::fromUTF8("Gro\xc3\x9f" "er Jubel"));
        CM_EXPECT(info.markers[0].type == MarkerType::BigCheer);
        CM_EXPECT(info.markers[0].scene == o.scene);
        CM_EXPECT(info.markers[0].frame == markerPos - info.startFrame);
        CM_EXPECT_NEAR(info.markers[0].seconds, (double) (markerPos - info.startFrame) / 48000.0, 1e-9);
        CM_EXPECT(info.markers[1].type == MarkerType::Problem);
    }
    CM_EXPECT(info.tracks.size() == 2);
    bool foundUni = false;
    for (auto& f : info.files) {
        CM_EXPECT(f.file.existsAsFile());
        if (f.sourceId == 7) { foundUni = f.fileName.contains(juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac")) && f.trackName == uni; }
        CM_EXPECT(! f.fileName.containsAnyOf(":/?\\"));
    }
    CM_EXPECT(foundUni);
    verifyTake(info, 2, 1.01 / 8388607.0);
    // Second take: same names -> numeric suffix, take number increments.
    o.namePattern = "{scene}_{source}";
    CM_EXPECT(rec.startRecording(o));
    d.run(5);
    rec.stopRecordingNow();
    CM_EXPECT(rec.getStatus().take == 2);
    CM_EXPECT(rec.startRecording(o));
    d.run(5);
    rec.stopRecordingNow();
    SessionInfo info3;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info3));
    CM_EXPECT(info3.take == 3);
    for (auto& f : info3.files) CM_EXPECT(f.fileName.contains("_2.wav"));
}

CM_TEST(Recorder_FileNameSanitisation) {
    CM_EXPECT(FileNaming::sanitise("CON") == "_CON");
    CM_EXPECT(FileNaming::sanitise("con.txt") == "_con.txt");
    CM_EXPECT(FileNaming::sanitise("LPT9") == "_LPT9");
    CM_EXPECT(FileNaming::sanitise("COM10") == "COM10");
    CM_EXPECT(FileNaming::sanitise("a<b>c:d\"e/f\\g|h?i*j") == "a_b_c_d_e_f_g_h_i_j");
    CM_EXPECT(FileNaming::sanitise("name. . ") == "name");
    CM_EXPECT(FileNaming::sanitise(".hidden") == "_hidden");
    CM_EXPECT(FileNaming::sanitise("") == "untitled");
    CM_EXPECT(FileNaming::sanitise("tab\there\nnl") == "tab_here_nl");
    const auto uni = juce::String::fromUTF8("B\xc3\xbchne \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x8e\xb8");   // incl. 4-byte emoji
    CM_EXPECT(FileNaming::sanitise(uni) == uni);
    juce::String longName;
    for (int i = 0; i < 100; ++i) longName << juce::String::fromUTF8("\xe6\x97\xa5");   // 3 bytes each
    const auto capped = FileNaming::sanitise(longName, 100);
    CM_EXPECT(capped.getNumBytesAsUTF8() <= 100);
    CM_EXPECT(capped.length() == 33);   // whole code points only
    CM_EXPECT(juce::CharPointer_UTF8::isValidString(capped.toRawUTF8(), 1000));

    NameTokens tok;
    tok.time = juce::Time(2026, 9, 14, 21, 5, 9, 0, true);   // months are 0-based
    tok.scene = {};
    tok.source = "Mic/1";
    tok.take = 3;
    CM_EXPECT(FileNaming::expandPattern("{date}_{time}_{scene}_{source}", tok) == "2026-10-14_21-05-09_Mic_1");
    CM_EXPECT(FileNaming::expandPattern("{show}_{venue}_T{take}", tok) == "T03");
    tok.scene = "CON";
    CM_EXPECT(FileNaming::expandPattern("{scene}", tok) == "_CON");

    rectest::TempDir tmp;
    tmp.dir.getChildFile("x.wav").create();
    CM_EXPECT(FileNaming::uniqueFile(tmp.dir, "x", ".wav").getFileName() == "x_2.wav");
    CM_EXPECT(FileNaming::uniqueFile(tmp.dir, "x", ".wav", { tmp.dir.getChildFile("x_2.wav").getFullPathName() }).getFileName() == "x_3.wav");
}

CM_TEST(Recorder_StartFailsCleanly) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    auto o = opts(tmp.dir, RecorderEngine::RecordOptions::Wav24);
    CM_EXPECT(! rec.startRecording(o));   // not prepared
    rec.prepare(48000.0, 128, 4096, false);
    CM_EXPECT(! rec.startRecording(o));   // nothing armed
    CM_EXPECT(rec.getStatus().lastError.contains("armed"));
    armTracks(rec, 1);
    // Destination below a regular file cannot be created.
    auto blocker = tmp.dir.getChildFile("iam_a_file");
    blocker.replaceWithText("x");
    o.folder = blocker.getChildFile("sub");
    CM_EXPECT(! rec.startRecording(o));
    CM_EXPECT(rec.getStatus().lastError.isNotEmpty());
    CM_EXPECT(! rec.getStatus().recording);
    // Read-only folder (only meaningful when not running as root).
    auto ro = tmp.dir.getChildFile("ro");
    ro.createDirectory();
    ro.setReadOnly(true);
    const bool rootCanWrite = ro.getChildFile("probe").create().wasOk();
    if (! rootCanWrite) {
        o.folder = ro;
        CM_EXPECT(! rec.startRecording(o));
        CM_EXPECT(rec.getStatus().lastError.contains("Cannot create"));
        CM_EXPECT(ro.getNumberOfChildFiles(juce::File::findFiles) == 0);
    }
    ro.setReadOnly(false);
    o.folder = tmp.dir.getChildFile("ok");
    CM_EXPECT(rec.startRecording(o));
    CM_EXPECT(! rec.startRecording(o));   // already recording
    rec.stopRecording();                  // never latched: discarded, nothing left behind
    CM_EXPECT(! rec.getStatus().recording);
    CM_EXPECT(o.folder.getNumberOfChildFiles(juce::File::findFiles) == 0);
}

// Windows refuses to delete a directory that contains open files, so "destination removed while recording" cannot
// happen there (the removal itself fails); the scenario is exercised on POSIX platforms.
CM_TEST(Recorder_DestinationRemovedDuringRecording) {
#if JUCE_WINDOWS
    return;
#endif
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    armTracks(rec, 2);
    Driver d(rec, 128);
    auto folder = tmp.dir.getChildFile("dest");
    CM_EXPECT(rec.startRecording(opts(folder, RecorderEngine::RecordOptions::Wav24)));
    d.run(100, 4);
    CM_EXPECT(folder.deleteRecursively());
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));   // writer checks the destination every ~1 s
    d.run(100, 4);
    CM_EXPECT(rec.hasTrackError(0) && rec.hasTrackError(1));
    CM_EXPECT(rec.getStatus().errors >= 1);
    CM_EXPECT(rec.getStatus().lastError.contains("removed"));
    rec.splitFile();   // reopening fails too: stays in error state, no crash
    d.run(20, 4);
    CM_EXPECT(rec.hasTrackError(0));
    CM_EXPECT(rec.getStatus().dropouts == 0);   // the audio side never noticed
    rec.stopRecordingNow();
    CM_EXPECT(! rec.getStatus().recording);
}

namespace {
/** Seekable in-memory stream that fails like a full disk after `limit` bytes. */
struct FullDiskStream : juce::OutputStream {
    explicit FullDiskStream(size_t l) : limit(l) {}
    bool write(const void* p, size_t n) override {
        if (pos + n > limit) return false;
        if (data.getSize() < pos + n) data.setSize(pos + n);
        std::memcpy(static_cast<char*>(data.getData()) + pos, p, n);
        pos += n;
        return true;
    }
    void flush() override {}
    juce::int64 getPosition() override { return (juce::int64) pos; }
    bool setPosition(juce::int64 p) override { pos = (size_t) p; return true; }
    juce::MemoryBlock data;
    size_t pos = 0, limit;
};
} // namespace

CM_TEST(Recorder_DiskFullSetsErrorAndRecoversOnSplit) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    rec.setOutputStreamFactoryForTesting([](const juce::File&) { return std::make_unique<FullDiskStream>(64 * 1024); });
    armTracks(rec, 1);
    Driver d(rec, 128);
    CM_EXPECT(rec.startRecording(opts(tmp.dir, RecorderEngine::RecordOptions::Wav32f)));
    d.run(375, 4);   // 1 s mono float = 192 kB > 64 kB
    CM_EXPECT(rec.hasTrackError(0));
    CM_EXPECT(rec.getStatus().errors == 1);
    CM_EXPECT(rec.getStatus().lastError.contains("disk full"));
    rec.splitFile();   // new file (fresh stream) -> recovered
    d.run(40, 4);
    CM_EXPECT(! rec.hasTrackError(0));
    rec.stopRecordingNow();
    CM_EXPECT(rec.getStatus().dropouts == 0);
    rec.setOutputStreamFactoryForTesting(nullptr);
}

CM_TEST(Recorder_NoGrowthAcrossStartStopCycles) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 64, 1 << 12, false);
    armTracks(rec, RecorderEngine::kMaxTracks, 16);
    Driver d(rec, 64);
    const auto bytes = rec.getAllocatedBytes();
    CM_EXPECT(bytes == (size_t) RecorderEngine::kMaxTracks * 2 * (1 << 12) * sizeof(float));
    for (int i = 0; i < 20; ++i) {
        CM_EXPECT(rec.startRecording(opts(tmp.dir.getChildFile("c" + juce::String(i)), RecorderEngine::RecordOptions::Wav16)));
        d.run(10, 2);
        if (i % 2 == 0) rec.stopRecordingNow();
        else {
            rec.stopRecording();
            d.run(2, 1);
            rec.drainForTesting();
        }
        CM_EXPECT(! rec.getStatus().recording);
        CM_EXPECT(rec.getAllocatedBytes() == bytes);
    }
    CM_EXPECT(rec.getStatus().take == 20);
    // Pre-roll memory cap: 32 tracks (48 ch) x 60 s would exceed an 8 MB cap -> clipped, total bounded.
    rec.setPreRollMemoryLimit(8 << 20);
    rec.setPreRollSeconds(60);
    rec.drainForTesting();
    CM_EXPECT(rec.getStatus().preRollClipped);
    CM_EXPECT(rec.getStatus().preRollSeconds < 60.0 && rec.getStatus().preRollSeconds > 0.0);
    CM_EXPECT(rec.getAllocatedBytes() <= (size_t) (8 << 20) + bytes);
    rec.setPreRollSeconds(0);
    rec.drainForTesting();
    CM_EXPECT(rec.getAllocatedBytes() == bytes);
    rec.shutdown();
    CM_EXPECT(rec.getAllocatedBytes() == 0);
}
