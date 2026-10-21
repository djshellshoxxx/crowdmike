// FLAC recording, disk guard, marker export, transport trigger, monitor bus (RECORDING_SPEC 2, 14, 15).
#include "RecTestUtil.h"
#include "Recording/MarkerExport.h"
#include "Recording/MonitorBus.h"
#include "Recording/PlaybackEngine.h"
#include "Recording/RecorderEngine.h"
#include "Recording/TransportRecordTrigger.h"

using namespace crowdmike::rec;
using rectest::ramp;

namespace {

struct Drv {
    RecorderEngine& rec;
    int block;
    std::vector<float> data[2];
    Drv(RecorderEngine& r, int b) : rec(r), block(b) { data[0].resize((size_t) b); data[1].resize((size_t) b); }
    void run(int blocks, int drainEvery = 4) {
        for (int b = 0; b < blocks; ++b) {
            const auto pos = rec.getFramePosition();
            for (int t = 0; t < RecorderEngine::kMaxTracks; ++t) {
                if (! rec.isTrackActive(t)) continue;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i) data[c][(size_t) i] = ramp(t, pos + i, c);
                const float* p[2] = { data[0].data(), data[1].data() };
                rec.pushTap(t, p, 2, block);
            }
            rec.advance(block);
            if (drainEvery > 0 && b % drainEvery == drainEvery - 1) rec.drainForTesting();
        }
    }
};

void arm(RecorderEngine& rec, int n, int stereoFrom) {
    for (int t = 0; t < n; ++t) rec.setTrack(t, { 100 + t, t >= stereoFrom ? 2 : 1, "T" + juce::String(t + 1), true });
}

RecorderEngine::RecordOptions options(const juce::File& folder, RecorderEngine::RecordOptions::Format f) {
    RecorderEngine::RecordOptions o;
    o.folder = folder;
    o.format = f;
    o.scene = "Scene";
    o.appVersion = "test";
    return o;
}

bool readAny(const juce::File& f, juce::AudioBuffer<float>& out) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) return false;
    out.setSize((int) r->numChannels, (int) r->lengthInSamples);
    return r->read(&out, 0, (int) r->lengthInSamples, 0, true, true);
}

} // namespace

CM_TEST(Flac_RoundTripBitExactWithSidecarMarkersAndSplit) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    arm(rec, 3, 1);   // track 0 mono, 1 and 2 stereo
    Drv d(rec, 128);
    auto o = options(tmp.dir, RecorderEngine::RecordOptions::Flac24);
    o.maxFileSeconds = 0.5;   // several parts
    CM_EXPECT(rec.startRecording(o));
    d.run(100);
    rec.addMarker("Big one", MarkerType::BigCheer);
    d.run(300);
    rec.stopRecordingNow();

    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.fileFormat == "flac24");
    CM_EXPECT(info.markers.size() == 1 && info.markers[0].label == "Big one");
    CM_EXPECT(info.totalFrames == 400 * 128 - 128);
    for (auto& tr : info.tracks) {
        const auto files = info.filesForTrack(tr.track);
        CM_EXPECT(files.size() > 1);
        std::int64_t frame = 0;
        double maxErr = 0.0;
        for (auto& fe : files) {
            CM_EXPECT(fe.file.getFileExtension() == ".flac");
            juce::AudioBuffer<float> b;
            CM_EXPECT(readAny(fe.file, b));
            CM_EXPECT(b.getNumChannels() == tr.numChannels);
            CM_EXPECT(fe.startFrame == frame && fe.numFrames == b.getNumSamples());
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    maxErr = std::max(maxErr, (double) std::abs(b.getSample(c, i) - ramp(tr.track, info.startFrame + frame + i, c)));
            frame += b.getNumSamples();
        }
        CM_EXPECT(frame == info.totalFrames);
        CM_EXPECT(maxErr <= 1.2e-7);   // within one 24-bit LSB, identical to WAV 24 quantisation (FLAC itself is lossless)
    }
    CM_EXPECT(rec.getStatus().errors == 0);
}

