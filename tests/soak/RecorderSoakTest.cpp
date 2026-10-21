// Recorder disk-full soak: a sustained multitrack take on a destination that repeatedly runs out of space.
// Contract (RECORDING_SPEC): the audio side never notices (no dropouts, never blocks), the error is reported,
// splitFile() onto fresh space recovers, memory stays bounded.
#include "SoakSupport.h"
#include "Recording/RecorderEngine.h"
#include "../recording/RecTestUtil.h"

using namespace crowdmike::rec;
using namespace soak;

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
}

CM_TEST(Soak_Recorder_DiskFullRepeatedly) {
    constexpr double sr = 48000.0;
    constexpr int block = 256, tracks = 8;
    rectest::TempDir tmp("soak");
    RecorderEngine rec;
    rec.prepare(sr, block, RecorderEngine::kDefaultFifoFrames, false);
    for (int t = 0; t < tracks; ++t) rec.setTrack(t, { 100 + t, t % 2 + 1, "Track " + juce::String(t + 1), true });
    const auto bytes = rec.getAllocatedBytes();

    RecorderEngine::RecordOptions o;
    o.folder = tmp.dir;
    o.format = RecorderEngine::RecordOptions::Wav24;
    o.maxFileSeconds = 0.0;
    // Every file gets a different (small) quota, so the disk "fills up" again and again.
    std::atomic<int> opened { 0 };
    rec.setOutputStreamFactoryForTesting([&](const juce::File&) {
        return std::make_unique<FullDiskStream>(((size_t) (opened.fetch_add(1) % 5) + 1) * 96 * 1024);
    });
    CM_EXPECT(rec.startRecording(o));

    std::vector<float> data[2] = { std::vector<float>(block), std::vector<float>(block) };
    const double seconds = envSeconds("CROWDMIKE_SOAK_REC_SECONDS", 300.0);
    const auto totalBlocks = (std::int64_t) (seconds * sr / block);
    int recoveries = 0, errorsSeen = 0;
    for (std::int64_t b = 0; b < totalBlocks; ++b) {
        const auto pos = rec.getFramePosition();
        for (int t = 0; t < tracks; ++t) {
            if (! rec.isTrackActive(t)) continue;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i) data[c][(size_t) i] = rectest::ramp(t, pos + i, c) * 0.5f;
            const float* p[2] = { data[0].data(), data[1].data() };
            rec.pushTap(t, p, 2, block);
        }
        rec.advance(block);
        if (b % 4 == 3) rec.drainForTesting();
        if (b % 400 == 399) {   // operator reaction roughly every 2 s of audio: split onto fresh space if in error
            bool anyErr = false;
            for (int t = 0; t < tracks; ++t) anyErr = anyErr || rec.hasTrackError(t);
            if (anyErr) { ++errorsSeen; rec.splitFile(); rec.drainForTesting(); ++recoveries; }
        }
    }
    CM_EXPECT(errorsSeen > 0);                       // the simulated disk really did fill
    CM_EXPECT(rec.getStatus().dropouts == 0);        // audio side never lost a block
    CM_EXPECT(rec.getStatus().errors > 0);
    rec.stopRecordingNow();
    CM_EXPECT(! rec.getStatus().recording);
    CM_EXPECT(rec.getAllocatedBytes() == bytes);     // FIFOs/pre-roll bounded, nothing grew
    rec.setOutputStreamFactoryForTesting(nullptr);
    // The recorder is reusable afterwards on a healthy disk.
    CM_EXPECT(rec.startRecording(o));
    for (int b = 0; b < 100; ++b) {
        for (int t = 0; t < tracks; ++t) {
            const float* p[2] = { data[0].data(), data[1].data() };
            if (rec.isTrackActive(t)) rec.pushTap(t, p, 2, block);
        }
        rec.advance(block);
        rec.drainForTesting();
    }
    for (int t = 0; t < tracks; ++t) CM_EXPECT(! rec.hasTrackError(t));
    rec.stopRecordingNow();
    (void) recoveries;
}
