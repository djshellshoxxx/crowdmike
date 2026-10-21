// Streaming read-ahead playback (RECORDING_SPEC 9.1).
#include "RecTestUtil.h"
#include "Recording/PlaybackEngine.h"
#include <filesystem>

using namespace crowdmike::rec;
using PE = PlaybackEngine;

namespace {

struct Out {
    std::vector<float> L, R;
};

/** Renders `total` frames of destination 0. `pump` > 0: the streamer is driven by hand `pump` times per block. */
Out renderPE(PE& pe, int total, int block, int pump) {
    std::vector<float> bl((size_t) block), br((size_t) block);
    float* dl[PE::kNumDestinations] {};
    float* dr[PE::kNumDestinations] {};
    Out o;
    for (int done = 0; done < total; done += block) {
        const int n = std::min(block, total - done);
        std::fill(bl.begin(), bl.end(), 0.0f); std::fill(br.begin(), br.end(), 0.0f);
        dl[0] = bl.data(); dr[0] = br.data();
        pe.process(dl, dr, n, false);
        o.L.insert(o.L.end(), bl.begin(), bl.begin() + n);
        o.R.insert(o.R.end(), br.begin(), br.begin() + n);
        for (int k = 0; k < pump; ++k) pe.pumpStreamsForTesting();
    }
    return o;
}

juce::File makeStereo(const juce::File& dir, const juce::String& name, double sr, int frames) {
    juce::AudioBuffer<float> b(2, frames);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i) b.setSample(c, i, rectest::ramp(3, i, c));
    auto f = dir.getChildFile(name);
    CM_EXPECT(rectest::writeWav(f, b, sr, 32));
    return f;
}

/** Declares a huge WAV: a real header patched to claim `seconds` of 16-bit mono at `sr`, extended sparse. */
juce::File makeHugeSparseWav(const juce::File& dir, const juce::String& name, double sr, double seconds) {
    juce::AudioBuffer<float> b(1, 800);
    for (int i = 0; i < 800; ++i) b.setSample(0, i, 0.25f);
    auto f = dir.getChildFile(name);
    CM_EXPECT(rectest::writeWav(f, b, sr, 16));
    juce::MemoryBlock mb;
    f.loadFileAsData(mb);
    auto* d = static_cast<unsigned char*>(mb.getData());
    size_t dataAt = 40;
    for (size_t k = 12; k + 8 < mb.getSize(); ++k)
        if (std::memcmp(d + k, "data", 4) == 0) { dataAt = k + 4; break; }
    const auto dataBytes = (juce::uint32) (seconds * sr * 2.0);
    for (int k = 0; k < 4; ++k) d[dataAt + (size_t) k] = (unsigned char) ((dataBytes >> (8 * k)) & 0xff);
    const juce::uint32 riff = (juce::uint32) (dataAt + 4 + dataBytes - 8);
    for (int k = 0; k < 4; ++k) d[4 + k] = (unsigned char) ((riff >> (8 * k)) & 0xff);
    f.replaceWithData(mb.getData(), mb.getSize());
    std::error_code ec;
    std::filesystem::resize_file(f.getFullPathName().toStdString(), dataAt + 4 + dataBytes, ec);
    CM_EXPECT(! ec);
    {   // audible 1 s bursts every 10 s so any region the bed picks contains sound
        juce::FileOutputStream os(f);
        std::vector<short> burst((size_t) sr, (short) 8192);
        for (double t = 10.0; t + 1.0 < seconds; t += 10.0) {
            os.setPosition((juce::int64) (dataAt + 4) + (juce::int64) (t * sr) * 2);
            os.write(burst.data(), burst.size() * sizeof(short));
        }
    }
    return f;
}

double maxDiff(const Out& a, const Out& b, size_t from = 0, size_t to = 0) {
    if (to == 0) to = std::min(a.L.size(), b.L.size());
    double m = 0.0;
    for (size_t i = from; i < to; ++i)
        m = std::max({ m, (double) std::abs(a.L[i] - b.L[i]), (double) std::abs(a.R[i] - b.R[i]) });
    return m;
}

} // namespace

CM_TEST(Streaming_MatchesResidentForwardReverseLoop) {
    rectest::TempDir tmp("st");
    const int frames = 48000 * 6;
    auto f = makeStereo(tmp.dir, "s.wav", 48000.0, frames);

    for (int variant = 0; variant < 3; ++variant) {
        PE ref, st;
        ref.prepare(48000.0, 256);
        st.prepare(48000.0, 256, false);
        st.setResidentLimitSeconds(1.0);     // everything longer than 1 s streams
        ref.loadClip(0, f);
        st.loadClip(0, f);
        CM_EXPECT(ref.waitForLoads(10000) && st.waitForLoads(10000));
        CM_EXPECT(! ref.getClipInfo(0).streaming && st.getClipInfo(0).streaming);
        CM_EXPECT(st.getClipInfo(0).headFrames > 0);
        PE::ClipSettings s;
        s.mode = variant == 2 ? PE::ClipSettings::Loop : PE::ClipSettings::OneShot;
        s.reverse = variant == 1;
        s.startFrac = variant == 1 ? 0.1 : 0.0;
        s.endFrac = variant == 2 ? 0.5 : 1.0;      // loop of 3 s wraps several times
        ref.setSettings(0, s);
        st.setSettings(0, s);
        CM_EXPECT(ref.waitForLoads(10000) && st.waitForLoads(10000));
        ref.trigger(0);
        st.trigger(0);
        const int total = variant == 2 ? 48000 * 10 : 48000 * 6 + 480;
        const auto a = renderPE(ref, total, 256, 0);
        const auto b = renderPE(st, total, 256, 6);
        CM_EXPECT_NEAR(maxDiff(a, b), 0.0, 1e-6);
        CM_EXPECT(st.getStreamStats().underrunEvents == 0);
    }
}

