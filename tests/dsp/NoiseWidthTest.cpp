#include "TestHarness.h"
#include "FxTestUtil.h"
#include "DSP/DigitalDelay.h"
#include "DSP/EchoThrow.h"
#include "DSP/NoiseEnhancer.h"
#include "DSP/ReverseChunk.h"
#include "DSP/TapeDelay.h"
#include "DSP/WidthProcessor.h"
#include <chrono>
#include <cstring>

using namespace crowdmike;
using namespace crowdmike::dsp;
using fxtest::runStereo;
namespace NP = params::NoiseP;
namespace WP = params::WidthP;

namespace {
std::vector<float> noiseParams() {
    auto p = fxtest::defaults(NP::defs);
    p[NP::Amount] = 1.0f; p[NP::Follow] = 0.0f; p[NP::TransFollow] = 0.0f; p[NP::Hpf] = 20.0f; p[NP::Lpf] = 20000.0f;
    return p;
}
double rms(const std::vector<float>& v, size_t from = 0) {
    return std::sqrt(fxtest::energy(v, from, v.size()) / static_cast<double>(v.size() - from));
}
double corr(const std::vector<float>& a, const std::vector<float>& b) {
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / std::sqrt(aa * bb + 1e-30);
}
}

// ------------------------------------------------------------------------------------------------ NoiseEnhancer
CM_TEST(noise_output_is_capped_at_minus_24_dbfs) {
    const int n = 48000 * 3;
    fxtest::Lcg rng;
    std::vector<float> loud(static_cast<size_t>(n));
    for (auto& v : loud) v = 4.0f * rng.next();
    for (int colour = 0; colour < 4; ++colour) {
        for (float tilt : { -1.0f, 1.0f }) {
            NoiseEnhancer ne;
            ne.prepare(48000.0, 512);
            auto p = noiseParams();
            p[NP::Color] = static_cast<float>(colour); p[NP::Tilt] = tilt; p[NP::TransFollow] = 1.0f;
            std::vector<float> oL, oR;
            runStereo(ne, loud, loud, oL, oR, 512, p.data(), FxContext {});
            CM_EXPECT(fxtest::maxAbs(oL) <= NoiseEnhancer::kCeiling && fxtest::maxAbs(oR) <= NoiseEnhancer::kCeiling);
            CM_EXPECT(fxtest::allFinite(oL));
            // A real but controlled level (not silent, not static at the cap): ~-48..-26 dBFS RMS.
            const double r = rms(oL, 4800);
            CM_EXPECT(r > 0.004 && r < 0.05);
        }
    }
}

