// Tests: Compressor, Gate, DeEsser, TransientEnhancer, GainRider, EnvelopeFollower, DensityFollower.
#include "TestHarness.h"
#include "Core/ParamTables.h"
#include "DSP/Biquad.h"
#include "DSP/Compressor.h"
#include "DSP/DeEsser.h"
#include "DSP/Envelope.h"
#include "DSP/GainRider.h"
#include "DSP/Gate.h"
#include "DSP/TransientEnhancer.h"
#include <cmath>
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
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
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
std::vector<float> concat(std::initializer_list<std::vector<float>> parts) {
    std::vector<float> r;
    for (const auto& p : parts) r.insert(r.end(), p.begin(), p.end());
    return r;
}
std::vector<float> add(const std::vector<float>& a, const std::vector<float>& b) {
    auto r = a;
    for (size_t i = 0; i < r.size(); ++i) r[i] += b[i];
    return r;
}
double toneAmp(const std::vector<float>& y, double sr, double hz, int start, int len) {
    double re = 0.0, im = 0.0;
    for (int i = 0; i < len; ++i) {
        const double ph = 2.0 * kPiD * hz * (start + i) / sr;
        re += y[static_cast<size_t>(start + i)] * std::cos(ph);
        im += y[static_cast<size_t>(start + i)] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / len;
}
double toneDb(const std::vector<float>& y, double sr, double hz, int start, int len) {
    return 20.0 * std::log10(std::max(toneAmp(y, sr, hz, start, len), 1.0e-12));
}
double rmsDb(const std::vector<float>& y, int start, int len) {
    double s = 0.0;
    for (int i = start; i < start + len; ++i) s += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    return 10.0 * std::log10(std::max(s / len, 1.0e-24));
}
double peakAbs(const std::vector<float>& y, int start, int len) {
    double m = 0.0;
    for (int i = start; i < start + len; ++i) m = std::max(m, static_cast<double>(std::fabs(y[static_cast<size_t>(i)])));
    return m;
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
// Click detector: 8-pole Butterworth HPF at 6x the tone, largest residual after `from` (see FiltersTest).
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

// Generic runner: `fn(chans, nch, n, startSample)` over (up to stereo) buffers in blocks.
using BlockFn = std::function<void(float* const*, int, int, int)>;
std::vector<float> runMono(const std::vector<float>& in, int block, const BlockFn& fn) {
    auto v = in;
    const int n = static_cast<int>(v.size());
    for (int s = 0; s < n; s += block) {
        float* c[] = { v.data() + s };
        fn(c, 1, std::min(block, n - s), s);
    }
    return v;
}
void runStereo(std::vector<float>& l, std::vector<float>& r, int block, const BlockFn& fn) {
    const int n = static_cast<int>(l.size());
    for (int s = 0; s < n; s += block) {
        float* c[] = { l.data() + s, r.data() + s };
        fn(c, 2, std::min(block, n - s), s);
    }
}
std::vector<float> nastyInput(int n, unsigned seed) {
    auto v = noise(n, seed);
    v[50] = kNaN; v[60] = kInf; v[70] = -kInf; v[80] = 1.0e30f; v[90] = -3.0e38f;
    for (int i = 1000; i < 1200; ++i) v[static_cast<size_t>(i)] = 1.0e6f * ((i & 1) ? 1.0f : -1.0f);
    return v;
}

struct CompParams {
    float p[CompP::Count] {};
    CompParams() {
        p[CompP::On] = 1; p[CompP::Thr] = -20; p[CompP::Ratio] = 4; p[CompP::Attack] = 1; p[CompP::Release] = 300;
        p[CompP::Knee] = 0; p[CompP::Makeup] = 0; p[CompP::AutoMakeup] = 0; p[CompP::ScHpf] = 20; p[CompP::Mix] = 1;
    }
};
std::vector<float> runComp(double sr, const std::vector<float>& in, int block, const std::function<void(int, CompParams&)>& sched,
                           Compressor* external = nullptr) {
    Compressor local;
    Compressor& c = external != nullptr ? *external : local;
    if (external == nullptr) c.prepare(sr, 4096, 1);
    CompParams cp;
    return runMono(in, block, [&](float* const* ch, int nch, int n, int s) { sched(s, cp); c.process(ch, nch, n, cp.p); });
}
}

// ------------------------------------------------------------------------------------------------ Compressor
CM_TEST(compressor_gain_computer_formula) {
    CM_EXPECT_NEAR(Compressor::computeGainDb(-30.0f, -20.0f, 4.0f, 0.0f), 0.0, 1e-6);
    CM_EXPECT_NEAR(Compressor::computeGainDb(-10.0f, -20.0f, 4.0f, 0.0f), -7.5, 1e-5);
    CM_EXPECT_NEAR(Compressor::computeGainDb(-20.0f, -20.0f, 4.0f, 12.0f), -1.125, 1e-5);
    CM_EXPECT_NEAR(Compressor::computeGainDb(-26.0f, -20.0f, 4.0f, 12.0f), 0.0, 1e-5);
    CM_EXPECT_NEAR(Compressor::computeGainDb(-14.0f, -20.0f, 4.0f, 12.0f), -4.5, 1e-5);
    CM_EXPECT_NEAR(Compressor::computeGainDb(0.0f, -20.0f, 1.0f, 6.0f), 0.0, 1e-6);
}

CM_TEST(compressor_static_curve_threshold_ratio_and_knee) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr), start = n / 2, len = n / 2;
        for (float knee : { 0.0f, 12.0f }) {
            for (double inDb : { -40.0, -26.0, -20.0, -14.0, -10.0, 0.0 }) {
                Compressor c;
                c.prepare(sr, 512, 1);
                const auto y = runComp(sr, sine(sr, 1000.0, dbToLin(inDb), n), 512,
                                       [&](int, CompParams& p) { p.p[CompP::Knee] = knee; }, &c);
                const double expect = inDb + Compressor::computeGainDb(static_cast<float>(inDb), -20.0f, 4.0f, knee);
                CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, start, len), expect, 0.7);
                CM_EXPECT_NEAR(c.getGainReductionDb(), expect - inDb, 0.7);
            }
        }
        // ratio sweep at a fixed input
        for (float ratio : { 1.0f, 2.0f, 8.0f, 20.0f }) {
            const auto y = runComp(sr, sine(sr, 1000.0, dbToLin(-6.0), n), 256,
                                   [&](int, CompParams& p) { p.p[CompP::Ratio] = ratio; p.p[CompP::Thr] = -30; });
            CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, start, len), -30.0 + 24.0 / ratio, 0.7);
        }
    }
}