CM_TEST(Flac_AndAiffClipsLoadInPlayback) {
    rectest::TempDir tmp;
    juce::AudioBuffer<float> b(2, 4800);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 4800; ++i) b.setSample(c, i, ramp(1, i, c));
    auto write = [&](juce::AudioFormat& fmt, const juce::File& f) {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(f);
        auto w = fmt.createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(48000.0).withNumChannels(2).withBitsPerSample(24)
                                              .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral));
        CM_EXPECT(w != nullptr);
        if (w != nullptr) CM_EXPECT(w->writeFromAudioSampleBuffer(b, 0, 4800));
    };
    const auto flac = tmp.dir.getChildFile("c.flac"), aiff = tmp.dir.getChildFile("c.aiff");
    write(*std::make_unique<juce::FlacAudioFormat>(), flac);
    write(*std::make_unique<juce::AiffAudioFormat>(), aiff);
    for (auto& f : { flac, aiff }) {
        PlaybackEngine pe;
        pe.prepare(48000.0, 480);
        pe.loadClip(0, f);
        CM_EXPECT(pe.waitForLoads(10000) && pe.getClipInfo(0).loaded);
        pe.trigger(0);
        std::vector<float> L(4800, 0.0f), R(4800, 0.0f);
        float* dl[PlaybackEngine::kNumDestinations] {}; float* dr[PlaybackEngine::kNumDestinations] {};
        double maxErr = 0.0;
        for (int blk = 0; blk < 10; ++blk) {
            dl[0] = L.data() + blk * 480; dr[0] = R.data() + blk * 480;
            pe.process(dl, dr, 480, false);
        }
        // the start ramp (5 ms) shapes the first samples; compare after it
        for (int i = 400; i < 4500; ++i)
            maxErr = std::max({ maxErr, (double) std::abs(L[(size_t) i] - ramp(1, i, 0)), (double) std::abs(R[(size_t) i] - ramp(1, i, 1)) });
        if (maxErr > 1e-6) std::fprintf(stderr, "file %s maxErr %g\n", f.getFileName().toRawUTF8(), maxErr);
        CM_EXPECT_NEAR(maxErr, 0.0, 1e-6);
    }
}

CM_TEST(DiskGuard_LowDiskWarnsStopsCleanlyAndRefusesStart) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    arm(rec, 2, 1);
    std::atomic<juce::int64> freeBytes { 10000000000ll };
    rec.setFreeSpaceProviderForTesting([&](const juce::File&) { return freeBytes.load(); });
    Drv d(rec, 128);
    auto o = options(tmp.dir, RecorderEngine::RecordOptions::Wav24);
    CM_EXPECT(rec.startRecording(o));
    d.run(40);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    d.run(8);
    CM_EXPECT(! rec.getStatus().lowDisk && ! rec.getStatus().diskStopped);
    CM_EXPECT(rec.getStatus().diskSecondsLeft > 100.0);

    freeBytes = 300ll * 1000000;   // below warn (500 MB), above stop (100 MB)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    d.run(8);
    CM_EXPECT(rec.getStatus().lowDisk && ! rec.getStatus().diskStopped);
    CM_EXPECT(rec.getStatus().recording);

    freeBytes = 50ll * 1000000;    // below stop: the take ends cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    d.run(8);
    d.run(8);
    CM_EXPECT(rec.getStatus().diskStopped);
    CM_EXPECT(rec.getStatus().lastError.isNotEmpty());
    CM_EXPECT(! rec.getStatus().recording);
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));   // session JSON complete
    CM_EXPECT(info.totalFrames > 0);
    for (auto& fe : info.files) {
        juce::AudioBuffer<float> b;
        CM_EXPECT(readAny(fe.file, b) && b.getNumSamples() == fe.numFrames);   // files finalised and readable
    }
    CM_EXPECT(! rec.startRecording(o));   // refused while the disk is nearly full
    CM_EXPECT(rec.getStatus().lastError.isNotEmpty());
    freeBytes = 10000000000ll;
    CM_EXPECT(rec.startRecording(o));
    rec.stopRecordingNow();
}

CM_TEST(MarkerExport_CsvAudacityReaperAndFiles) {
    rectest::TempDir tmp;
    SessionInfo info;
    info.sampleRate = 48000.0;
    info.scene = "Scene A";
    Marker m1, m2;
    m1.frame = 48000 * 3 / 2; m1.seconds = 1.5; m1.label = "Cheer, \"big\""; m1.scene = "Scene A"; m1.type = MarkerType::BigCheer;
    m2.frame = 48000 * 3725; m2.seconds = 3725.0; m2.label = "Dropout"; m2.scene = "Scene A"; m2.type = MarkerType::Problem;
    info.markers = { m1, m2 };
    const auto csv = MarkerExport::toCsv(info);
    CM_EXPECT(csv.contains("\"Cheer, \"\"big\"\"\""));            // RFC 4180 quoting
    CM_EXPECT(csv.contains("0:00:01.500") && csv.contains("1:02:05.000"));
    CM_EXPECT(csv.contains("72000"));
    const auto aud = MarkerExport::toAudacityLabels(info);
    CM_EXPECT(aud.contains("1.500000\t1.500000\t"));
    CM_EXPECT(aud.contains("3725.000000\t3725.000000\t"));
    CM_EXPECT(MarkerExport::toReaperCsv(info).startsWith("#,Name,Start,End,Length,Color"));
    CM_EXPECT(MarkerExport::timecode(59.9996) == "0:01:00.000");

    auto json = tmp.dir.getChildFile("take.json");
    juce::String err;
    CM_EXPECT(MarkerExport::writeAll(json, info, err));
    CM_EXPECT(tmp.dir.getChildFile("take_markers.csv").existsAsFile());
    CM_EXPECT(tmp.dir.getChildFile("take_markers.txt").existsAsFile());
}