CM_TEST(noise_amount_zero_is_exact_silence_and_follow_tracks_input) {
    const int n = 48000;
    fxtest::Lcg rng;
    std::vector<float> loud(static_cast<size_t>(n)), quiet(static_cast<size_t>(n), 0.0f), oL, oR;
    for (auto& v : loud) v = 0.5f * rng.next();
    NoiseEnhancer ne;
    ne.prepare(48000.0, 256);
    auto p = noiseParams();
    p[NP::Amount] = 0.0f;
    runStereo(ne, loud, loud, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL) == 0.0f && fxtest::maxAbs(oR) == 0.0f);

    p[NP::Amount] = 1.0f; p[NP::Follow] = 1.0f;
    ne.reset();
    runStereo(ne, quiet, quiet, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL) == 0.0f && fxtest::maxAbs(oR) == 0.0f);   // silence in -> silence out
    runStereo(ne, loud, loud, oL, oR, 256, p.data(), FxContext {});
    const double followLoud = rms(oL, 4800);
    CM_EXPECT(followLoud > 0.003);
    runStereo(ne, quiet, quiet, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(rms(oL, 36000) < 0.3 * followLoud);                           // follows the crowd back down (release)

    p[NP::Follow] = 0.0f;   // constant level regardless of input
    ne.reset();
    runStereo(ne, quiet, quiet, oL, oR, 256, p.data(), FxContext {});
    const double a = rms(oL, 4800);
    ne.reset();
    runStereo(ne, loud, loud, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT_NEAR(rms(oL, 4800) / a, 1.0, 1e-6);

    // Transient follower adds level on onsets only.
    p[NP::Follow] = 1.0f; p[NP::Amount] = 1.0f; p[NP::TransFollow] = 1.0f;
    std::vector<float> step(static_cast<size_t>(n), 0.0f);
    for (int i = 24000; i < n; ++i) step[static_cast<size_t>(i)] = 0.05f * rng.next();
    ne.reset();
    runStereo(ne, step, step, oL, oR, 256, p.data(), FxContext {});
    const double onset = rms(std::vector<float>(oL.begin() + 24000, oL.begin() + 26400));
    const double steady = rms(std::vector<float>(oL.begin() + 40000, oL.begin() + 42400));
    CM_EXPECT(onset > 1.5 * steady);
}

CM_TEST(noise_is_deterministic_and_seeded) {
    const int n = 20000;
    std::vector<float> in(static_cast<size_t>(n), 0.0f), aL, aR, bL, bR;
    auto p = noiseParams();
    p[NP::Seed] = 42.0f;
    NoiseEnhancer a, b;
    a.prepare(48000.0, n); b.prepare(48000.0, 64);
    runStereo(a, in, in, aL, aR, n, p.data(), FxContext {});
    runStereo(b, in, in, bL, bR, 64, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(aL, bL) <= 1e-6f && fxtest::maxDiff(aR, bR) <= 1e-6f);   // block independent, reproducible
    a.reset();
    runStereo(a, in, in, bL, bR, 1, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(aL, bL) <= 1e-6f);                                        // reset() restarts the stream
    p[NP::Seed] = 43.0f;
    a.reset();
    runStereo(a, in, in, bL, bR, 512, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(aL, bL) > 1e-3f);                                         // different seed, different noise
    // Width: 0 -> identical channels, 1 -> decorrelated.
    p[NP::Width] = 0.0f;
    a.reset();
    runStereo(a, in, in, aL, aR, 512, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(aL, aR) == 0.0f);
    p[NP::Width] = 1.0f;
    a.reset();
    runStereo(a, in, in, aL, aR, 512, p.data(), FxContext {});
    CM_EXPECT(std::fabs(corr(aL, aR)) < 0.1);
    // Bypass fades then writes exact zeros; NaN input/params are harmless.
    p[NP::Bypass] = 1.0f;
    runStereo(a, in, in, aL, aR, 64, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(aL, 600) == 0.0f);
    std::vector<float> bad(NP::Count, std::numeric_limits<float>::quiet_NaN());
    in[5] = std::numeric_limits<float>::infinity();
    runStereo(a, in, in, aL, aR, 64, bad.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(aL) && fxtest::allFinite(aR));
}

// ------------------------------------------------------------------------------------------------ WidthProcessor
namespace {
std::vector<float> widthParams(float width) {
    std::vector<float> p(WP::Count, 0.0f);
    p[WP::On] = 1.0f; p[WP::Width] = width;
    return p;
}
void runWidth(WidthProcessor& w, std::vector<float>& L, std::vector<float>& R, int block, const float* p) {
    for (size_t pos = 0; pos < L.size(); pos += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), L.size() - pos));
        w.process(L.data() + pos, R.data() + pos, n, p);
    }
}
}

CM_TEST(width_identity_and_mono_extremes) {
    fxtest::Lcg rng;
    const int n = 20000;
    std::vector<float> inL(n), inR(n);
    for (int i = 0; i < n; ++i) { inL[i] = 0.7f * rng.next(); inR[i] = 0.3f * rng.next() + 0.2f * inL[i]; }
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
        WidthProcessor w;
        w.prepare(sr, 512);
        auto L = inL, R = inR;
        auto p = widthParams(1.0f);
        runWidth(w, L, R, 512, p.data());
        CM_EXPECT(fxtest::maxDiff(L, inL) < 1e-5f && fxtest::maxDiff(R, inR) < 1e-5f);

        w.reset();
        L = inL; R = inR;
        p = widthParams(0.0f);
        runWidth(w, L, R, 512, p.data());
        float worst = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float mid = 0.5f * (inL[i] + inR[i]);
            worst = std::max({ worst, std::fabs(L[i] - mid), std::fabs(R[i] - mid) });
        }
        CM_EXPECT(worst < 1e-6f);

        w.reset();   // MonoCompat 1 -> mono regardless of width / Haas / decorrelation
        L = inL; R = inR;
        p = widthParams(2.0f);
        p[WP::MonoCompat] = 1.0f; p[WP::HaasMs] = 12.0f; p[WP::Decorr] = 1.0f; p[WP::LfMonoHz] = 150.0f;
        runWidth(w, L, R, 512, p.data());
        CM_EXPECT(fxtest::maxDiff(L, R) < 1e-6f);
    }
}

