// Tests: Biquad designers, SlopeFilter, EqBank, AllpassPhase.
#include "TestHarness.h"
#include "Core/ParamTables.h"
#include "DSP/AllpassPhase.h"
#include "DSP/Biquad.h"
#include "DSP/EqBank.h"
#include "DSP/SlopeFilter.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <vector>

using namespace crowdmike;
using namespace crowdmike::dsp;
using namespace crowdmike::params;

namespace {
constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
constexpr double kPiD = 3.14159265358979323846;

std::vector<float> sine(double sr, double hz, double amp, int n) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPiD * hz * i / sr));
    return v;
}
std::vector<float> noise(int n, unsigned seed, float amp = 0.5f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = d(rng);
    return v;
}
// Single-bin DFT amplitude of y[start, start+len) at hz.
double toneAmp(const std::vector<float>& y, double sr, double hz, int start, int len) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < len; ++i) {
        const double ph = 2.0 * kPiD * hz * (start + i) / sr;
        re += y[static_cast<size_t>(start + i)] * std::cos(ph);
        im += y[static_cast<size_t>(start + i)] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / len;
}
double toneDb(const std::vector<float>& y, double sr, double hz, int start, int len, double ampIn) {
    return 20.0 * std::log10(std::max(toneAmp(y, sr, hz, start, len), 1.0e-12) / ampIn);
}
double tonePhase(const std::vector<float>& y, double sr, double hz, int start, int len) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < len; ++i) {
        const double ph = 2.0 * kPiD * hz * (start + i) / sr;
        re += y[static_cast<size_t>(start + i)] * std::cos(ph);
        im += y[static_cast<size_t>(start + i)] * std::sin(ph);
    }
    return std::atan2(re, im);   // phase relative to sin()
}
// Click detector: energy far above the test tone. The output is high-passed (8-pole Butterworth at 6x the tone
// frequency, which removes the tone by > 120 dB) and the largest residual magnitude after `from` is returned.
// Smooth gain/phase modulation stays near the tone; a step discontinuity of size d shows up as roughly d.
double clickLevel(const std::vector<float>& y, double sr, double toneHz, int from = 2048) {
    std::vector<Biquad> hp(4);
    for (int k = 0; k < 4; ++k) hp[static_cast<size_t>(k)].setCoeffs(designHighpass(sr, static_cast<float>(6.0 * toneHz), static_cast<float>(SlopeFilter::butterworthQ(8, k))));
    double m = 0.0;
    for (size_t i = 0; i < y.size(); ++i) {
        float v = y[i];
        for (auto& b : hp) v = b.process(v);
        if (static_cast<int>(i) >= from) m = std::max(m, static_cast<double>(std::fabs(v)));
    }
    return m;
}
constexpr double kClickTol = 0.01;   // -34 dB re the 0.5-amplitude test tone
// Same criterion relative to the loudest output sample (for processors with large gain changes).
double clickTolFor(const std::vector<float>& y) {
    double pk = 0.0;
    for (float v : y) pk = std::max(pk, static_cast<double>(std::fabs(v)));
    return std::max(kClickTol, 0.02 * pk);
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
// Runs `fn(ptr, n, startSample)` over a mono buffer in blocks of `block`.
using MonoFn = std::function<void(float*, int, int)>;
void runBlocks(std::vector<float>& v, int block, const MonoFn& fn) {
    const int n = static_cast<int>(v.size());
    for (int s = 0; s < n; s += block) fn(v.data() + s, std::min(block, n - s), s);
}
double butterAttenDb(int poles, double ratio) { return -10.0 * std::log10(1.0 + std::pow(ratio, 2.0 * poles)); }

// SlopeFilter mono runner with a per-sample-index parameter schedule.
std::vector<float> runSlope(double sr, bool hp, const std::vector<float>& in, int block,
                            const std::function<void(int, bool&, float&, int&)>& sched) {
    SlopeFilter f;
    f.prepare(sr, 8192, 1, hp);
    auto v = in;
    runBlocks(v, block, [&](float* d, int n, int s) {
        bool en = true; float hz = 100.0f; int slope = 0;
        sched(s, en, hz, slope);
        float* chans[] = { d };
        f.process(chans, 1, n, en, hz, slope);
    });
    return v;
}

struct EqParams {
    float v[8 * kEqBandStride] {};
    void band(int b, bool on, int type, float hz, float gain, float q) {
        float* p = v + b * kEqBandStride;
        p[EqOn] = on ? 1.0f : 0.0f; p[EqType] = static_cast<float>(type); p[EqHz] = hz; p[EqGain] = gain; p[EqQ] = q;
    }
};
}

