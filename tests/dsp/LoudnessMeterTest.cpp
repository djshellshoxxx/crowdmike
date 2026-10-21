#include "TestHarness.h"
#include "DSP/LoudnessMeter.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace crowdmike::dsp;

namespace {
constexpr double kTwoPi = 6.283185307179586;

// Feeds `seconds` of a stereo sine (amplitude `amp`, frequency `hz`) in blocks of `block` samples.
void feedSine(LoudnessMeter& m, double sr, double hz, float amp, double seconds, int block, double& phase) {
    const long total = static_cast<long>(seconds * sr);
    std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
    for (long done = 0; done < total; done += block) {
        const int n = static_cast<int>(std::min<long>(block, total - done));
        for (int i = 0; i < n; ++i) {
            const float v = amp * static_cast<float>(std::sin(phase));
            phase += kTwoPi * hz / sr;
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = v;
        }
        m.push(l.data(), r.data(), n);
    }
}
}

CM_TEST(loudness_reference_sine_997hz_reads_minus_23_lufs_at_minus_20_dbfs) {
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        LoudnessMeter m;
        m.prepare(sr);
        double ph = 0.0;
        feedSine(m, sr, 997.0, 0.1f, 5.0, 480, ph);   // -20 dBFS peak per channel
        CM_EXPECT_NEAR(m.getShortTermLufs(), -23.0, 0.3);
        CM_EXPECT_NEAR(m.getMomentaryRmsDb(), -23.01, 0.2);   // unweighted 10 log10(0.1^2 / 2)
    }
}

CM_TEST(loudness_k_weighting_attenuates_low_frequencies_and_boosts_highs) {
    const double sr = 48000.0;
    LoudnessMeter low, mid, high;
    low.prepare(sr); mid.prepare(sr); high.prepare(sr);
    double p1 = 0, p2 = 0, p3 = 0;
    feedSine(low, sr, 30.0, 0.1f, 6.0, 256, p1);
    feedSine(mid, sr, 1000.0, 0.1f, 6.0, 256, p2);
    feedSine(high, sr, 8000.0, 0.1f, 6.0, 256, p3);
    CM_EXPECT(low.getShortTermLufs() < mid.getShortTermLufs() - 1.5);    // RLB high-pass is ~ -3 dB at 38 Hz and steeper below
    CM_EXPECT(high.getShortTermLufs() > mid.getShortTermLufs() + 3.0);   // shelf: about +4 dB
    CM_EXPECT(high.getShortTermLufs() < mid.getShortTermLufs() + 5.0);
}

CM_TEST(loudness_silence_nonfinite_and_mono_fallback_stay_finite) {
    LoudnessMeter m;
    m.prepare(48000.0);
    std::vector<float> z(48000, 0.0f);
    m.push(z.data(), z.data(), 48000);
    CM_EXPECT(m.getShortTermLufs() == LoudnessMeter::kFloorDb);
    CM_EXPECT(m.getMomentaryRmsDb() == LoudnessMeter::kFloorDb);
    std::vector<float> bad(4800, std::numeric_limits<float>::quiet_NaN());
    bad[3] = std::numeric_limits<float>::infinity();
    m.push(bad.data(), nullptr, 4800);   // right == nullptr reuses left
    CM_EXPECT(std::isfinite(m.getShortTermLufs()) && std::isfinite(m.getMomentaryRmsDb()));
    CM_EXPECT(m.getShortTermLufs() >= LoudnessMeter::kFloorDb);
    m.push(nullptr, nullptr, 100);       // ignored
    m.push(z.data(), z.data(), 0);
    CM_EXPECT(std::isfinite(m.getShortTermLufs()));
}

CM_TEST(loudness_result_is_independent_of_block_size_and_reset_is_deterministic) {
    const double sr = 48000.0;
    float ref = 0.0f;
    for (int block : { 1, 16, 64, 128, 512, 2048 }) {
        LoudnessMeter m;
        m.prepare(sr);
        double ph = 0.0;
        feedSine(m, sr, 440.0, 0.2f, 4.0, block, ph);
        if (block == 1) ref = m.getShortTermLufs();
        CM_EXPECT_NEAR(m.getShortTermLufs(), ref, 1e-3);
        m.reset();
        CM_EXPECT(m.getShortTermLufs() == LoudnessMeter::kFloorDb);
        ph = 0.0;
        feedSine(m, sr, 440.0, 0.2f, 4.0, block, ph);
        CM_EXPECT_NEAR(m.getShortTermLufs(), ref, 1e-3);
    }
}

CM_TEST(loudness_short_term_window_forgets_after_three_seconds) {
    const double sr = 48000.0;
    LoudnessMeter m;
    m.prepare(sr);
    double ph = 0.0;
    feedSine(m, sr, 1000.0, 0.5f, 4.0, 480, ph);
    const float loud = m.getShortTermLufs();
    feedSine(m, sr, 1000.0, 0.005f, 3.5, 480, ph);   // 40 dB quieter for longer than the window
    CM_EXPECT_NEAR(loud - m.getShortTermLufs(), 40.0, 0.5);
}