CM_TEST(width_lf_mono_keeps_bass_centred_and_mono_sum) {
    const double sr = 48000.0;
    const int n = 48000;
    auto lowL = fxtest::sine(n, sr, 40.0, 0.5f);
    std::vector<float> lowR(n, 0.0f);   // bass only on the left: lots of LF side
    auto hi = fxtest::sine(n, sr, 5000.0, 0.2f);
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) { L[i] = lowL[i] + hi[i]; R[i] = lowR[i] - hi[i]; }
    const auto inL = L, inR = R;
    WidthProcessor w;
    w.prepare(sr, 256);
    auto p = widthParams(1.5f);
    p[WP::LfMonoHz] = 200.0f;
    runWidth(w, L, R, 256, p.data());
    // Mono sum is untouched (mid path is never filtered).
    std::vector<float> monoIn(n), monoOut(n), sideLow(n);
    for (int i = 0; i < n; ++i) { monoIn[i] = inL[i] + inR[i]; monoOut[i] = L[i] + R[i]; }
    CM_EXPECT(fxtest::maxDiff(monoIn, monoOut) < 1e-5f);
    // LF side removed (40 Hz side was 0.25 peak), HF side widened x1.5 (0.2 -> 0.3 peak).
    WidthProcessor probe;   // isolate the 40 Hz part
    probe.prepare(sr, 256);
    auto bL = lowL, bR = lowR;
    runWidth(probe, bL, bR, 256, p.data());
    for (int i = 0; i < n; ++i) sideLow[i] = 0.5f * (bL[i] - bR[i]);
    CM_EXPECT(fxtest::maxAbs(sideLow, 24000) < 0.25f * 0.03f);
    std::vector<float> side(n);
    for (int i = 0; i < n; ++i) side[i] = 0.5f * (L[i] - R[i]) - sideLow[i];
    CM_EXPECT_NEAR(fxtest::maxAbs(side, 24000), 0.3, 0.01);

    // Haas on a bass-only signal: the low band is not delayed -> mono energy within 1 dB.
    WidthProcessor hz;
    hz.prepare(sr, 256);
    auto q = widthParams(1.0f);
    q[WP::HaasMs] = 20.0f;
    auto aL = lowL, aR = lowL;   // centred 40 Hz
    runWidth(hz, aL, aR, 256, q.data());
    std::vector<float> m1(n), m0(n);
    for (int i = 0; i < n; ++i) { m1[i] = aL[i] + aR[i]; m0[i] = 2.0f * lowL[i]; }
    const double ratioDb = 10.0 * std::log10(fxtest::energy(m1, 24000, n) / fxtest::energy(m0, 24000, n));
    CM_EXPECT(std::fabs(ratioDb) < 1.0);
}

CM_TEST(width_haas_delays_high_band_of_right_only) {
    const double sr = 48000.0;
    WidthProcessor w;
    w.prepare(sr, 2048);
    auto p = widthParams(1.0f);
    p[WP::HaasMs] = 10.0f;
    std::vector<float> L(4000, 0.0f), R(4000, 0.0f);
    L[100] = 1.0f; R[100] = 1.0f;
    runWidth(w, L, R, 2048, p.data());
    CM_EXPECT_NEAR(L[100], 1.0, 1e-6);                   // left untouched
    CM_EXPECT(fxtest::maxAbs(L, 101) < 1e-6f);
    CM_EXPECT(std::fabs(R[100 + 480]) > 0.3f);           // right high band arrives 480 samples later
    CM_EXPECT(std::fabs(R[100]) < 0.2f);                 // only the (low-passed) low band is on time
}

CM_TEST(width_bypass_is_click_free_and_bit_exact) {
    const double sr = 48000.0;
    const int n = 48000;
    auto L0 = fxtest::sine(n, sr, 300.0, 0.5f);
    auto R0 = fxtest::sine(n, sr, 450.0, 0.5f);
    WidthProcessor w;
    w.prepare(sr, 64);
    auto p = widthParams(2.0f);
    p[WP::Decorr] = 0.5f; p[WP::HaasMs] = 5.0f; p[WP::LfMonoHz] = 120.0f;
    auto L = L0, R = R0;
    for (int pos = 0; pos < n; pos += 64) {
        p[WP::On] = pos < 24000 ? 1.0f : 0.0f;
        w.process(L.data() + pos, R.data() + pos, 64, p.data());
    }
    const float step = 0.5f * 2.0f * 3.14159265f * 450.0f / 48000.0f;
    CM_EXPECT(fxtest::maxJump(L, 24000 - 64, 24000 + 1024) < 4.0f * step);
    CM_EXPECT(fxtest::maxJump(R, 24000 - 64, 24000 + 1024) < 4.0f * step);
    CM_EXPECT(std::memcmp(L.data() + 25000, L0.data() + 25000, sizeof(float) * (n - 25000)) == 0);
    CM_EXPECT(std::memcmp(R.data() + 25000, R0.data() + 25000, sizeof(float) * (n - 25000)) == 0);
    // NaN input while on -> finite output; NaN params -> defaults.
    std::vector<float> bL(1000, std::numeric_limits<float>::quiet_NaN()), bR(1000, 1.0f);
    p[WP::On] = 1.0f;
    runWidth(w, bL, bR, 100, p.data());
    std::vector<float> bad(WP::Count, std::numeric_limits<float>::quiet_NaN());
    runWidth(w, bL, bR, 100, bad.data());
    CM_EXPECT(fxtest::allFinite(bL) && fxtest::allFinite(bR));
}