// ------------------------------------------------------------------------------------------------ Biquad
CM_TEST(biquad_designs_hit_their_targets) {
    for (double sr : kRates) {
        CM_EXPECT_NEAR(biquadMagnitudeDb(designBell(sr, 1000.0f, 1.0f, 6.0f), sr, 1000.0), 6.0, 0.01);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designBell(sr, 1000.0f, 1.0f, -12.0f), sr, 1000.0), -12.0, 0.01);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designBell(sr, 1000.0f, 1.0f, 6.0f), sr, 20.0), 0.0, 0.05);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designNotch(sr, 1000.0f, 10.0f, -24.0f), sr, 1000.0), -24.0, 0.01);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designNotch(sr, 1000.0f, 10.0f, 0.0f), sr, 700.0), 0.0, 1e-6);
        const auto ls = designLowShelf(sr, 100.0f, 0.7071f, 6.0f);
        CM_EXPECT_NEAR(biquadMagnitudeDb(ls, sr, 5.0), 6.0, 0.15);
        CM_EXPECT_NEAR(biquadMagnitudeDb(ls, sr, 100.0), 3.0, 0.05);
        CM_EXPECT_NEAR(biquadMagnitudeDb(ls, sr, 10000.0), 0.0, 0.05);
        const auto hs = designHighShelf(sr, 4000.0f, 0.7071f, -9.0f);
        CM_EXPECT_NEAR(biquadMagnitudeDb(hs, sr, 4000.0), -4.5, 0.05);
        CM_EXPECT_NEAR(biquadMagnitudeDb(hs, sr, 0.499 * sr), -9.0, 0.3);
        CM_EXPECT_NEAR(biquadMagnitudeDb(hs, sr, 50.0), 0.0, 0.05);
        const auto ap = designAllpass(sr, 700.0f, 0.7071f);
        for (double f : { 30.0, 700.0, 5000.0, 15000.0 }) CM_EXPECT_NEAR(biquadMagnitudeDb(ap, sr, f), 0.0, 1e-6);
        CM_EXPECT_NEAR(std::fabs(biquadPhase(ap, sr, 700.0)), kPiD, 1e-6);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designLowpass(sr, 1000.0f, 0.7071f), sr, 1000.0), -3.0103, 0.01);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designHighpass(sr, 1000.0f, 0.7071f), sr, 1000.0), -3.0103, 0.01);
        CM_EXPECT_NEAR(biquadMagnitudeDb(designBandpass(sr, 5000.0f, 1.4f), sr, 5000.0), 0.0, 1e-6);
    }
}

CM_TEST(biquad_processing_matches_design_for_bell_and_shelf) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr);
        struct Case { BiquadCoeffs c; double hz, expect; };
        const Case cases[] = { { designBell(sr, 1000.0f, 2.0f, 9.0f), 1000.0, 9.0 },
                               { designLowShelf(sr, 200.0f, 0.7071f, -6.0f), 20.0, -6.0 },
                               { designHighShelf(sr, 3000.0f, 0.7071f, 6.0f), 3000.0, 3.0 } };
        for (const auto& k : cases) {
            Biquad b;
            b.setCoeffs(k.c);
            auto y = sine(sr, k.hz, 0.25, n);
            b.process(y.data(), n);
            CM_EXPECT_NEAR(toneDb(y, sr, k.hz, n / 2, n / 2, 0.25), biquadMagnitudeDb(k.c, sr, k.hz), 0.05);
            CM_EXPECT_NEAR(toneDb(y, sr, k.hz, n / 2, n / 2, 0.25), k.expect, 0.15);
        }
    }
}

