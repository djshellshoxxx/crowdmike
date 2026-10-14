// Tests: Limiter (lookahead brickwall, constant latency, zero-lookahead bus mode).
#include "TestHarness.h"
#include "Core/ParamTables.h"
#include "DSP/Biquad.h"
#include "DSP/Limiter.h"
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
constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
constexpr double kPiD = 3.14159265358979323846;

double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
std::vector<float> sine(double sr, double hz, double amp, int n) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * kPiD * hz * i / sr));
    return v;
}
std::vector<float> noise(int n, unsigned seed, float amp) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-amp, amp);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = d(rng);
    return v;
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
double meanSquare(const std::vector<float>& y, int start, int len) {
    double s = 0.0;
    for (int i = start; i < start + len; ++i) s += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    return s / len;
}
double peakAbs(const std::vector<float>& y, int from = 0) {
    double m = 0.0;
    for (size_t i = static_cast<size_t>(from); i < y.size(); ++i) m = std::max(m, static_cast<double>(std::fabs(y[i])));
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

struct LimParams {
    float p[LimP::Count] {};
    LimParams(float ceil = -1.0f) { p[LimP::On] = 1; p[LimP::Ceiling] = ceil; p[LimP::Release] = 100; p[LimP::Drive] = 0; }
};
using Sched = std::function<void(int, LimParams&)>;
std::vector<float> runLim(double sr, float lookMs, const std::vector<float>& in, int block, const Sched& sched,
                          Limiter* ext = nullptr) {
    Limiter local;
    Limiter& l = ext != nullptr ? *ext : local;
    if (ext == nullptr) l.prepare(sr, 2048, 1, lookMs);
    LimParams lp;
    auto v = in;
    const int n = static_cast<int>(v.size());
    for (int s = 0; s < n; s += block) {
        sched(s, lp);
        float* c[] = { v.data() + s };
        l.process(c, 1, std::min(block, n - s), lp.p, true);
    }
    return v;
}
std::vector<float> bursts(double sr, int n, double peakDb) {
    std::vector<float> v(static_cast<size_t>(n), 0.0f);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    const int period = static_cast<int>(0.05 * sr), len = static_cast<int>(0.005 * sr);
    for (int s = period / 3; s < n; s += period)
        for (int i = 0; i < len && s + i < n; ++i) v[static_cast<size_t>(s + i)] = static_cast<float>(dbToLin(peakDb) * d(rng));
    return v;
}
}

CM_TEST(limiter_never_exceeds_ceiling) {
    for (double sr : kRates) {
        const int n = static_cast<int>(0.25 * sr);
        std::vector<std::vector<float>> inputs;
        inputs.push_back(noise(n, 1, 10.0f));                     // +20 dBFS noise
        inputs.push_back(sine(sr, 1000.0, dbToLin(12.0), n));      // +12 dBFS sine
        inputs.push_back(sine(sr, 60.0, dbToLin(6.0), n));         // low-frequency sine
        inputs.push_back(bursts(sr, n, 20.0));                     // +20 dBFS bursts out of silence
        auto spike = std::vector<float>(static_cast<size_t>(n), 0.01f);
        for (int i = 300; i < n; i += 997) spike[static_cast<size_t>(i)] = (i & 1) ? 10.0f : -10.0f;
        inputs.push_back(spike);                                   // isolated full-scale+ spikes
        for (float look : { 0.0f, 1.5f, 5.0f }) {
            for (float ceil : { -1.0f, -6.0f }) {
                const double limit = dbToLin(ceil + 0.01);
                for (int block : { 1, 64, 512, 2048 }) {
                    if (block == 1 && sr > 48000.0) continue;   // runtime
                    for (const auto& in : inputs) {
                        const auto y = runLim(sr, look, in, block, [&](int, LimParams& p) { p.p[LimP::Ceiling] = ceil; });
                        CM_EXPECT(peakAbs(y) <= limit);
                    }
                    // with input drive (+24 dB on a -6 dBFS sine) and a drive ramp mid-stream
                    const auto y = runLim(sr, look, sine(sr, 440.0, 0.5, n), block, [&](int s, LimParams& p) {
                        p.p[LimP::Ceiling] = ceil; p.p[LimP::Drive] = s < n / 2 ? 24.0f : 6.0f; p.p[LimP::Release] = 5.0f;
                    });
                    CM_EXPECT(peakAbs(y) <= limit);
                }
            }
        }
    }
}