CM_TEST(compressor_makeup_auto_makeup_and_parallel_mix) {
    const double sr = 48000.0;
    const int n = static_cast<int>(sr), start = n / 2, len = n / 2;
    // below threshold: only makeup applies
    auto y = runComp(sr, sine(sr, 1000.0, dbToLin(-40.0), n), 512, [](int, CompParams& p) { p.p[CompP::Makeup] = 5; });
    CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, start, len), -35.0, 0.1);
    // auto makeup = -Thr * (1 - 1/R) * 0.5 = 7.5 dB (manual makeup ignored)
    y = runComp(sr, sine(sr, 1000.0, dbToLin(-40.0), n), 512,
                [](int, CompParams& p) { p.p[CompP::AutoMakeup] = 1; p.p[CompP::Makeup] = 12; });
    CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, start, len), -40.0 + 7.5, 0.1);
    // parallel mix 0.5: out = 0.5 * (dry + compressed)
    y = runComp(sr, sine(sr, 1000.0, dbToLin(-10.0), n), 512, [](int, CompParams& p) { p.p[CompP::Mix] = 0.5f; });
    const double expect = 20.0 * std::log10(0.5 * (1.0 + dbToLin(-7.5))) - 10.0;
    CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, start, len), expect, 0.5);
    // mix 0 -> dry
    y = runComp(sr, sine(sr, 1000.0, dbToLin(-10.0), n), 512, [](int, CompParams& p) { p.p[CompP::Mix] = 0.0f; });
    CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, start, len), -10.0, 0.01);
}

CM_TEST(compressor_sidechain_hpf_ignores_low_frequencies) {
    const double sr = 48000.0;
    const int n = static_cast<int>(sr);
    Compressor c;
    c.prepare(sr, 512, 1);
    const auto y = runComp(sr, sine(sr, 40.0, dbToLin(-6.0), n), 512, [](int, CompParams& p) { p.p[CompP::ScHpf] = 1000; }, &c);
    // 40 Hz is ~ -56 dB in a 2nd-order 1 kHz HPF -> no compression
    CM_EXPECT(c.getGainReductionDb() > -0.5f);
    CM_EXPECT_NEAR(toneDb(y, sr, 40.0, n / 2, n / 2), -6.0, 0.3);
}

CM_TEST(compressor_stereo_link_applies_same_gain) {
    const double sr = 48000.0;
    const int n = static_cast<int>(sr);
    Compressor c;
    c.prepare(sr, 512, 2);
    auto l = sine(sr, 1000.0, dbToLin(-6.0), n), r = sine(sr, 1000.0, dbToLin(-40.0), n);
    CompParams cp;
    runStereo(l, r, 512, [&](float* const* ch, int nch, int nn, int) { c.process(ch, nch, nn, cp.p); });
    const double gl = toneDb(l, sr, 1000.0, n / 2, n / 2) + 6.0, gr = toneDb(r, sr, 1000.0, n / 2, n / 2) + 40.0;
    CM_EXPECT_NEAR(gl, gr, 0.05);
    CM_EXPECT_NEAR(gl, -10.5, 0.7);
}