CM_TEST(notch_centre_depth_and_q_accuracy) {
    for (double sr : kRates) {
        for (float q : { 4.0f, 10.0f, 30.0f }) {
            const float f0 = 1000.0f;
            const auto c = designNotch(sr, f0, q, -24.0f);
            // centre depth by processing
            const int n = static_cast<int>(sr);
            Biquad b;
            b.setCoeffs(c);
            auto y = sine(sr, f0, 0.5, n);
            b.process(y.data(), n);
            CM_EXPECT_NEAR(toneDb(y, sr, f0, n / 2, n / 2, 0.5), -24.0, 0.1);
            // -3 dB band edges of a notch of bandwidth f0/Q (|H|^2 = (1 + g^2)/2 at the edges)
            const double bw = f0 / q;
            const double fl = std::sqrt(f0 * f0 + bw * bw / 4.0) - bw / 2.0, fh = fl + bw;
            const double edgeDb = 10.0 * std::log10((1.0 + std::pow(10.0, -2.4)) / 2.0);
            CM_EXPECT_NEAR(biquadMagnitudeDb(c, sr, fl), edgeDb, 0.25);
            CM_EXPECT_NEAR(biquadMagnitudeDb(c, sr, fh), edgeDb, 0.25);
            // well outside the notch: flat
            CM_EXPECT_NEAR(biquadMagnitudeDb(c, sr, f0 * 4.0), 0.0, 0.2);
        }
    }
}

CM_TEST(biquad_hpf_20hz_is_accurate_and_stable_at_192k) {
    const double sr = 192000.0;
    const auto c = designHighpass(sr, 20.0f, 0.7071f);
    CM_EXPECT_NEAR(biquadMagnitudeDb(c, sr, 20.0), -3.0103, 0.01);
    CM_EXPECT_NEAR(biquadMagnitudeDb(c, sr, 10.0), butterAttenDb(2, 2.0), 0.1);
    Biquad b;
    b.setCoeffs(c);
    const int n = static_cast<int>(sr * 4);
    auto y = sine(sr, 20.0, 0.5, n);
    b.process(y.data(), n);
    CM_EXPECT_NEAR(toneDb(y, sr, 20.0, n / 2, n / 2, 0.5), -3.0103, 0.05);
    // impulse response decays (stable)
    Biquad imp;
    imp.setCoeffs(designHighpass(sr, 20.0f, 2.5f));
    float last = 1.0f;
    for (int i = 0; i < static_cast<int>(sr * 10); ++i) last = imp.process(i == 0 ? 1.0f : 0.0f);
    CM_EXPECT(std::fabs(last) < 1.0e-9f);
}

CM_TEST(click_detector_flags_a_small_step) {
    for (double sr : kRates) {
        auto y = sine(sr, 300.0, 0.5, static_cast<int>(sr));
        CM_EXPECT(clickLevel(y, sr, 300.0) < 1.0e-4);
        for (size_t i = y.size() / 2; i < y.size(); ++i) y[i] += 0.02f;   // 0.02 step (-28 dB)
        CM_EXPECT(clickLevel(y, sr, 300.0) > kClickTol);
    }
}

// ------------------------------------------------------------------------------------------------ SlopeFilter
CM_TEST(slope_filter_butterworth_qs) {
    CM_EXPECT_NEAR(SlopeFilter::butterworthQ(2, 0), 0.70711, 1e-4);
    CM_EXPECT_NEAR(SlopeFilter::butterworthQ(4, 0), 0.54120, 1e-4);
    CM_EXPECT_NEAR(SlopeFilter::butterworthQ(4, 1), 1.30656, 1e-4);
    CM_EXPECT_NEAR(SlopeFilter::butterworthQ(8, 3), 2.56292, 1e-4);
}

