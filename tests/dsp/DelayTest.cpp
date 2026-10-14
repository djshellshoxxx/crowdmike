// Tests: ChannelDelay (exact integer delay, gliding changes, zero-cost 0 ms).
#include "TestHarness.h"
#include "DSP/Biquad.h"
#include "DSP/ChannelDelay.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <vector>

using namespace crowdmike::dsp;

namespace {
constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr double kPiD = 3.14159265358979323846;

std::vector<float> sine(double sr, double hz, double amp, int n) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPiD * hz * i / sr));
    return v;
}
std::vector<float> noise(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = d(rng);
    return v;
}
bool allFinite(const std::vector<float>& y) {
    for (float v : y) if (!std::isfinite(v)) return false;
    return true;
}
bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double m = 0.0;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(static_cast<double>(a[i]) - b[i]));
    return m;
}
// true if y[i] == x[i - d] bit-exactly for i in [from, to)
bool isExactDelay(const std::vector<float>& y, const std::vector<float>& x, int d, int from, int to) {
    for (int i = std::max(from, d); i < to; ++i)
        if (y[static_cast<size_t>(i)] != x[static_cast<size_t>(i - d)]) return false;
    return true;
}
double clickLevel(const std::vector<float>& y, double sr, double toneHz, int from = 2048) {
    Biquad hp[4];
    for (int k = 0; k < 4; ++k) {
        const double q = 1.0 / (2.0 * std::cos(kPiD * (2.0 * k + 1.0) / 16.0));
        hp[k].setCoeffs(designHighpass(sr, static_cast<float>(6.0 * toneHz), static_cast<float>(q)));
    }
    double m = 0.0;
    for (size_t i = 0; i < y.size(); ++i) {
        float v = y[i];
        for (auto& b : hp) v = b.process(v);
        if (static_cast<int>(i) >= from) m = std::max(m, static_cast<double>(std::fabs(v)));
    }
    return m;
}
std::vector<float> runDelay(double sr, const std::vector<float>& in, int block, const std::function<float(int)>& ms,
                            ChannelDelay* ext = nullptr, float maxMs = 500.0f) {
    ChannelDelay local;
    ChannelDelay& d = ext != nullptr ? *ext : local;
    if (ext == nullptr) d.prepare(sr, 2048, 1, maxMs);
    auto v = in;
    const int n = static_cast<int>(v.size());
    for (int s = 0; s < n; s += block) {
        float* c[] = { v.data() + s };
        d.process(c, 1, std::min(block, n - s), ms(s));
    }
    return v;
}
}

CM_TEST(channel_delay_is_sample_exact_across_rates) {
    for (double sr : kRates) {
        for (float ms : { 0.02f, 0.5f, 1.0f, 7.3f, 10.0f, 123.45f, 333.33f, 500.0f }) {
            const int expect = static_cast<int>(std::lround(sr * ms / 1000.0));
            const int n = expect + 3000;
            std::vector<float> imp(static_cast<size_t>(n), 0.0f);
            imp[10] = 1.0f;
            for (int block : { 1, 64, 512, 2048 }) {
                ChannelDelay d;
                d.prepare(sr, 2048, 1, 500.0f);
                const auto y = runDelay(sr, imp, block, [&](int) { return ms; }, &d);
                CM_EXPECT(d.getDelaySamples() == expect);
                CM_EXPECT(!d.isGliding());
                std::vector<float> want(static_cast<size_t>(n), 0.0f);
                want[static_cast<size_t>(10 + expect)] = 1.0f;
                CM_EXPECT(bitEqual(y, want));
            }
            const auto nz = noise(n, 3);
            CM_EXPECT(isExactDelay(runDelay(sr, nz, 333, [&](int) { return ms; }), nz, expect, 0, n));
        }
    }
}

CM_TEST(channel_delay_multichannel_and_zero_ms_pass_through) {
    const double sr = 48000.0;
    ChannelDelay d;
    d.prepare(sr, 256, 4, 50.0f);
    auto a = noise(4096, 1), b = noise(4096, 2), c = noise(4096, 3), e = noise(4096, 4);
    const auto a0 = a, b0 = b, c0 = c, e0 = e;
    for (int s = 0; s < 4096; s += 256) {
        float* ch[] = { a.data() + s, b.data() + s, c.data() + s, e.data() + s };
        d.process(ch, 3, 256, 2.0f);   // only 3 of 4 channels processed
    }
    CM_EXPECT(isExactDelay(a, a0, 96, 0, 4096) && isExactDelay(b, b0, 96, 0, 4096) && isExactDelay(c, c0, 96, 0, 4096));
    CM_EXPECT(bitEqual(e, e0));
    const auto in = noise(8192, 5);
    CM_EXPECT(bitEqual(runDelay(sr, in, 100, [](int) { return 0.0f; }), in));
    CM_EXPECT(bitEqual(runDelay(sr, in, 100, [](int) { return 0.004f; }), in));   // rounds to 0 samples
}