CM_TEST(compressor_bypass_bit_exact_and_transitions_click_free) {
    for (double sr : kRates) {
        const auto in = noise(20000, 21);
        auto y = runComp(sr, in, 300, [](int, CompParams& p) { p.p[CompP::On] = 0; });
        CM_EXPECT(bitEqual(y, in));
        y = runComp(sr, in, 100, [](int s, CompParams& p) { p.p[CompP::On] = s < 5000 ? 1.0f : 0.0f; });
        bool exact = true;
        for (size_t i = 8000; i < in.size(); ++i) exact = exact && y[i] == in[i];
        CM_EXPECT(exact);
        // heavy compression toggled on/off and threshold/ratio/makeup/mix jumps on a 100 Hz tone
        const int n = static_cast<int>(sr);
        const auto tone = sine(sr, 100.0, 0.5, n);
        const int q = n / 8;
        y = runComp(sr, tone, 64, [&](int s, CompParams& p) {
            p.p[CompP::On] = (s < q || (s >= 2 * q && s < 6 * q)) ? 1.0f : 0.0f;
            p.p[CompP::Attack] = 10; p.p[CompP::Release] = 100;
            p.p[CompP::Thr] = s < 3 * q ? -30.0f : -6.0f;
            p.p[CompP::Ratio] = s < 4 * q ? 10.0f : 2.0f;
            p.p[CompP::Makeup] = s < 5 * q ? 12.0f : 0.0f;
            p.p[CompP::Mix] = s < 5 * q ? 1.0f : 0.3f;
        });
        CM_EXPECT(allFinite(y));
        CM_EXPECT(clickLevel(y, sr, 100.0) < 0.01);
    }
}

CM_TEST(compressor_block_size_independent_reset_deterministic_robust) {
    for (double sr : kRates) {
        const auto in = noise(16384, 33, 0.9f);
        auto sched = [](int s, CompParams& p) {
            p.p[CompP::Thr] = s < 4096 ? -30.0f : -12.0f;
            p.p[CompP::Knee] = s < 8192 ? 6.0f : 0.0f;
            p.p[CompP::Mix] = s < 12288 ? 1.0f : 0.6f;
            p.p[CompP::Attack] = 5; p.p[CompP::Release] = 80;
        };
        const auto ref = runComp(sr, in, 2048, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runComp(sr, in, b, sched), ref) < 1e-5);
        auto fixed = [](int, CompParams& p) { p.p[CompP::Thr] = -24; p.p[CompP::Knee] = 6; };
        const auto big = runComp(sr, in, 16384, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runComp(sr, in, b, fixed), big) < 1e-5);
        Compressor c;
        c.prepare(sr, 1000, 1);
        const auto a = runComp(sr, in, 1000, sched, &c);
        c.reset();
        CM_EXPECT(bitEqual(runComp(sr, in, 1000, sched, &c), a));
    }
    const auto bad = nastyInput(8192, 4);
    for (int variant = 0; variant < 3; ++variant) {
        const auto y = runComp(192000.0, bad, 64, [&](int s, CompParams& p) {
            const float x = variant == 0 ? kNaN : (variant == 1 ? 1.0e9f : -1.0e9f);
            for (int k = 1; k < CompP::Count; ++k) p.p[k] = (s / 64) % 2 ? x : 0.0f;
            p.p[CompP::On] = 1;
        });
        CM_EXPECT(allFinite(y));
    }
    // nullptr params: treated as off
    Compressor c;
    c.prepare(48000.0, 64, 1);
    auto v = noise(64, 1);
    const auto v0 = v;
    float* ch[] = { v.data() };
    c.process(ch, 1, 64, nullptr);
    CM_EXPECT(bitEqual(v, v0));
}

// ------------------------------------------------------------------------------------------------ Gate
namespace {
struct GateParams {
    float p[GateP::Count] {};
    GateParams() {
        p[GateP::On] = 1; p[GateP::Thr] = -40; p[GateP::Range] = -30; p[GateP::Ratio] = 20; p[GateP::Attack] = 1;
        p[GateP::Hold] = 0; p[GateP::Release] = 20; p[GateP::Hyst] = 6; p[GateP::ScHpf] = 100; p[GateP::ScLpf] = 12000;
    }
};
}

CM_TEST(gate_open_closed_and_range) {
    for (double sr : kRates) {
        const int n = static_cast<int>(0.5 * sr);
        for (double lvl : { -20.0, -70.0 }) {
            Gate g;
            g.prepare(sr, 256, 1);
            GateParams gp;
            const auto y = runMono(sine(sr, 1000.0, dbToLin(lvl), n), 256,
                                   [&](float* const* c, int nch, int nn, int) { g.process(c, nch, nn, gp.p); });
            const double expect = lvl > -40.0 ? 0.0 : -30.0;
            CM_EXPECT_NEAR(g.getGainReductionDb(), expect, 0.3);
            CM_EXPECT_NEAR(toneDb(y, sr, 1000.0, n / 2, n / 2), lvl + expect, 0.3);
            CM_EXPECT(g.isOpen() == (lvl > -40.0));
        }
        // ratio 2 expander: 5 dB below the threshold -> 5 dB extra attenuation
        Gate g;
        g.prepare(sr, 256, 1);
        GateParams gp;
        gp.p[GateP::Ratio] = 2;
        gp.p[GateP::Hyst] = 0;
        runMono(sine(sr, 1000.0, dbToLin(-45.0), n), 256, [&](float* const* c, int nch, int nn, int) { g.process(c, nch, nn, gp.p); });
        CM_EXPECT_NEAR(g.getGainReductionDb(), -5.0, 0.3);
    }
}