CM_TEST(Streaming_HeadStartsInstantlyAndStalledStreamerGivesSilenceNotCrash) {
    rectest::TempDir tmp("st");
    auto f = makeStereo(tmp.dir, "s.wav", 48000.0, 48000 * 8);
    PE ref, st;
    ref.prepare(48000.0, 256);
    st.prepare(48000.0, 256, false);          // the streamer never runs on its own
    st.setResidentLimitSeconds(1.0);
    ref.loadClip(0, f);
    st.loadClip(0, f);
    CM_EXPECT(ref.waitForLoads(10000) && st.waitForLoads(10000));
    ref.trigger(0);
    st.trigger(0);
    const auto a = renderPE(ref, 48000 * 4, 256, 0);
    const auto b = renderPE(st, 48000 * 4, 256, 0);   // never pumped
    // the first second comes from the head, bit-exact, from the very first sample
    CM_EXPECT_NEAR(maxDiff(a, b, 0, 48000), 0.0, 1e-6);
    // after the ring (about 1.3 s) there is silence, an underrun is counted and everything stays finite
    const auto stats = st.getStreamStats();
    CM_EXPECT(stats.underrunEvents > 0 && stats.underrunFrames > 0);
    double tail = 0.0;
    for (size_t i = 48000 * 3; i < b.L.size(); ++i) { CM_EXPECT(std::isfinite(b.L[i])); tail = std::max(tail, (double) std::abs(b.L[i])); }
    CM_EXPECT_NEAR(tail, 0.0, 0.0);
    // the streamer catching up again resumes audio without a crash and the clip keeps its place
    st.resetStreamStats();
    const auto c = renderPE(st, 48000, 256, 6);
    for (float v : c.L) CM_EXPECT(std::isfinite(v));
}

CM_TEST(Streaming_TwoHourBedStaysBounded) {
    rectest::TempDir tmp("st");
    const double seconds = 2.0 * 3600.0;
    auto f = makeHugeSparseWav(tmp.dir, "bed2h.wav", 8000.0, seconds);

    PE pe;
    pe.prepare(48000.0, 512);                 // real streamer thread
    pe.loadClip(0, f);
    CM_EXPECT(pe.waitForLoads(60000));
    const auto info = pe.getClipInfo(0);
    CM_EXPECT(info.loaded && info.streaming);
    CM_EXPECT_NEAR(info.durationSeconds, seconds, 1.0);
    const auto m0 = pe.getMemoryStats();
    CM_EXPECT(m0.residentClips == 0 && m0.streamedClips == 1);
    // decoded 2 h stereo float would be 2.76 GB; head + rings must stay below a few MB
    CM_EXPECT(m0.totalBytes < 16ll * 1024 * 1024);

    PE::BedSettings bed;
    bed.slot = 0;
    bed.enabled = true;
    bed.minLoopSec = 5.0;
    bed.maxLoopSec = 8.0;
    bed.crossfadeSec = 1.0;
    bed.gainDb = 0.0f;
    pe.setBed(bed);
    juce::Thread::sleep(50);
    std::vector<float> bl(512), br(512);
    float* dl[PE::kNumDestinations] {};
    float* dr[PE::kNumDestinations] {};
    double peak = 0.0;
    for (int blk = 0; blk < 48000 * 25 / 512; ++blk) {
        std::fill(bl.begin(), bl.end(), 0.0f); std::fill(br.begin(), br.end(), 0.0f);
        dl[0] = bl.data(); dr[0] = br.data();
        pe.process(dl, dr, 512, false);
        for (float v : bl) { CM_EXPECT(std::isfinite(v)); peak = std::max(peak, (double) std::abs(v)); }
        if (blk % 20 == 0) juce::Thread::sleep(8);   // keep roughly real-time so the streamer thread can keep up
    }
    CM_EXPECT(peak > 0.0);                    // the bed produced sound from the streamed source
    const auto m1 = pe.getMemoryStats();
    CM_EXPECT(m1.totalBytes < 16ll * 1024 * 1024);
    CM_EXPECT(m1.residentBytes == 0);
    // waveform sketch still exists for the UI
    float mn[64], mx[64];
    CM_EXPECT(pe.getWaveform(0, mn, mx, 64));
}

CM_TEST(Streaming_LongClipLimitAndMemoryAccountingReturnsToZero) {
    rectest::TempDir tmp("st");
    auto f = makeStereo(tmp.dir, "s.wav", 48000.0, 48000 * 3);
    PE pe;
    pe.prepare(48000.0, 256, false);
    pe.loadClip(0, f);
    CM_EXPECT(pe.waitForLoads(10000));
    CM_EXPECT(pe.getMemoryStats().residentClips == 1 && pe.getMemoryStats().residentBytes > 0);
    pe.setResidentLimitSeconds(1.0);
    pe.loadClip(1, f);
    CM_EXPECT(pe.waitForLoads(10000));
    CM_EXPECT(pe.getMemoryStats().streamedClips == 1 && pe.getMemoryStats().headBytes > 0);
    pe.unloadClip(0);
    pe.unloadClip(1);
    renderPE(pe, 512, 256, 1);   // commands reach the audio thread
    pe.releaseRetired();
    CM_EXPECT(pe.waitForLoads(10000));
    pe.releaseRetired();
    const auto m = pe.getMemoryStats();
    CM_EXPECT(m.residentBytes == 0 && m.headBytes == 0);
}