CM_TEST(slope_filter_attenuation_per_slope) {
    for (double sr : kRates) {
        const int n = static_cast<int>(0.75 * sr), start = static_cast<int>(0.25 * sr), len = static_cast<int>(0.5 * sr);
        for (int slope = 0; slope < 4; ++slope) {
            const int poles = 2 * (slope + 1);
            auto measure = [&](bool hp, float fc, double f) {
                const auto y = runSlope(sr, hp, sine(sr, f, 0.5, n), 512, [&](int, bool& en, float& hz, int& s) {
                    en = true; hz = fc; s = slope;
                });
                return toneDb(y, sr, f, start, len, 0.5);
            };
            // High-pass at 200 Hz
            CM_EXPECT_NEAR(measure(true, 200.0f, 200.0), -3.0103, 0.2);
            CM_EXPECT_NEAR(measure(true, 200.0f, 100.0), butterAttenDb(poles, 2.0), 1.5);
            CM_EXPECT_NEAR(measure(true, 200.0f, 4000.0), 0.0, 0.1);
            const double decade = measure(true, 200.0f, 20.0);
            if (slope <= 1) CM_EXPECT_NEAR(decade, butterAttenDb(poles, 10.0), 1.5);
            else CM_EXPECT(decade < -75.0);
            // Low-pass at 400 Hz (octave 800 Hz, decade 4 kHz)
            CM_EXPECT_NEAR(measure(false, 400.0f, 400.0), -3.0103, 0.2);
            CM_EXPECT_NEAR(measure(false, 400.0f, 800.0), butterAttenDb(poles, 2.0), 1.5);
            CM_EXPECT_NEAR(measure(false, 400.0f, 40.0), 0.0, 0.1);
            const double lpDecade = measure(false, 400.0f, 4000.0);
            if (slope <= 1) CM_EXPECT_NEAR(lpDecade, butterAttenDb(poles, 10.0), 1.5);
            else CM_EXPECT(lpDecade < -75.0);
        }
    }
}

CM_TEST(slope_filter_48db_hpf_20hz_at_192k_is_accurate) {
    const double sr = 192000.0;
    const int n = static_cast<int>(3.0 * sr), start = static_cast<int>(1.0 * sr), len = static_cast<int>(2.0 * sr);
    for (double f : { 10.0, 20.0, 1000.0 }) {
        const auto y = runSlope(sr, true, sine(sr, f, 0.5, n), 256, [](int, bool& en, float& hz, int& s) {
            en = true; hz = 20.0f; s = 3;
        });
        CM_EXPECT(allFinite(y));
        const double expect = f == 10.0 ? butterAttenDb(8, 2.0) : (f == 20.0 ? -3.0103 : 0.0);
        CM_EXPECT_NEAR(toneDb(y, sr, f, start, len, 0.5), expect, f == 10.0 ? 1.0 : 0.15);
    }
}

CM_TEST(slope_filter_bypass_is_bit_exact) {
    const double sr = 48000.0;
    const auto in = noise(20000, 11);
    // disabled from the start
    auto y = runSlope(sr, true, in, 333, [](int, bool& en, float& hz, int& s) { en = false; hz = 300.0f; s = 2; });
    CM_EXPECT(bitEqual(y, in));
    // enabled then disabled: after the fade settles the output is the input again
    y = runSlope(sr, true, in, 100, [](int s0, bool& en, float& hz, int& s) { en = s0 < 5000; hz = 300.0f; s = 2; });
    bool exact = true;
    for (size_t i = 8000; i < in.size(); ++i) exact = exact && (y[i] == in[i]);
    CM_EXPECT(exact);
}

CM_TEST(slope_filter_glides_slope_and_enable_changes_are_click_free) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr);
        const auto in = sine(sr, 300.0, 0.5, n);

        const int q = n / 8;
        auto y = runSlope(sr, true, in, 64, [&](int s0, bool& en, float& hz, int& s) {
            en = !(s0 >= 5 * q && s0 < 6 * q);
            hz = s0 < q ? 40.0f : (s0 < 3 * q ? 450.0f : 120.0f);
            s = s0 < 2 * q ? 0 : (s0 < 4 * q ? 3 : 1);
        });
        CM_EXPECT(allFinite(y));
        // a step discontinuity of 0.01 would give a second difference >= 0.01
        CM_EXPECT(clickLevel(y, sr, 300.0) < kClickTol);
        auto yl = runSlope(sr, false, in, 64, [&](int s0, bool& en, float& hz, int& s) {
            en = !(s0 >= 5 * q && s0 < 6 * q);
            hz = s0 < q ? 2000.0f : (s0 < 3 * q ? 350.0f : 900.0f);
            s = s0 < 2 * q ? 0 : (s0 < 4 * q ? 3 : 1);
        });
        CM_EXPECT(clickLevel(yl, sr, 300.0) < kClickTol);
    }
}