CM_TEST(gate_hysteresis_and_hold) {
    for (double sr : kRates) {
        Gate g;
        g.prepare(sr, 64, 1);
        GateParams gp;
        const int seg = static_cast<int>(0.3 * sr);
        auto feed = [&](double lvl) {
            auto v = sine(sr, 1000.0, dbToLin(lvl), seg);
            runMono(v, 64, [&](float* const* c, int nch, int nn, int) { g.process(c, nch, nn, gp.p); });
        };
        feed(-60.0); CM_EXPECT(!g.isOpen()); CM_EXPECT_NEAR(g.getGainReductionDb(), -30.0, 0.3);
        feed(-43.0); CM_EXPECT(!g.isOpen());                        // between close (-46) and open (-40): stays closed
        feed(-30.0); CM_EXPECT(g.isOpen()); CM_EXPECT_NEAR(g.getGainReductionDb(), 0.0, 0.1);
        feed(-43.0); CM_EXPECT(g.isOpen()); CM_EXPECT_NEAR(g.getGainReductionDb(), 0.0, 0.1);   // stays open
        feed(-50.0); CM_EXPECT(!g.isOpen()); CM_EXPECT_NEAR(g.getGainReductionDb(), -30.0, 0.3);

        // hold: 200 ms after the level falls below the close threshold the gate is still open
        Gate h;
        h.prepare(sr, 48, 1);
        GateParams hp;
        hp.p[GateP::Hold] = 200; hp.p[GateP::Hyst] = 3; hp.p[GateP::Release] = 10;
        const int on = static_cast<int>(0.3 * sr), off = static_cast<int>(0.8 * sr);
        const auto in = concat({ sine(sr, 1000.0, dbToLin(-20.0), on), std::vector<float>(static_cast<size_t>(off), 0.0f) });
        std::vector<float> gr;
        runMono(in, 48, [&](float* const* c, int nch, int nn, int) { h.process(c, nch, nn, hp.p); gr.push_back(h.getGainReductionDb()); });
        auto grAt = [&](double secAfterDrop) { return gr[static_cast<size_t>((on + secAfterDrop * sr) / 48)]; };
        // detector (30 ms release) needs ~80 ms to fall 23 dB; + 200 ms hold
        CM_EXPECT(grAt(0.20) > -0.1f);
        CM_EXPECT(grAt(0.25) > -0.1f);
        CM_EXPECT_NEAR(grAt(0.6), -30.0, 0.5);
        // without hold the gate is closed long before
        Gate nh;
        nh.prepare(sr, 48, 1);
        hp.p[GateP::Hold] = 0;
        gr.clear();
        runMono(in, 48, [&](float* const* c, int nch, int nn, int) { nh.process(c, nch, nn, hp.p); gr.push_back(nh.getGainReductionDb()); });
        CM_EXPECT(grAt(0.25) < -20.0f);
    }
}

CM_TEST(gate_bypass_click_free_block_independent_robust) {
    for (double sr : kRates) {
        const auto in = noise(20000, 41, 0.02f);
        auto runG = [&](const std::vector<float>& x, int block, const std::function<void(int, GateParams&)>& sched) {
            Gate g;
            g.prepare(sr, 4096, 1);
            GateParams gp;
            return runMono(x, block, [&](float* const* c, int nch, int nn, int s) { sched(s, gp); g.process(c, nch, nn, gp.p); });
        };
        CM_EXPECT(bitEqual(runG(in, 128, [](int, GateParams& p) { p.p[GateP::On] = 0; }), in));
        // On toggles with the gate closed (range -30): click-free
        const int n = static_cast<int>(sr);
        const auto tone = sine(sr, 100.0, 0.5, n);
        auto y = runG(tone, 64, [&](int s, GateParams& p) {
            p.p[GateP::Thr] = 0; p.p[GateP::On] = (s / (n / 6)) % 2 ? 1.0f : 0.0f; p.p[GateP::ScHpf] = 20;
        });
        CM_EXPECT(clickLevel(y, sr, 100.0) < 0.01);
        auto sched = [](int s, GateParams& p) { p.p[GateP::Thr] = s < 4096 ? -20.0f : -50.0f; p.p[GateP::Hold] = 10; };
        const auto ref = runG(in, 2048, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runG(in, b, sched), ref) < 1e-5);
        auto fixed = [](int, GateParams& p) { p.p[GateP::Thr] = -30; };
        const auto big = runG(in, 20000, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runG(in, b, fixed), big) < 1e-5);
        CM_EXPECT(bitEqual(runG(in, 333, sched), runG(in, 333, sched)));
    }
    Gate g;
    g.prepare(96000.0, 64, 2);
    auto l = nastyInput(4096, 2), r = nastyInput(4096, 3);
    GateParams gp;
    runStereo(l, r, 64, [&](float* const* c, int nch, int nn, int s) {
        for (int k = 1; k < GateP::Count; ++k) gp.p[k] = (s / 64) % 3 == 0 ? kNaN : ((s / 64) % 3 == 1 ? 1.0e9f : -1.0e9f);
        gp.p[GateP::On] = 1;
        g.process(c, nch, nn, gp.p);
    });
    CM_EXPECT(allFinite(l) && allFinite(r));
}