CM_TEST(limiter_latency_is_constant_and_reported) {
    for (double sr : kRates) {
        for (float look : { 0.0f, 1.0f, 2.5f, 5.0f }) {
            Limiter l;
            l.prepare(sr, 512, 2, look);
            const int L = static_cast<int>(std::lround(look * sr / 1000.0));
            CM_EXPECT(l.getLatencySamples() == L);
            // impulses below the ceiling come out exactly L samples later, whether On or Off, across On toggles
            const int n = 20000;
            std::vector<float> in(static_cast<size_t>(n), 0.0f);
            for (int i = 100; i < n; i += 1000) in[static_cast<size_t>(i)] = 0.5f;
            auto a = in, b = in;
            LimParams lp;
            for (int s = 0; s < n; s += 512) {
                lp.p[LimP::On] = (s / 4096) % 2 ? 0.0f : 1.0f;
                float* c[] = { a.data() + s, b.data() + s };
                l.process(c, 2, std::min(512, n - s), lp.p, true);
            }
            CM_EXPECT(l.getLatencySamples() == L);
            bool ok = true;
            for (int i = 0; i < n; ++i) {
                const float expect = i - L >= 0 ? in[static_cast<size_t>(i - L)] : 0.0f;
                ok = ok && std::fabs(a[static_cast<size_t>(i)] - expect) < 1e-6f && a[static_cast<size_t>(i)] == b[static_cast<size_t>(i)];
            }
            CM_EXPECT(ok);
        }
    }
}

CM_TEST(limiter_steady_sine_is_clean_and_gain_reduction_reported) {
    for (double sr : kRates) {
        const int n = static_cast<int>(sr), start = n / 2, len = n / 2;
        for (double hz : { 200.0, 1000.0 }) {
            Limiter l;
            l.prepare(sr, 512, 1, 5.0f);
            const auto y = runLim(sr, 5.0f, sine(sr, hz, dbToLin(5.0), n), 512, [](int, LimParams&) {}, &l);
            const double amp = toneAmp(y, sr, hz, start, len);
            CM_EXPECT_NEAR(20.0 * std::log10(amp), -1.0, 0.2);
            const double resid = meanSquare(y, start, len) - amp * amp / 2.0;
            CM_EXPECT(10.0 * std::log10(std::max(resid, 1e-20) / (amp * amp / 2.0)) < -50.0);
            CM_EXPECT_NEAR(l.getGainReductionDb(), -6.0, 0.3);
        }
        // below the ceiling: unity (delayed), no gain reduction
        Limiter l;
        l.prepare(sr, 512, 1, 5.0f);
        const auto in = sine(sr, 1000.0, 0.5, n);
        const auto y = runLim(sr, 5.0f, in, 512, [](int, LimParams&) {}, &l);
        const int L = l.getLatencySamples();
        double d = 0.0;
        for (int i = L; i < n; ++i) d = std::max(d, std::fabs(static_cast<double>(y[static_cast<size_t>(i)]) - in[static_cast<size_t>(i - L)]));
        CM_EXPECT(d < 1e-6);
        CM_EXPECT(l.getGainReductionDb() > -0.01f);
    }
}

CM_TEST(limiter_releases_after_a_burst) {
    const double sr = 48000.0;
    const int n = static_cast<int>(sr);
    auto in = sine(sr, 1000.0, 0.1, n);
    for (int i = 1000; i < 1500; ++i) in[static_cast<size_t>(i)] *= 50.0f;
    Limiter l;
    l.prepare(sr, 480, 1, 2.0f);
    LimParams lp;
    lp.p[LimP::Release] = 50;
    std::vector<float> gr;
    auto v = in;
    for (int s = 0; s < n; s += 480) {
        float* c[] = { v.data() + s };
        l.process(c, 1, 480, lp.p, true);
        gr.push_back(l.getGainReductionDb());
    }
    CM_EXPECT(gr[3] < -10.0f);                       // engaged during the burst
    CM_EXPECT(gr[static_cast<size_t>((1500 + 0.1 * sr) / 480)] > -1.5f);   // ~2 release time constants later
    CM_EXPECT(gr.back() > -0.01f);
}