CM_TEST(channel_delay_glides_click_free_and_lands_exactly) {
    for (double sr : kRates) {
        const int q = static_cast<int>(1.2 * sr);   // segment length > longest glide (4 * 247 ms)
        const int n = 6 * q;
        const auto tone = sine(sr, 200.0, 0.5, n);
        auto sched = [&](int s) {
            switch (s / q) { case 0: return 0.0f; case 1: return 10.0f; case 2: return 250.0f; case 3: return 3.0f; case 4: return 0.0f; default: return 1.37f; }
        };
        ChannelDelay d;
        d.prepare(sr, 2048, 1, 500.0f);
        const auto y = runDelay(sr, tone, 64, sched, &d);
        CM_EXPECT(allFinite(y));
        CM_EXPECT(clickLevel(y, sr, 200.0) < 0.01);
        // each segment ends settled and exact (glides last max(30 ms, 4 * |change|), here < 1 s)
        const int d10 = static_cast<int>(std::lround(sr * 0.010)), d250 = static_cast<int>(std::lround(sr * 0.250)),
                  d3 = static_cast<int>(std::lround(sr * 0.003)), d137 = static_cast<int>(std::lround(sr * 0.00137));
        CM_EXPECT(isExactDelay(y, tone, d10, 2 * q - 1000, 2 * q));
        CM_EXPECT(isExactDelay(y, tone, d250, 3 * q - 1000, 3 * q));
        CM_EXPECT(isExactDelay(y, tone, d3, 4 * q - 1000, 4 * q));
        CM_EXPECT(isExactDelay(y, tone, 0, 5 * q - 1000, 5 * q));
        CM_EXPECT(isExactDelay(y, tone, d137, 6 * q - 1000, 6 * q));
        CM_EXPECT(d.getDelaySamples() == d137);
        // a glide never changes the playback speed by more than 25 %: |dy/dt| bounded by 1.25 x the tone's slope
        double maxStep = 0.0;
        for (size_t i = 1; i < y.size(); ++i) maxStep = std::max(maxStep, std::fabs(static_cast<double>(y[i]) - y[i - 1]));
        CM_EXPECT(maxStep <= 1.26 * 0.5 * 2.0 * kPiD * 200.0 / sr + 1e-6);
    }
}

CM_TEST(channel_delay_block_independent_reset_deterministic_robust) {
    for (double sr : kRates) {
        const auto in = noise(16384, 7);
        auto sched = [](int s) { return s < 4096 ? 2.0f : (s < 8192 ? 20.0f : (s < 12288 ? 0.0f : 7.0f)); };
        const auto ref = runDelay(sr, in, 2048, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runDelay(sr, in, b, sched), ref) < 1e-5);
        ChannelDelay d;
        d.prepare(sr, 16384, 1, 100.0f);
        const auto big = runDelay(sr, in, 16384, [](int) { return 31.0f; }, &d);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(bitEqual(runDelay(sr, in, b, [](int) { return 31.0f; }), big));
        d.reset();
        CM_EXPECT(bitEqual(runDelay(sr, in, 16384, [](int) { return 31.0f; }, &d), big));
        ChannelDelay e;
        e.prepare(sr, 1000, 1, 500.0f);
        const auto a1 = runDelay(sr, in, 1000, sched, &e);
        e.reset();
        CM_EXPECT(bitEqual(runDelay(sr, in, 1000, sched, &e), a1));
    }
    auto bad = noise(48000, 8);
    bad[10] = std::numeric_limits<float>::quiet_NaN();
    bad[20] = std::numeric_limits<float>::infinity();
    bad[30] = -1.0e30f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ChannelDelay d;
    d.prepare(48000.0, 512, 1, 20.0f);
    const auto y = runDelay(48000.0, bad, 512, [&](int s) {
        const int k = (s / 512) % 4;
        return k == 0 ? nan : (k == 1 ? 1.0e9f : (k == 2 ? -5.0f : std::numeric_limits<float>::infinity()));
    }, &d);
    CM_EXPECT(allFinite(y));
    CM_EXPECT(d.getDelaySamples() <= 960);
    ChannelDelay z;
    z.prepare(48000.0, 64, 1, 0.0f);   // no delay memory: always a pass-through
    const auto zin = noise(1000, 9);
    CM_EXPECT(bitEqual(runDelay(48000.0, zin, 64, [](int) { return 100.0f; }, &z), zin));
}