// ------------------------------------------------------------------------------------------------ DeEsser
namespace {
struct DeEssParams {
    float p[DeEssP::Count] {};
    DeEssParams() {
        p[DeEssP::On] = 1; p[DeEssP::Freq] = 7000; p[DeEssP::Thr] = -24; p[DeEssP::Ratio] = 4; p[DeEssP::Range] = 12;
        p[DeEssP::Attack] = 1; p[DeEssP::Release] = 60; p[DeEssP::Listen] = 0; p[DeEssP::Mode] = 0;
    }
};
std::vector<float> runDe(double sr, const std::vector<float>& in, int block, const std::function<void(int, DeEssParams&)>& sched,
                         DeEsser* ext = nullptr) {
    DeEsser local;
    DeEsser& d = ext != nullptr ? *ext : local;
    if (ext == nullptr) d.prepare(sr, 4096, 1);
    DeEssParams dp;
    return runMono(in, block, [&](float* const* c, int nch, int nn, int s) { sched(s, dp); d.process(c, nch, nn, dp.p); });
}
}

CM_TEST(deesser_detects_the_sibilant_band_only) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr), start = n / 2, len = n / 2;
        const auto low = sine(sr, 200.0, dbToLin(-6.0), n), sib = sine(sr, 7000.0, dbToLin(-6.0), n);
        // 200 Hz alone: untouched, no gain reduction
        DeEsser d;
        d.prepare(sr, 512, 1);
        auto y = runDe(sr, low, 512, [](int, DeEssParams&) {}, &d);
        CM_EXPECT(d.getGainReductionDb() > -0.1f);
        CM_EXPECT_NEAR(toneDb(y, sr, 200.0, start, len), -6.0, 0.05);
        // 7 kHz sibilant: 18 dB over threshold * (1 - 1/4) = 13.5 -> limited by the 12 dB range
        DeEsser d2;
        d2.prepare(sr, 512, 1);
        y = runDe(sr, sib, 512, [](int, DeEssParams&) {}, &d2);
        CM_EXPECT_NEAR(d2.getGainReductionDb(), -12.0, 0.7);
        CM_EXPECT_NEAR(toneDb(y, sr, 7000.0, start, len), -18.0, 1.0);
        // both: split-band reduces 7 kHz, leaves 200 Hz alone
        const auto both = add(low, sib);
        y = runDe(sr, both, 512, [](int, DeEssParams&) {});
        CM_EXPECT_NEAR(toneDb(y, sr, 200.0, start, len), -6.0, 0.2);
        CM_EXPECT(toneDb(y, sr, 7000.0, start, len) < -15.0);
        // wideband: everything is reduced
        y = runDe(sr, both, 512, [](int, DeEssParams& p) { p.p[DeEssP::Mode] = 1; });
        CM_EXPECT_NEAR(toneDb(y, sr, 200.0, start, len), -18.0, 1.0);
        CM_EXPECT_NEAR(toneDb(y, sr, 7000.0, start, len), -18.0, 1.0);
        // listen: only the detector band is heard
        y = runDe(sr, both, 512, [](int, DeEssParams& p) { p.p[DeEssP::Listen] = 1; });
        CM_EXPECT_NEAR(toneDb(y, sr, 7000.0, start, len), -6.0, 0.2);
        CM_EXPECT(toneDb(y, sr, 200.0, start, len) < -35.0);
        // detector tuned elsewhere (3 kHz) barely reacts to 200 Hz and only partly to 7 kHz
        DeEsser d3;
        d3.prepare(sr, 512, 1);
        runDe(sr, low, 512, [](int, DeEssParams& p) { p.p[DeEssP::Freq] = 3000; }, &d3);
        CM_EXPECT(d3.getGainReductionDb() > -0.1f);
    }
}

CM_TEST(deesser_bypass_click_free_block_independent_robust) {
    for (double sr : kRates) {
        const auto in = noise(16384, 51);
        CM_EXPECT(bitEqual(runDe(sr, in, 256, [](int, DeEssParams& p) { p.p[DeEssP::On] = 0; }), in));
        const int n = static_cast<int>(sr);
        const auto tone = sine(sr, 100.0, 0.4, n);
        auto y = runDe(sr, tone, 64, [&](int s, DeEssParams& p) {
            const int k = s / (n / 8);
            p.p[DeEssP::On] = k % 2 ? 0.0f : 1.0f;
            p.p[DeEssP::Listen] = k == 2 ? 1.0f : 0.0f;
            p.p[DeEssP::Mode] = k >= 4 ? 1.0f : 0.0f;
            p.p[DeEssP::Freq] = k >= 6 ? 3000.0f : 7000.0f;
            p.p[DeEssP::Thr] = -60;
        });
        CM_EXPECT(allFinite(y));
        CM_EXPECT(clickLevel(y, sr, 100.0) < 0.01);
        auto sched = [](int s, DeEssParams& p) {
            p.p[DeEssP::Freq] = s < 4096 ? 5000.0f : 9000.0f; p.p[DeEssP::Thr] = -40; p.p[DeEssP::Mode] = s < 8192 ? 0.0f : 1.0f;
        };
        const auto ref = runDe(sr, in, 2048, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runDe(sr, in, b, sched), ref) < 1e-5);
        auto fixed = [](int, DeEssParams& p) { p.p[DeEssP::Thr] = -40; };
        const auto big = runDe(sr, in, 16384, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runDe(sr, in, b, fixed), big) < 1e-5);
        DeEsser d;
        d.prepare(sr, 1024, 1);
        const auto a = runDe(sr, in, 1024, sched, &d);
        d.reset();
        CM_EXPECT(bitEqual(runDe(sr, in, 1024, sched, &d), a));
    }
    const auto bad = nastyInput(8192, 6);
    const auto y = runDe(44100.0, bad, 100, [](int s, DeEssParams& p) {
        for (int k = 1; k < DeEssP::Count; ++k) p.p[k] = (s / 100) % 3 == 0 ? kNaN : ((s / 100) % 3 == 1 ? 1.0e9f : -1.0e9f);
        p.p[DeEssP::On] = 1;
    });
    CM_EXPECT(allFinite(y));
}

