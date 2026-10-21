// Clip tempo sync (RECORDING_SPEC 9.2): quantised triggers land on the grid at 60 / 120 / 174 BPM.
#include "RecTestUtil.h"
#include "Recording/PlaybackEngine.h"

using namespace crowdmike::rec;
using PE = PlaybackEngine;

namespace {

constexpr double kSr = 48000.0;

juce::File dcClip(const juce::File& dir, int frames) {
    juce::AudioBuffer<float> b(1, frames);
    for (int i = 0; i < frames; ++i) b.setSample(0, i, 0.5f);
    auto f = dir.getChildFile("dc.wav");
    CM_EXPECT(rectest::writeWav(f, b, kSr, 32));
    return f;
}

/** Runs blocks with the given tempo; returns the absolute frame of the first audible sample (-1: none). */
long long onsetFrame(PE& pe, double bpm, double startPpq, bool hasTempo, bool playing, int block, int totalBlocks,
                     int triggerBlock, bool now = false) {
    std::vector<float> bl((size_t) block), br((size_t) block);
    float* dl[PE::kNumDestinations] {};
    float* dr[PE::kNumDestinations] {};
    const double bps = bpm / (60.0 * kSr);
    long long frame = 0;
    for (int b = 0; b < totalBlocks; ++b) {
        if (b == triggerBlock) { if (now) pe.triggerNow(0); else pe.trigger(0); }
        pe.setTempo({ bpm, startPpq + (double) frame * bps, hasTempo, playing });
        std::fill(bl.begin(), bl.end(), 0.0f); std::fill(br.begin(), br.end(), 0.0f);
        dl[0] = bl.data(); dr[0] = br.data();
        pe.process(dl, dr, block, false);
        for (int i = 0; i < block; ++i)
            if (std::abs(bl[(size_t) i]) > 1e-6f) return frame + i;
        frame += block;
    }
    return -1;
}

} // namespace

CM_TEST(TempoSync_TriggerLandsOnGridAt60_120_174) {
    rectest::TempDir tmp("ts");
    auto f = dcClip(tmp.dir, 48000 * 4);
    for (double bpm : { 60.0, 120.0, 174.0 })
        for (int block : { 64, 480, 1000, 2048 })
            for (int q : { 0, 2, 4 }) {                       // 1/16, 1 beat, 1 bar
                PE pe;
                pe.prepare(kSr, 2048);
                CM_EXPECT(pe.loadClip(0, f) && pe.waitForLoads(10000));
                PE::ClipSettings s;
                s.tempoSync = true;
                s.syncQuantise = q;
                pe.setSettings(0, s);
                const double startPpq = 0.3;
                const int triggerBlock = 3;
                const long long at = onsetFrame(pe, bpm, startPpq, true, true, block, 4 * 48000 / block, triggerBlock);
                // the command runs at the top of block `triggerBlock`; the grid line is the next multiple of the grid
                const double gridBeats = PE::quantiseBeats(q);
                const double ppqAtTrigger = startPpq + (double) triggerBlock * block * bpm / (60.0 * kSr);
                const double lineBeat = std::ceil(ppqAtTrigger / gridBeats) * gridBeats;
                const double expected = (double) triggerBlock * block + (lineBeat - ppqAtTrigger) * 60.0 / bpm * kSr;
                CM_EXPECT(at >= 0);
                CM_EXPECT_NEAR((double) at, expected, 2.5);
                if (std::abs((double) at - expected) > 2.5)
                    std::fprintf(stderr, "  bpm=%g block=%d q=%d onset=%lld expected=%.1f\n", bpm, block, q, at, expected);
            }
}