CM_TEST(MarkerExport_AutoExportOnTakeEnd) {
    rectest::TempDir tmp;
    RecorderEngine rec;
    rec.prepare(48000.0, 128, RecorderEngine::kDefaultFifoFrames, false);
    arm(rec, 1, 1);
    Drv d(rec, 128);
    auto o = options(tmp.dir, RecorderEngine::RecordOptions::Wav16);
    o.exportMarkers = true;
    o.tempoBpm = 123.5;
    CM_EXPECT(rec.startRecording(o));
    d.run(20);
    rec.addMarker("Hello", MarkerType::Chant);
    d.run(20);
    rec.stopRecordingNow();
    const auto json = rec.getLastSessionFile();
    CM_EXPECT(json.withFileExtension("").getSiblingFile(json.getFileNameWithoutExtension() + "_markers.csv").existsAsFile());
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(json, info));
    CM_EXPECT_NEAR(info.tempoBpm, 123.5, 1e-9);
}

CM_TEST(TransportRecord_StartsAndStopsOnlyOwnTakes) {
    TransportRecordTrigger t;
    using A = TransportRecordTrigger::Action;
    t.setHostPlaying(true);
    CM_EXPECT(t.poll(false) == A::None);          // disabled
    t.setEnabled(true);
    t.setHostPlaying(false);
    CM_EXPECT(t.poll(false) == A::None);
    t.setHostPlaying(true);
    CM_EXPECT(t.poll(false) == A::Start);
    t.noteStarted();
    CM_EXPECT(t.poll(true) == A::None);
    t.setHostPlaying(false);
    CM_EXPECT(t.poll(true) == A::Stop);
    // a manual take is left alone
    t.setHostPlaying(true);
    CM_EXPECT(t.poll(true) == A::None);
    t.setHostPlaying(false);
    CM_EXPECT(t.poll(true) == A::None);
}

CM_TEST(Monitor_RoutesMonitoredTapsToCuePair) {
    MonitorBus mb;
    mb.prepare(48000.0, 256);
    CM_EXPECT(! mb.isActive());
    mb.setTrackMonitored(0, true);
    CM_EXPECT(! mb.isActive());          // no pair chosen
    mb.setPair(2);
    CM_EXPECT(mb.isActive() && mb.getPair() == 2);
    mb.setTrackMonitored(1, true);
    mb.setTrackMonitored(2, false);
    CM_EXPECT(mb.isTrackMonitored(0) && mb.isTrackMonitored(1) && ! mb.isTrackMonitored(2));

    const int n = 256;
    std::vector<float> a(n, 0.25f), bl(n, 0.1f), br(n, -0.1f), outL(n, 0.0f), outR(n, 0.0f);
    for (int blk = 0; blk < 4; ++blk) {   // let the gain ramp settle
        std::fill(outL.begin(), outL.end(), 0.0f); std::fill(outR.begin(), outR.end(), 0.0f);
        mb.beginBlock(n);
        mb.addSource(a.data(), nullptr, n);               // mono tap -> both sides
        mb.addSource(bl.data(), br.data(), n);            // stereo tap
        mb.mixInto(outL.data(), outR.data(), n);
    }
    CM_EXPECT_NEAR(outL[n - 1], 0.35f, 1e-6);
    CM_EXPECT_NEAR(outR[n - 1], 0.15f, 1e-6);
    CM_EXPECT_NEAR(mb.takePeak(), 0.35f, 1e-6);
    CM_EXPECT(mb.takePeak() == 0.0f);

    mb.setGainDb(-6.0206f);
    for (int blk = 0; blk < 4; ++blk) {
        std::fill(outL.begin(), outL.end(), 0.0f); std::fill(outR.begin(), outR.end(), 0.0f);
        mb.beginBlock(n);
        mb.addSource(a.data(), nullptr, n);
        mb.mixInto(outL.data(), outR.data(), n);
    }
    CM_EXPECT_NEAR(outL[n - 1], 0.125f, 1e-3);

    // the cue is added to what is already on the output and hard-limited
    std::vector<float> loud(n, 0.9f);
    mb.setGainDb(12.0f);
    for (int blk = 0; blk < 6; ++blk) {
        std::fill(outL.begin(), outL.end(), 0.5f);
        mb.beginBlock(n);
        mb.addSource(loud.data(), loud.data(), n);
        mb.mixInto(outL.data(), nullptr, n);               // pair partly beyond the outputs: right is nullptr
    }
    for (float v : outL) CM_EXPECT(std::abs(v) <= 1.0f && std::isfinite(v));
    mb.setPair(-1);
    CM_EXPECT(! mb.isActive());
}