// ------------------------------------------------------------------------------------------------ TransientEnhancer
namespace {
struct TransParams {
    float p[TransP::Count] {};
    TransParams() { p[TransP::On] = 1; p[TransP::AttackGain] = 0; p[TransP::SustainGain] = 0; p[TransP::Sens] = 0.5f; p[TransP::Mix] = 1; }
};
std::vector<float> runTr(double sr, const std::vector<float>& in, int block, const std::function<void(int, TransParams&)>& sched) {
    TransientEnhancer t;
    t.prepare(sr, 4096, 1);
    TransParams tp;
    return runMono(in, block, [&](float* const* c, int nch, int nn, int s) { sched(s, tp); t.process(c, nch, nn, tp.p); });
}
}

CM_TEST(transient_enhancer_shapes_attack_and_sustain) {
    for (double sr : kRates) {
        const int pre = static_cast<int>(0.1 * sr), tone = static_cast<int>(0.4 * sr);
        const auto in = concat({ std::vector<float>(static_cast<size_t>(pre), 0.0f), sine(sr, 1000.0, 0.25, tone) });
        const int early = static_cast<int>(0.01 * sr), tail = static_cast<int>(0.1 * sr);
        auto up = runTr(sr, in, 128, [](int, TransParams& p) { p.p[TransP::AttackGain] = 9; });
        CM_EXPECT(peakAbs(up, pre, early) > 0.25 * dbToLin(3.0));
        CM_EXPECT_NEAR(rmsDb(up, pre + tone - tail, tail), rmsDb(in, pre + tone - tail, tail), 0.3);
        auto down = runTr(sr, in, 128, [](int, TransParams& p) { p.p[TransP::AttackGain] = -9; });
        CM_EXPECT(peakAbs(down, pre, early) < 0.25 * dbToLin(-3.0));
        CM_EXPECT_NEAR(rmsDb(down, pre + tone - tail, tail), rmsDb(in, pre + tone - tail, tail), 0.3);
        // sustain: exponentially decaying tail (tau 80 ms) after a steady part
        const int dec = static_cast<int>(0.3 * sr);
        auto steady = sine(sr, 1000.0, 0.25, tone + dec);
        for (int i = 0; i < dec; ++i) steady[static_cast<size_t>(tone + i)] *= static_cast<float>(std::exp(-i / (0.08 * sr)));
        const auto sIn = steady;
        const int w0 = tone + static_cast<int>(0.03 * sr), wl = static_cast<int>(0.2 * sr);
        auto sus = runTr(sr, sIn, 128, [](int, TransParams& p) { p.p[TransP::SustainGain] = 9; });
        CM_EXPECT(rmsDb(sus, w0, wl) - rmsDb(sIn, w0, wl) > 2.0);
        CM_EXPECT_NEAR(rmsDb(sus, tone - tail, tail), rmsDb(sIn, tone - tail, tail), 0.3);
        auto dry = runTr(sr, sIn, 128, [](int, TransParams& p) { p.p[TransP::SustainGain] = -9; });
        CM_EXPECT(rmsDb(dry, w0, wl) - rmsDb(sIn, w0, wl) < -2.0);
        // mix 0 is transparent, sensitivity 1 reacts more than 0
        CM_EXPECT(maxAbsDiff(runTr(sr, in, 128, [](int, TransParams& p) { p.p[TransP::AttackGain] = 9; p.p[TransP::Mix] = 0; }), in) < 1e-7);
        // sensitivity: a moderate (+8 dB) step reacts more with Sens 1 than with Sens 0
        auto step = sine(sr, 1000.0, 0.25, pre + tone);
        for (int i = 0; i < pre; ++i) step[static_cast<size_t>(i)] *= 0.4f;
        auto hi = runTr(sr, step, 128, [](int, TransParams& p) { p.p[TransP::AttackGain] = 6; p.p[TransP::Sens] = 1; });
        auto lo = runTr(sr, step, 128, [](int, TransParams& p) { p.p[TransP::AttackGain] = 6; p.p[TransP::Sens] = 0; });
        CM_EXPECT(rmsDb(hi, pre, early * 3) > rmsDb(lo, pre, early * 3) + 0.5);
        CM_EXPECT(rmsDb(lo, pre, early * 3) > rmsDb(step, pre, early * 3) + 0.2);
    }
}