CM_TEST(TempoSync_FreeRunWhenStoppedImmediateWithoutTempoAndTriggerNow) {
    rectest::TempDir tmp("ts");
    auto f = dcClip(tmp.dir, 48000 * 4);
    auto make = [&](PE& pe) {
        pe.prepare(kSr, 512);
        CM_EXPECT(pe.loadClip(0, f) && pe.waitForLoads(10000));
        PE::ClipSettings s;
        s.tempoSync = true;
        s.syncQuantise = 2;     // 1 beat
        pe.setSettings(0, s);
    };
    {   // transport stopped, tempo known: waits for the free-running grid (at most one beat = 0.5 s at 120)
        PE pe; make(pe);
        const long long at = onsetFrame(pe, 120.0, 0.0, true, false, 512, 200, 2);
        CM_EXPECT(at > 2 * 512 && at <= 2 * 512 + 24000 + 512);
    }
    {   // no tempo: immediate start
        PE pe; make(pe);
        const long long at = onsetFrame(pe, 120.0, 0.0, false, false, 512, 200, 2);
        CM_EXPECT(at >= 2 * 512 && at < 2 * 512 + 4);
    }
    {   // triggerNow bypasses the grid
        PE pe; make(pe);
        const long long at = onsetFrame(pe, 120.0, 0.3, true, true, 512, 200, 2, true);
        CM_EXPECT(at >= 2 * 512 && at < 2 * 512 + 4);
    }
    {   // sync off: immediate even with tempo
        PE pe;
        pe.prepare(kSr, 512);
        CM_EXPECT(pe.loadClip(0, f) && pe.waitForLoads(10000));
        const long long at = onsetFrame(pe, 120.0, 0.3, true, true, 512, 200, 2);
        CM_EXPECT(at >= 2 * 512 && at < 2 * 512 + 4);
    }
}

CM_TEST(TempoSync_LoopRestartsEveryNBeats) {
    rectest::TempDir tmp("ts");
    // 3 s ramp 0..0.9: a restart shows as a sudden fall back to the start
    const int frames = 48000 * 3;
    juce::AudioBuffer<float> b(1, frames);
    for (int i = 0; i < frames; ++i) b.setSample(0, i, 0.9f * (float) i / (float) frames);
    auto f = tmp.dir.getChildFile("ramp.wav");
    CM_EXPECT(rectest::writeWav(f, b, kSr, 32));
    PE pe;
    pe.prepare(kSr, 480);
    CM_EXPECT(pe.loadClip(0, f) && pe.waitForLoads(10000));
    PE::ClipSettings s;
    s.tempoSync = true;
    s.mode = PE::ClipSettings::Loop;
    s.syncQuantise = 2;
    s.syncLoopBeats = 1.0;          // 1 beat = 0.5 s at 120 BPM, well inside the 3 s clip
    pe.setSettings(0, s);
    pe.trigger(0);
    std::vector<float> bl(480), br(480);
    float* dl[PE::kNumDestinations] {};
    float* dr[PE::kNumDestinations] {};
    std::vector<float> all;
    long long frame = 0;
    for (int blk = 0; blk < 48000 * 5 / 480; ++blk) {
        pe.setTempo({ 120.0, 0.3 + (double) frame * 120.0 / (60.0 * kSr), true, true });
        std::fill(bl.begin(), bl.end(), 0.0f); std::fill(br.begin(), br.end(), 0.0f);
        dl[0] = bl.data(); dr[0] = br.data();
        pe.process(dl, dr, 480, false);
        all.insert(all.end(), bl.begin(), bl.end());
        frame += 480;
    }
    // a restart: a sample that is much smaller than 200 samples earlier (the ramp climbs slowly otherwise)
    std::vector<size_t> restarts;
    for (size_t i = 24000; i + 1 < all.size(); ++i)
        if (all[i] < 0.02f && all[i - 20] > 0.05f && (restarts.empty() || i - restarts.back() > 2000)) restarts.push_back(i);
    CM_EXPECT(restarts.size() >= 5);
    for (size_t k = 1; k < restarts.size(); ++k)
        CM_EXPECT_NEAR((double) (restarts[k] - restarts[k - 1]), 24000.0, 40.0);
    for (float v : all) CM_EXPECT(std::isfinite(v));
}