CM_TEST(slope_filter_block_size_independent_and_reset_deterministic) {
    for (double sr : kRates) {
        const auto in = noise(16384, 7);
        auto sched = [](int s0, bool& en, float& hz, int& s) {
            en = s0 < 12288; hz = s0 < 4096 ? 80.0f : 260.0f; s = s0 < 8192 ? 1 : 3;
        };
        // parameter changes happen on multiples of 4096 samples, so every block size sees them at the same sample
        const auto ref = runSlope(sr, true, in, 2048, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runSlope(sr, true, in, b, sched), ref) < 1e-5);
        auto fixed = [](int, bool& en, float& hz, int& s) { en = true; hz = 150.0f; s = 3; };
        const auto big = runSlope(sr, true, in, 16384, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runSlope(sr, true, in, b, fixed), big) < 1e-5);
        SlopeFilter f;
        f.prepare(sr, 4096, 2, false);
        auto run = [&]() {
            auto l = in, r = in;
            for (int s = 0; s < 16384; s += 1000) {
                const int nn = std::min(1000, 16384 - s);
                float* chans[] = { l.data() + s, r.data() + s };
                f.process(chans, 2, nn, true, s < 8000 ? 3000.0f : 6000.0f, 2);
            }
            return l;
        };
        const auto a = run();
        f.reset();
        CM_EXPECT(bitEqual(run(), a));
    }
}