CM_TEST(transient_enhancer_bypass_block_independent_robust) {
    for (double sr : kRates) {
        const auto in = noise(16384, 61);
        CM_EXPECT(bitEqual(runTr(sr, in, 256, [](int, TransParams& p) { p.p[TransP::On] = 0; p.p[TransP::AttackGain] = 12; }), in));
        auto sched = [](int s, TransParams& p) { p.p[TransP::AttackGain] = s < 4096 ? 6.0f : -6.0f; p.p[TransP::SustainGain] = 4; p.p[TransP::Mix] = s < 8192 ? 1.0f : 0.5f; };
        const auto ref = runTr(sr, in, 2048, sched);
        for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runTr(sr, in, b, sched), ref) < 1e-5);
        auto fixed = [](int, TransParams& p) { p.p[TransP::AttackGain] = 6; p.p[TransP::SustainGain] = -3; };
        const auto big = runTr(sr, in, 16384, fixed);
        for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runTr(sr, in, b, fixed), big) < 1e-5);
        const int n = static_cast<int>(sr);
        auto y = runTr(sr, sine(sr, 100.0, 0.5, n), 64, [&](int s, TransParams& p) {
            p.p[TransP::On] = (s / (n / 6)) % 2 ? 0.0f : 1.0f; p.p[TransP::SustainGain] = 12; p.p[TransP::Mix] = s > n / 2 ? 0.2f : 1.0f;
        });
        CM_EXPECT(clickLevel(y, sr, 100.0) < 0.01);
    }
    const auto y = runTr(44100.0, nastyInput(8192, 7), 50, [](int s, TransParams& p) {
        for (int k = 1; k < TransP::Count; ++k) p.p[k] = (s / 50) % 3 == 0 ? kNaN : ((s / 50) % 3 == 1 ? 1.0e9f : -1.0e9f);
        p.p[TransP::On] = 1;
    });
    CM_EXPECT(allFinite(y));
}

// ------------------------------------------------------------------------------------------------ GainRider
CM_TEST(gain_rider_rides_towards_target_within_limits) {
    for (double sr : { 44100.0, 96000.0 }) {
        struct Case { double rmsIn, expect; };
        for (const Case k : { Case { -30.0, 6.0 }, Case { -22.0, 2.0 }, Case { -14.0, -6.0 }, Case { -8.0, -6.0 }, Case { -70.0, 0.0 } }) {
            GainRider r;
            r.prepare(sr, 512, 2);
            r.setTargetLevelDb(-20.0f);
            const int n = static_cast<int>(12.0 * sr);
            const double amp = dbToLin(k.rmsIn) * std::sqrt(2.0);
            auto l = sine(sr, 500.0, amp, n), rr = sine(sr, 500.0, amp, n);
            std::vector<float> trace;
            runStereo(l, rr, 512, [&](float* const* c, int nch, int nn, int) { r.process(c, nch, nn, true); trace.push_back(r.getGainDb()); });
            CM_EXPECT_NEAR(r.getGainDb(), k.expect, 0.3);
            const int last = static_cast<int>(sr);
            CM_EXPECT_NEAR(rmsDb(l, n - last, last), k.rmsIn + k.expect, 0.3);
            // slew limit: never faster than 3 dB/s
            const double perBlock = 3.0 * 512 / sr + 1e-4;
            bool slewOk = true;
            for (size_t i = 1; i < trace.size(); ++i) slewOk = slewOk && std::fabs(trace[i] - trace[i - 1]) <= perBlock;
            CM_EXPECT(slewOk);
        }
        // freezes in silence: the gain reached at -22 dBFS RMS is held when the signal stops
        GainRider r;
        r.prepare(sr, 512, 1);
        r.setTargetLevelDb(-20.0f);
        const int n = static_cast<int>(10.0 * sr);
        auto in = concat({ sine(sr, 500.0, dbToLin(-22.0) * std::sqrt(2.0), n), std::vector<float>(static_cast<size_t>(n), 0.0f) });
        std::vector<float> tr;
        runMono(in, 512, [&](float* const* c, int nch, int nn, int) { r.process(c, nch, nn, true); tr.push_back(r.getGainDb()); });
        const float held = tr[static_cast<size_t>(n / 512 - 1)];
        CM_EXPECT_NEAR(held, 2.0, 0.3);
        CM_EXPECT_NEAR(r.getGainDb(), held, 0.15);
    }
}

CM_TEST(gain_rider_bypass_block_independent_robust) {
    const double sr = 48000.0;
    const auto in = noise(48000, 71, 0.05f);
    auto runR = [&](const std::vector<float>& x, int block, const std::function<bool(int)>& en) {
        GainRider r;
        r.prepare(sr, 4096, 1);
        return runMono(x, block, [&](float* const* c, int nch, int nn, int s) { r.process(c, nch, nn, en(s)); });
    };
    CM_EXPECT(bitEqual(runR(in, 512, [](int) { return false; }), in));
    auto sched = [](int s) { return s < 32768; };
    const auto ref = runR(in, 2048, sched);
    for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runR(in, b, sched), ref) < 1e-5);
    const auto big = runR(in, 48000, [](int) { return true; });
    for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runR(in, b, [](int) { return true; }), big) < 1e-5);
    CM_EXPECT(allFinite(runR(nastyInput(48000, 8), 100, [](int) { return true; })));
}