CM_TEST(limiter_bypass_bit_exact_and_transitions_click_free) {
    for (double sr : kRates) {
        const auto in = noise(20000, 3, 2.0f);
        // zero lookahead + Off: untouched
        CM_EXPECT(bitEqual(runLim(sr, 0.0f, in, 256, [](int, LimParams& p) { p.p[LimP::On] = 0; }), in));
        // lookahead + Off: exact delayed copy
        Limiter l;
        l.prepare(sr, 2048, 1, 3.0f);
        const auto y = runLim(sr, 3.0f, in, 300, [](int, LimParams& p) { p.p[LimP::On] = 0; p.p[LimP::Drive] = 12; }, &l);
        const int L = l.getLatencySamples();
        bool exact = true;
        for (size_t i = static_cast<size_t>(L); i < in.size(); ++i) exact = exact && y[i] == in[i - static_cast<size_t>(L)];
        CM_EXPECT(exact);
        // toggling On with a signal 6 dB over the ceiling: crossfaded, no clicks. (A 1 kHz tone: with 3 ms lookahead the
        // limiter gain is steady, so anything far above the tone is a transition artefact.)
        const int n = static_cast<int>(sr);
        const auto tone = sine(sr, 1000.0, dbToLin(5.0), n);
        const auto t = runLim(sr, 3.0f, tone, 64, [&](int s, LimParams& p) {
            p.p[LimP::On] = (s / (n / 6)) % 2 ? 0.0f : 1.0f;
            p.p[LimP::Drive] = s > n / 2 ? 6.0f : 0.0f;
        });
        CM_EXPECT(clickLevel(t, sr, 1000.0) < 0.01);
    }
}

CM_TEST(limiter_linking_block_independence_reset_and_robustness) {
    for (double sr : { 44100.0, 96000.0 }) {
        const int n = static_cast<int>(0.5 * sr);
        // linked: the quiet channel follows the loud channel's gain; unlinked: it is untouched
        for (bool link : { true, false }) {
            Limiter l;
            l.prepare(sr, 512, 2, 2.0f);
            auto a = sine(sr, 1000.0, dbToLin(5.0), n), b = sine(sr, 1000.0, 0.1, n);
            LimParams lp;
            for (int s = 0; s < n; s += 512) {
                float* c[] = { a.data() + s, b.data() + s };
                l.process(c, 2, std::min(512, n - s), lp.p, link);
            }
            const double gb = 20.0 * std::log10(toneAmp(b, sr, 1000.0, n / 2, n / 2) / 0.1);
            CM_EXPECT_NEAR(gb, link ? -6.0 : 0.0, 0.3);
            CM_EXPECT(peakAbs(a) <= dbToLin(-0.99) && peakAbs(b) <= dbToLin(-0.99));
        }
        const auto in = noise(16384, 9, 4.0f);
        for (float look : { 0.0f, 4.0f }) {
            auto sched = [](int s, LimParams& p) {
                p.p[LimP::Drive] = s < 4096 ? 0.0f : 6.0f; p.p[LimP::Ceiling] = s < 8192 ? -1.0f : -3.0f;
                p.p[LimP::On] = s < 12288 ? 1.0f : 0.0f;
            };
            const auto ref = runLim(sr, look, in, 2048, sched);
            for (int b : { 1, 64, 512 }) CM_EXPECT(maxAbsDiff(runLim(sr, look, in, b, sched), ref) < 1e-5);
            auto fixed = [](int, LimParams& p) { p.p[LimP::Release] = 30; };
            Limiter big;
            big.prepare(sr, 16384, 1, look);
            const auto r0 = runLim(sr, look, in, 16384, fixed, &big);
            for (int b : { 1, 64, 512, 2048 }) CM_EXPECT(maxAbsDiff(runLim(sr, look, in, b, fixed), r0) < 1e-5);
            big.reset();
            CM_EXPECT(bitEqual(runLim(sr, look, in, 16384, fixed, &big), r0));
        }
    }
    auto bad = noise(8192, 4, 1.0f);
    bad[10] = std::numeric_limits<float>::quiet_NaN();
    bad[20] = std::numeric_limits<float>::infinity();
    bad[30] = -3.0e38f;
    bad[40] = 1.0e30f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float look : { 0.0f, 5.0f }) {
        const auto y = runLim(48000.0, look, bad, 64, [&](int s, LimParams& p) {
            const int k = (s / 64) % 3;
            p.p[LimP::Ceiling] = k == 0 ? nan : (k == 1 ? 1.0e9f : -1.0e9f);
            p.p[LimP::Release] = k == 0 ? -1.0e9f : nan;
            p.p[LimP::Drive] = k == 2 ? nan : 1.0e9f;
        });
        CM_EXPECT(allFinite(y));
        CM_EXPECT(peakAbs(y) <= 1.0);
    }
    Limiter weird;
    weird.prepare(48000.0, 64, 1, std::numeric_limits<float>::quiet_NaN());
    CM_EXPECT(weird.getLatencySamples() == 0);
    weird.prepare(48000.0, 64, 1, 1000.0f);
    CM_EXPECT(weird.getLatencySamples() == 2400);   // clamped to 50 ms
}