CM_TEST(slope_filter_survives_nan_inf_and_extreme_params) {
    for (double sr : { 44100.0, 192000.0 }) {
        auto in = noise(8192, 3);
        in[100] = std::numeric_limits<float>::quiet_NaN();
        in[200] = std::numeric_limits<float>::infinity();
        in[300] = -1.0e30f;
        in[400] = 1.0e30f;
        const float hzs[] = { 0.0f, -5.0f, 1.0e9f, std::numeric_limits<float>::quiet_NaN(), 20.0f };
        for (float h : hzs) {
            for (bool hp : { true, false }) {
                auto y = runSlope(sr, hp, in, 128, [&](int s0, bool& en, float& hz, int& s) {
                    en = true; hz = s0 < 4096 ? h : 1000.0f; s = s0 < 2048 ? 99 : -3;
                });
                CM_EXPECT(allFinite(y));
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ EqBank
namespace {
std::vector<float> runEq(double sr, const std::vector<float>& in, int block, int nb,
                         const std::function<void(int, EqParams&)>& sched, int channels = 1) {
    EqBank eq;
    eq.prepare(sr, 8192, channels, 8);
    auto v = in;
    EqParams p;
    runBlocks(v, block, [&](float* d, int n, int s) {
        sched(s, p);
        float* chans[] = { d };
        eq.process(chans, 1, n, p.v, nb);
    });
    return v;
}
}

CM_TEST(eqbank_band_gains_and_notch_are_accurate) {
    for (double sr : kRates) {
        const int n = static_cast<int>(0.5 * sr), start = n / 2, len = n / 2;
        auto sched = [](int, EqParams& p) {
            p.band(0, true, EqLowShelf, 100.0f, 6.0f, 0.7071f);
            p.band(1, true, EqBell, 1000.0f, -9.0f, 4.0f);
            p.band(2, true, EqNotch, 3000.0f, 18.0f, 20.0f);   // notch uses -|Gain|
            p.band(3, true, EqHighShelf, 10000.0f, 4.0f, 0.7071f);
        };
        auto expectAt = [&](double f) {
            double e = 0.0;
            e += biquadMagnitudeDb(designLowShelf(sr, 100.0f, 0.7071f, 6.0f), sr, f);
            e += biquadMagnitudeDb(designBell(sr, 1000.0f, 4.0f, -9.0f), sr, f);
            e += biquadMagnitudeDb(designNotch(sr, 3000.0f, 20.0f, -18.0f), sr, f);
            e += biquadMagnitudeDb(designHighShelf(sr, 10000.0f, 0.7071f, 4.0f), sr, f);
            return e;
        };
        for (double f : { 20.0, 1000.0, 3000.0, 18000.0 }) {
            const auto y = runEq(sr, sine(sr, f, 0.25, n), 256, 4, sched);
            CM_EXPECT_NEAR(toneDb(y, sr, f, start, len, 0.25), expectAt(f), 0.05);
        }
        CM_EXPECT_NEAR(expectAt(1000.0), -9.0, 0.3);
        CM_EXPECT_NEAR(expectAt(3000.0), -18.0, 0.3);
    }
}

CM_TEST(eqbank_all_off_or_null_params_is_bit_exact) {
    const auto in = noise(10000, 5);
    auto y = runEq(48000.0, in, 256, 8, [](int, EqParams& p) {
        for (int b = 0; b < 8; ++b) p.band(b, false, EqBell, 1000.0f, 12.0f, 1.0f);
    });
    CM_EXPECT(bitEqual(y, in));
    EqBank eq;
    eq.prepare(48000.0, 512, 1, 4);
    auto v = in;
    runBlocks(v, 500, [&](float* d, int n, int) { float* c[] = { d }; eq.process(c, 1, n, nullptr, 4); });
    CM_EXPECT(bitEqual(v, in));
    // switched on, then off again -> bit exact once the fade has settled
    y = runEq(48000.0, in, 100, 4, [](int s, EqParams& p) { p.band(2, s < 3000, EqBell, 1000.0f, 12.0f, 1.0f); });
    bool exact = true;
    for (size_t i = 5000; i < in.size(); ++i) exact = exact && (y[i] == in[i]);
    CM_EXPECT(exact);
}

CM_TEST(eqbank_bypass_type_and_parameter_changes_are_click_free) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr);
        const auto in = sine(sr, 400.0, 0.5, n);

        const int q = n / 10;
        auto y = runEq(sr, in, 64, 2, [&](int s, EqParams& p) {
            const bool on = !(s >= q && s < 2 * q);
            const int type = s < 3 * q ? EqBell : (s < 5 * q ? EqNotch : EqLowShelf);
            const float hz = s < 6 * q ? 400.0f : 2000.0f;
            const float gain = s < 7 * q ? 12.0f : -12.0f;
            const float qq = s < 8 * q ? 2.0f : 0.3f;
            p.band(0, on, type, hz, gain, qq);
            p.band(1, s > 4 * q, EqHighShelf, 1000.0f, 9.0f, 0.7f);
        });
        CM_EXPECT(allFinite(y));
        CM_EXPECT(clickLevel(y, sr, 400.0) < clickTolFor(y));
    }
}

CM_TEST(eqbank_block_size_independent_reset_deterministic_and_robust) {
    for (double sr : kRates) {
        const auto in = noise(16384, 9);
        auto sched = [](int s, EqParams& p) {
            p.band(0, true, s < 4096 ? EqBell : EqNotch, s < 8192 ? 500.0f : 900.0f, 9.0f, 3.0f);
            p.band(1, s >= 2048, EqHighShelf, 6000.0f, -6.0f, 0.7f);
            p.band(2, true, EqLowShelf, 80.0f, s < 12288 ? 4.0f : -4.0f, 0.7f);
        };
        const auto ref = runEq(sr, in, 2048, 3, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runEq(sr, in, b, 3, sched), ref) < 1e-5);
        auto fixed = [](int, EqParams& p) {
            p.band(0, true, EqNotch, 700.0f, 12.0f, 8.0f);
            p.band(1, true, EqHighShelf, 6000.0f, -6.0f, 0.7f);
            p.band(2, true, EqBell, 150.0f, 5.0f, 1.0f);
        };
        const auto big = runEq(sr, in, 16384, 3, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runEq(sr, in, b, 3, fixed), big) < 1e-5);
        CM_EXPECT(bitEqual(runEq(sr, in, 777, 3, sched), runEq(sr, in, 777, 3, sched)));
        EqBank eq;
        eq.prepare(sr, 1024, 1, 4);
        EqParams p;
        sched(0, p);
        auto run = [&]() { auto v = in; runBlocks(v, 1024, [&](float* d, int n, int) { float* c[] = { d }; eq.process(c, 1, n, p.v, 3); }); return v; };
        const auto a = run();
        eq.reset();
        CM_EXPECT(bitEqual(run(), a));
    }
    // NaN / Inf / huge input and absurd parameters
    auto in = noise(8192, 1);
    in[10] = std::numeric_limits<float>::quiet_NaN();
    in[20] = -std::numeric_limits<float>::infinity();
    in[30] = 3.0e38f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto y = runEq(192000.0, in, 64, 99, [&](int s, EqParams& p) {
        p.band(0, true, 7, s < 4000 ? nan : 1.0e9f, 1.0e6f, 0.0f);
        p.band(1, true, -2, -100.0f, nan, 1.0e9f);
        p.band(2, true, EqNotch, 20.0f, -1.0e6f, nan);
        p.band(3, true, EqLowShelf, 20.0f, 24.0f, 30.0f);
        p.band(4, true, EqHighShelf, 20000.0f, 24.0f, 30.0f);
    });
    CM_EXPECT(allFinite(y));
}