CM_TEST(gate_transient_rider_density_reset_is_deterministic) {
    const double sr = 48000.0;
    const auto in = concat({ noise(8000, 91, 0.3f), std::vector<float>(4000, 0.0f), noise(8000, 92, 0.05f) });
    Gate g;
    g.prepare(sr, 500, 1);
    GateParams gp;
    auto runG = [&]() { return runMono(in, 500, [&](float* const* c, int nch, int nn, int) { g.process(c, nch, nn, gp.p); }); };
    const auto g1 = runG();
    g.reset();
    CM_EXPECT(bitEqual(runG(), g1));
    TransientEnhancer t;
    t.prepare(sr, 500, 1);
    TransParams tp;
    tp.p[TransP::AttackGain] = 6; tp.p[TransP::SustainGain] = -4;
    auto runT = [&]() { return runMono(in, 500, [&](float* const* c, int nch, int nn, int) { t.process(c, nch, nn, tp.p); }); };
    const auto t1 = runT();
    t.reset();
    CM_EXPECT(bitEqual(runT(), t1));
    GainRider r;
    r.prepare(sr, 500, 1);
    auto runR = [&]() { return runMono(in, 500, [&](float* const* c, int nch, int nn, int) { r.process(c, nch, nn, true); }); };
    const auto r1 = runR();
    r.reset();
    CM_EXPECT(bitEqual(runR(), r1));
    DensityFollower d;
    d.prepare(sr);
    const float* c[] = { in.data() };
    d.process(c, 1, static_cast<int>(in.size()));
    const float d1 = d.getDensity();
    d.reset();
    d.process(c, 1, static_cast<int>(in.size()));
    CM_EXPECT(d.getDensity() == d1);
}

// ------------------------------------------------------------------------------------------------ Envelope
CM_TEST(envelope_follower_time_constants_and_rms) {
    for (double sr : kRates) {
        EnvelopeFollower e;
        e.prepare(sr);
        e.setAttackMs(10.0f);
        e.setReleaseMs(100.0f);
        const int att = static_cast<int>(0.01 * sr), rel = static_cast<int>(0.1 * sr);
        float v = 0.0f;
        for (int i = 0; i < att; ++i) v = e.process(1.0f);
        CM_EXPECT_NEAR(v, 1.0 - std::exp(-1.0), 0.01);
        for (int i = 0; i < 20 * att; ++i) e.process(1.0f);
        for (int i = 0; i < rel; ++i) v = e.process(0.0f);
        CM_EXPECT_NEAR(v, std::exp(-1.0), 0.01);
        e.reset();
        e.setMode(EnvelopeFollower::Mode::Rms);
        e.setAttackMs(20.0f);
        e.setReleaseMs(20.0f);
        const auto s = sine(sr, 1000.0, 1.0, static_cast<int>(sr));
        CM_EXPECT_NEAR(e.processBlock(s.data(), static_cast<int>(s.size())), std::sqrt(0.5), 0.02);
        CM_EXPECT(std::isfinite(e.process(kNaN)) && std::isfinite(e.process(kInf)));
    }
}

CM_TEST(density_follower_maps_level_history_to_0_1) {
    const double sr = 48000.0;
    DensityFollower d;
    d.prepare(sr);
    auto feed = [&](double rms, double sec) {
        auto v = noise(static_cast<int>(sec * sr), 81, 1.0f);
        const double scale = dbToLin(rms) * std::sqrt(3.0);   // uniform noise RMS = amp / sqrt(3)
        for (auto& x : v) x = static_cast<float>(x * scale);
        const float* c[] = { v.data() };
        for (size_t s = 0; s < v.size(); s += 480) {
            const float* cc[] = { c[0] + s };
            d.process(cc, 1, static_cast<int>(std::min<size_t>(480, v.size() - s)));
        }
    };
    feed(-90.0, 5.0);
    CM_EXPECT(d.getDensity() < 0.01f);
    feed(-36.0, 30.0);
    CM_EXPECT_NEAR(d.getDensity(), 0.5, 0.05);
    CM_EXPECT_NEAR(d.getLevelDb(), -36.0, 1.0);
    feed(-6.0, 30.0);
    CM_EXPECT(d.getDensity() > 0.97f);
    feed(-90.0, 2.0);
    const float mid = d.getDensity();
    CM_EXPECT(mid < 0.97f && mid > 0.2f);   // slow release
    feed(-90.0, 30.0);
    CM_EXPECT(d.getDensity() < 0.01f);
    auto bad = nastyInput(4800, 9);
    const float* c[] = { bad.data() };
    d.process(c, 1, 4800);
    CM_EXPECT(std::isfinite(d.getDensity()) && d.getDensity() >= 0.0f && d.getDensity() <= 1.0f);
}