CM_TEST(width_block_size_independence) {
    fxtest::Lcg rng;
    const int n = 30000;
    std::vector<float> inL(n), inR(n);
    for (int i = 0; i < n; ++i) { inL[i] = 0.5f * rng.next(); inR[i] = 0.5f * rng.next(); }
    auto p = widthParams(1.7f);
    p[WP::Decorr] = 0.6f; p[WP::HaasMs] = 7.3f; p[WP::LfMonoHz] = 140.0f; p[WP::MonoCompat] = 0.2f;
    WidthProcessor w;
    w.prepare(48000.0, n);
    auto rL = inL, rR = inR;
    runWidth(w, rL, rR, n, p.data());
    for (int b : { 1, 64, 512, 2048 }) {
        w.reset();
        auto L = inL, R = inR;
        runWidth(w, L, R, b, p.data());
        CM_EXPECT(fxtest::maxDiff(L, rL) <= 1e-5f && fxtest::maxDiff(R, rR) <= 1e-5f);
    }
}

// ------------------------------------------------------------------------------------------------ CPU budget
CM_TEST(time_fx_cpu_budget_128_at_48k) {
    const double sr = 48000.0;
    const int block = 128, blocks = 3000;
    DigitalDelay dd; EchoThrow et; TapeDelay td; ReverseChunk rc; NoiseEnhancer ne; WidthProcessor wp;
    dd.prepare(sr, block); et.prepare(sr, block); td.prepare(sr, block); rc.prepare(sr, block); ne.prepare(sr, block);
    wp.prepare(sr, block);
    auto pd = fxtest::defaults(params::DelayP::defs); pd[params::DelayP::Diffusion] = 0.5f; pd[params::DelayP::Mode] = 2.0f;
    auto pe = fxtest::defaults(params::EchoP::defs);
    auto pt = fxtest::defaults(params::TapeP::defs);
    auto pr = fxtest::defaults(params::ReverseP::defs); pr[params::ReverseP::Mode] = 1.0f; pr[params::ReverseP::ChunkSec] = 0.5f;
    auto pn = fxtest::defaults(params::NoiseP::defs);
    auto pw = widthParams(1.5f); pw[WP::Decorr] = 0.5f; pw[WP::HaasMs] = 5.0f; pw[WP::LfMonoHz] = 120.0f;
    fxtest::Lcg rng;
    std::vector<float> inL(block), inR(block), oL(block), oR(block);
    double worstAvgMs = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < block; ++i) { inL[i] = 0.3f * rng.next(); inR[i] = 0.3f * rng.next(); }
        pe[params::EchoP::Trigger] = (b % 200) < 100 ? 1.0f : 0.0f;
        const FxContext ctx;
        dd.process(inL.data(), inR.data(), oL.data(), oR.data(), block, pd.data(), ctx);
        et.process(inL.data(), inR.data(), oL.data(), oR.data(), block, pe.data(), ctx);
        td.process(inL.data(), inR.data(), oL.data(), oR.data(), block, pt.data(), ctx);
        rc.process(inL.data(), inR.data(), inL.data(), inR.data(), oL.data(), oR.data(), block, pr.data(), ctx);
        ne.process(inL.data(), inR.data(), oL.data(), oR.data(), block, pn.data(), ctx);
        wp.process(oL.data(), oR.data(), block, pw.data());
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    worstAvgMs = ms / blocks;
    std::cout << "  [time fx] all six modules, stereo 128 @ 48 kHz: " << worstAvgMs * 1000.0 << " us/block (budget 2667 us)\n";
    CM_EXPECT(worstAvgMs < 0.5);
}