// ------------------------------------------------------------------------------------------------ AllpassPhase
namespace {
std::vector<float> runAp(double sr, const std::vector<float>& in, int block, const std::function<void(int, int&, float&)>& sched) {
    AllpassPhase ap;
    ap.prepare(sr, 8192, 1);
    auto v = in;
    runBlocks(v, block, [&](float* d, int n, int s) {
        int st = 0; float hz = 500.0f;
        sched(s, st, hz);
        float* c[] = { d };
        ap.process(c, 1, n, st, hz);
    });
    return v;
}
}

CM_TEST(allpass_phase_is_flat_and_rotates_phase) {
    for (double sr : kRates) {
        const int n = static_cast<int>(0.5 * sr), start = n / 2, len = n / 2;
        for (int stages = 1; stages <= 4; ++stages) {
            for (double f : { 100.0, 700.0, 5000.0 }) {
                const auto in = sine(sr, f, 0.5, n);
                const auto y = runAp(sr, in, 512, [&](int, int& st, float& hz) { st = stages; hz = 700.0f; });
                CM_EXPECT_NEAR(toneDb(y, sr, f, start, len, 0.5), 0.0, 0.02);
                if (f == 700.0) {
                    double ph = tonePhase(y, sr, f, start, len) - tonePhase(in, sr, f, start, len);
                    const double expect = -kPiD * stages;
                    double d = std::remainder(ph - expect, 2.0 * kPiD);
                    CM_EXPECT_NEAR(d, 0.0, 0.02);
                }
            }
        }
        const auto in = noise(4096, 2);
        CM_EXPECT(bitEqual(runAp(sr, in, 128, [](int, int& st, float& hz) { st = 0; hz = 300.0f; }), in));
    }
}

CM_TEST(allpass_phase_changes_click_free_block_independent_robust) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr);
        const auto in = sine(sr, 250.0, 0.5, n);

        const int q = n / 6;
        auto sched = [&](int s, int& st, float& hz) {
            st = s < q ? 0 : (s < 2 * q ? 2 : (s < 4 * q ? 4 : (s < 5 * q ? 1 : 0)));
            hz = s < 3 * q ? 200.0f : 2000.0f;
        };
        auto y = runAp(sr, in, 64, sched);
        CM_EXPECT(clickLevel(y, sr, 250.0) < kClickTol);
        auto sched2 = [](int s, int& st, float& hz) { st = s < 4096 ? 3 : 1; hz = s < 8192 ? 300.0f : 900.0f; };
        const auto nz = noise(16384, 4);
        const auto r = runAp(sr, nz, 2048, sched2);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runAp(sr, nz, b, sched2), r) < 1e-5);
        auto fixed = [](int, int& st, float& hz) { st = 3; hz = 400.0f; };
        const auto big = runAp(sr, nz, 16384, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runAp(sr, nz, b, fixed), big) < 1e-5);
    }
    auto in = noise(4096, 8);
    in[5] = std::numeric_limits<float>::quiet_NaN();
    in[6] = 1.0e30f;
    auto y = runAp(192000.0, in, 100, [](int s, int& st, float& hz) {
        st = s < 2000 ? 99 : -1; hz = s < 1000 ? std::numeric_limits<float>::infinity() : 0.0f;
    });
    CM_EXPECT(allFinite(y));
}
