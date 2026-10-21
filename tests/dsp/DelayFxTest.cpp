#include "TestHarness.h"
#include "FxTestUtil.h"
#include "DSP/DigitalDelay.h"
#include "DSP/EchoThrow.h"
#include "DSP/TapeDelay.h"

using namespace crowdmike;
using namespace crowdmike::dsp;
using fxtest::runStereo;
namespace DP = params::DelayP;
namespace EP = params::EchoP;
namespace TP = params::TapeP;

namespace {
std::vector<float> delayParams(float ms, float fb = 0.0f, int mode = 0) {
    auto p = fxtest::defaults(DP::defs);
    p[DP::TimeMs] = ms; p[DP::Feedback] = fb; p[DP::Mode] = static_cast<float>(mode);
    p[DP::Hpf] = 20.0f; p[DP::Lpf] = 20000.0f;
    return p;
}
int firstNonZero(const std::vector<float>& v) {
    for (size_t i = 0; i < v.size(); ++i) if (v[i] != 0.0f) return static_cast<int>(i);
    return -1;
}
}

// ------------------------------------------------------------------------------------------------ DigitalDelay
CM_TEST(delay_time_is_sample_exact_across_rates) {
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const float times[] = { 1.0f, 10.5f, 350.0f, 1234.5f, 2000.0f };
    for (double sr : rates) {
        DigitalDelay d;
        d.prepare(sr, 512);
        for (float ms : times) {
            d.reset();
            const int expected = static_cast<int>(std::lround(sr * ms / 1000.0));
            const int len = expected + 64;
            std::vector<float> in(static_cast<size_t>(len), 0.0f), oL, oR;
            in[0] = 1.0f;
            auto p = delayParams(ms);
            runStereo(d, in, in, oL, oR, 512, p.data(), FxContext {});
            CM_EXPECT(firstNonZero(oL) == expected);
            CM_EXPECT(firstNonZero(oR) == expected);
            CM_EXPECT_NEAR(oL[static_cast<size_t>(expected)], 1.0, 1e-6);
            CM_EXPECT(d.getTargetDelaySamples() == expected);
        }
    }
}

CM_TEST(delay_stereo_offset_shifts_right_channel) {
    DigitalDelay d;
    d.prepare(48000.0, 256);
    auto p = delayParams(100.0f);
    p[DP::Offset] = 10.0f;
    std::vector<float> in(8000, 0.0f), oL, oR;
    in[0] = 1.0f;
    runStereo(d, in, in, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(firstNonZero(oL) == 4800);
    CM_EXPECT(firstNonZero(oR) == 5280);
}

CM_TEST(delay_tempo_sync_period_120_and_90_bpm) {
    DigitalDelay d;
    d.prepare(48000.0, 512);
    struct Case { double bpm; int div; int expected; };
    const Case cases[] = { { 120.0, 8, 24000 }, { 90.0, 5, 16000 }, { 120.0, 2, 6000 }, { 90.0, 12, 128000 } };
    for (const auto& c : cases) {
        d.reset();
        auto p = delayParams(350.0f);
        p[DP::Sync] = 1.0f; p[DP::Division] = static_cast<float>(c.div);
        FxContext ctx; ctx.hasTempo = true; ctx.bpm = c.bpm; ctx.playing = true;
        const int expected = std::min(c.expected, static_cast<int>(std::lround(48000.0 * DigitalDelay::kMaxDelayMs / 1000.0)));
        std::vector<float> in(static_cast<size_t>(expected + 100), 0.0f), oL, oR;
        in[0] = 1.0f;
        runStereo(d, in, in, oL, oR, 512, p.data(), ctx);
        CM_EXPECT(firstNonZero(oL) == expected);
    }
    // No host tempo: falls back to TimeMs.
    d.reset();
    auto p = delayParams(350.0f);
    p[DP::Sync] = 1.0f;
    std::vector<float> in(20000, 0.0f), oL, oR;
    in[0] = 1.0f;
    runStereo(d, in, in, oL, oR, 512, p.data(), FxContext {});
    CM_EXPECT(firstNonZero(oL) == 16800);
}

CM_TEST(delay_ping_pong_alternates) {
    DigitalDelay d;
    d.prepare(48000.0, 512);
    auto p = delayParams(100.0f, 0.5f, 1);
    std::vector<float> inL(30000, 0.0f), inR(30000, 0.0f), oL, oR;
    inL[0] = 1.0f;
    runStereo(d, inL, inR, oL, oR, 512, p.data(), FxContext {});
    CM_EXPECT_NEAR(oL[4800], 0.5, 1e-6);                       // mono sum lands left first
    CM_EXPECT(fxtest::maxAbs(oR, 0, 9000) == 0.0f);
    CM_EXPECT(fxtest::maxAbs(oR, 9590, 9700) > 0.1f);          // then right
    CM_EXPECT(fxtest::maxAbs(oL, 9000, 14000) < 0.01f);
    CM_EXPECT(fxtest::maxAbs(oL, 14390, 14500) > 0.05f);       // then left again
    CM_EXPECT(fxtest::maxAbs(oR, 14390, 14500) < 0.01f);
}

CM_TEST(delay_multitap_spacing) {
    for (int taps : { 2, 3, 4, 6 }) {
        DigitalDelay d;
        d.prepare(48000.0, 512);
        auto p = delayParams(100.0f, 0.0f, 2);
        p[DP::Taps] = static_cast<float>(taps);
        std::vector<float> in(6000, 0.0f), oL, oR;
        in[0] = 1.0f;
        runStereo(d, in, in, oL, oR, 512, p.data(), FxContext {});
        int nonZero = 0;
        for (float v : oL) nonZero += v != 0.0f;
        CM_EXPECT(nonZero == taps);
        for (int k = 1; k <= taps; ++k) {
            const size_t at = static_cast<size_t>(std::lround(4800.0 * k / taps));
            CM_EXPECT(std::fabs(oL[at]) > 0.1f);
            CM_EXPECT(std::fabs(oR[at]) > 0.1f);
        }
    }
}

CM_TEST(delay_feedback_bounded_with_extreme_params) {
    for (int mode = 0; mode < 3; ++mode) {
        DigitalDelay d;
        d.prepare(48000.0, 512);
        auto p = delayParams(20.0f, 5.0f /* far above range */, mode);
        p[DP::Diffusion] = 1.0f; p[DP::InGain] = 12.0f; p[DP::OutGain] = 12.0f;
        p[DP::Taps] = 6.0f;
        fxtest::Lcg rng;
        const int n = 48000 * 10;
        std::vector<float> in(static_cast<size_t>(n)), oL, oR;
        for (auto& v : in) v = 8.0f * rng.next();
        FxContext ctx; ctx.freeze = false;
        runStereo(d, in, in, oL, oR, 512, p.data(), ctx);
        CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
        CM_EXPECT(fxtest::maxAbs(oL) < 40.0f && fxtest::maxAbs(oR) < 40.0f);
        ctx.freeze = true;
        runStereo(d, in, in, oL, oR, 512, p.data(), ctx);
        CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
        CM_EXPECT(fxtest::maxAbs(oL) < 40.0f && fxtest::maxAbs(oR) < 40.0f);
    }
}

CM_TEST(delay_freeze_holds_loop_and_gates_input) {
    DigitalDelay d;
    d.prepare(48000.0, 256);
    auto p = delayParams(300.0f, 0.5f);
    const int sec = 48000;
    std::vector<float> in = fxtest::sine(sec, 48000.0, 300.0, 0.5f), oL, oR;
    runStereo(d, in, in, oL, oR, 256, p.data(), FxContext {});
    FxContext fz; fz.freeze = true;
    std::vector<float> loud = fxtest::sine(4 * sec, 48000.0, 1000.0, 1.0f);
    runStereo(d, loud, loud, oL, oR, 256, p.data(), fz);
    const float early = fxtest::maxAbs(oL, 0, static_cast<size_t>(sec));
    const float late = fxtest::maxAbs(oL, static_cast<size_t>(3 * sec), static_cast<size_t>(4 * sec));
    CM_EXPECT(late > 0.2f * early);   // loop held (no input refresh, feedback 0.97)
    CM_EXPECT(late < 1.5f);
    // Input gated: the loop must not contain the 1 kHz test tone -> compare against an unfrozen run.
    d.reset();
    std::vector<float> zero(static_cast<size_t>(4 * sec), 0.0f), z1, z2;
    runStereo(d, zero, zero, z1, z2, 256, p.data(), fz);
    runStereo(d, loud, loud, z1, z2, 256, p.data(), fz);
    CM_EXPECT(fxtest::maxAbs(z1, static_cast<size_t>(sec)) < 1e-3f);
}

CM_TEST(delay_block_size_independence) {
    const int n = 48000;
    fxtest::Lcg rng;
    std::vector<float> inL(n), inR(n);
    for (int i = 0; i < n; ++i) { inL[i] = 0.5f * rng.next(); inR[i] = 0.5f * rng.next(); }
    for (int mode = 0; mode < 3; ++mode) {
        auto p = delayParams(123.4f, 0.7f, mode);
        p[DP::Diffusion] = 0.6f; p[DP::Offset] = 7.0f;
        DigitalDelay d;
        d.prepare(48000.0, n);
        std::vector<float> rL, rR, oL, oR;
        runStereo(d, inL, inR, rL, rR, n, p.data(), FxContext {});
        for (int b : { 1, 64, 512, 2048 }) {
            d.reset();
            runStereo(d, inL, inR, oL, oR, b, p.data(), FxContext {});
            CM_EXPECT(fxtest::maxDiff(oL, rL) <= 1e-5f && fxtest::maxDiff(oR, rR) <= 1e-5f);
        }
    }
}

CM_TEST(delay_time_change_glides_without_clicks) {
    DigitalDelay d;
    d.prepare(48000.0, 128);
    auto p = delayParams(350.0f);
    const int n = 48000 * 2;
    auto in = fxtest::sine(n, 48000.0, 220.0, 0.5f);
    std::vector<float> oL(n), oR(n);
    for (int pos = 0; pos < n; pos += 128) {
        if (pos == 48000) p[DP::TimeMs] = 97.0f;
        if (pos == 60032) p[DP::Offset] = 20.0f;
        d.process(in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, 128, p.data(), FxContext {});
    }
    const float sineStep = 0.5f * 2.0f * 3.14159265f * 220.0f / 48000.0f;
    CM_EXPECT(fxtest::maxJump(oL, 16801) < 2.5f * sineStep);
    CM_EXPECT(fxtest::maxJump(oR, 16801) < 2.5f * sineStep);
}

CM_TEST(delay_bypass_fades_then_clears) {
    DigitalDelay d;
    d.prepare(48000.0, 64);
    auto p = delayParams(50.0f, 0.6f);
    const int n = 48000;
    auto in = fxtest::sine(n, 48000.0, 200.0, 0.5f);
    std::vector<float> oL(n), oR(n);
    for (int pos = 0; pos < n; pos += 64) {
        if (pos == 24000) p[DP::Bypass] = 1.0f;
        d.process(in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, 64, p.data(), FxContext {});
    }
    const float sineStep = 0.5f * 2.0f * 3.14159265f * 200.0f / 48000.0f;
    CM_EXPECT(fxtest::maxJump(oL, 2401) < 4.0f * sineStep);
    CM_EXPECT(fxtest::maxAbs(oL, 24000 + 600) == 0.0f);
    // Leaving bypass starts from cleared buffers: silence in -> exact silence out.
    p[DP::Bypass] = 0.0f;
    std::vector<float> zero(4800, 0.0f), zL, zR;
    runStereo(d, zero, zero, zL, zR, 64, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(zL) == 0.0f && fxtest::maxAbs(zR) == 0.0f);
}

CM_TEST(delay_nan_inf_safety) {
    DigitalDelay d;
    d.prepare(96000.0, 256);
    auto p = delayParams(5.0f, 0.9f, 1);
    std::vector<float> in(20000, 0.25f), oL, oR;
    for (size_t i = 0; i < in.size(); i += 97) in[i] = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 50; i < in.size(); i += 101) in[i] = std::numeric_limits<float>::infinity();
    runStereo(d, in, in, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
    std::vector<float> bad(DP::Count, std::numeric_limits<float>::quiet_NaN());
    runStereo(d, in, in, oL, oR, 256, bad.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
    runStereo(d, in, in, oL, oR, 256, nullptr, FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
}

// ------------------------------------------------------------------------------------------------ EchoThrow
namespace {
std::vector<float> echoParams() {
    auto p = fxtest::defaults(EP::defs);
    p[EP::TimeMs] = 100.0f; p[EP::CaptureMs] = 50.0f; p[EP::Repeats] = 4.0f; p[EP::Decay] = 0.5f;
    p[EP::Hpf] = 20.0f; p[EP::Lpf] = 20000.0f;
    return p;
}
// Runs `blocks` blocks of 64 with input from `in` starting at `pos`.
void echoRun(EchoThrow& e, const std::vector<float>& in, std::vector<float>& out, int from, int count, const float* p,
             const FxContext& ctx = {}) {
    std::vector<float> tmpR(64);
    for (int pos = from; pos < from + count; pos += 64)
        e.process(in.data() + pos, in.data() + pos, out.data() + pos, tmpR.data(), 64, p, ctx);
}
// One silent block outside the measured buffers (latches Trigger low after reset()).
void echoLatch(EchoThrow& e, const float* p, const FxContext& ctx = {}) {
    std::vector<float> z(64, 0.0f), oL(64), oR(64);
    e.process(z.data(), z.data(), oL.data(), oR.data(), 64, p, ctx);
}
}

CM_TEST(echo_throw_capture_repeats_and_decay) {
    EchoThrow e;
    e.prepare(48000.0, 64);
    auto p = echoParams();
    const int n = 48000;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[1000] = 1.0f;     // inside the 50 ms (2400 sample) capture window
    in[3000] = 1.0f;     // after the capture window -> never repeated
    p[EP::Trigger] = 0.0f;
    echoLatch(e, p.data());
    p[EP::Trigger] = 1.0f;                       // rising edge at sample 0, held afterwards
    echoRun(e, in, out, 0, n, p.data());
    CM_EXPECT(fxtest::maxAbs(out, 0, 4800) == 0.0f);
    double E[6] = {};
    for (int k = 1; k <= 5; ++k) E[k] = fxtest::energy(out, 1000 + k * 4800 - 50, 1000 + k * 4800 + 50);
    CM_EXPECT(E[1] > 0.5);
    for (int k = 1; k < 4; ++k) CM_EXPECT_NEAR(E[k + 1] / E[k], 0.25, 0.02);
    CM_EXPECT(E[5] < 1e-4 * E[1]);
    // The second impulse (outside capture) never appears.
    CM_EXPECT(fxtest::energy(out, 3000 + 4800 - 50, 3000 + 4800 + 50) < 1e-4 * E[1]);
    // Held trigger did not retrigger; after the throw the return is exact digital silence.
    CM_EXPECT(e.getActiveThrows() == 0);
    CM_EXPECT(fxtest::maxAbs(out, 4 * 4800 + 2400 + 6000) == 0.0f);
}

CM_TEST(echo_throw_retrigger_and_tail_kill) {
    EchoThrow e;
    e.prepare(48000.0, 64);
    auto p = echoParams();
    p[EP::Repeats] = 16.0f; p[EP::Decay] = 0.95f; p[EP::CaptureMs] = 400.0f;
    const int n = 48000 * 2;
    fxtest::Lcg rng;
    std::vector<float> in(n), out(n, 0.0f);
    for (auto& v : in) v = 0.5f * rng.next();
    echoRun(e, in, out, 0, 64, p.data());              // latch trigger low
    p[EP::Trigger] = 1.0f;
    echoRun(e, in, out, 64, 64, p.data());
    CM_EXPECT(e.getActiveThrows() == 1 && e.isCapturing());
    p[EP::Trigger] = 0.0f;
    echoRun(e, in, out, 128, 64, p.data());
    p[EP::Trigger] = 1.0f;                             // second press -> second throw
    echoRun(e, in, out, 192, 64 * 300, p.data());
    CM_EXPECT(e.getActiveThrows() == 2);
    const int killAt = 192 + 64 * 300;
    const float before = fxtest::maxAbs(out, static_cast<size_t>(killAt - 4800), static_cast<size_t>(killAt));
    CM_EXPECT(before > 0.05f);
    p[EP::TailKill] = 1.0f;
    echoRun(e, in, out, killAt, n - killAt - 64 * 10, p.data());
    CM_EXPECT(e.getActiveThrows() == 0);
    // Faded within 40 ms (-60 dB by 50 ms incl. the tone-filter ring), exact silence after the idle reset (100 ms).
    CM_EXPECT(fxtest::maxAbs(out, static_cast<size_t>(killAt + 2400), static_cast<size_t>(killAt + 2048 + 4800)) < 1e-3f * before);
    CM_EXPECT(fxtest::maxAbs(out, static_cast<size_t>(killAt + 2048 + 4800 + 64), static_cast<size_t>(n - 64 * 10)) == 0.0f);
}

CM_TEST(echo_throw_freeze_extends_repeats) {
    EchoThrow e;
    e.prepare(48000.0, 64);
    auto p = echoParams();
    p[EP::Repeats] = 2.0f; p[EP::Decay] = 0.8f;
    const int n = 48000 * 2;
    std::vector<float> in(n, 0.0f), out(n, 0.0f);
    in[1000] = 1.0f;
    FxContext fz; fz.freeze = true;
    echoLatch(e, p.data(), fz);
    p[EP::Trigger] = 1.0f;
    echoRun(e, in, out, 0, 48000, p.data(), fz);
    CM_EXPECT(fxtest::energy(out, 1000 + 6 * 4800 - 50, 1000 + 6 * 4800 + 50) > 0.1);   // beyond Repeats while frozen
    echoRun(e, in, out, 48000, n - 48000, p.data(), FxContext {});
    CM_EXPECT(e.getActiveThrows() == 0);
    CM_EXPECT(fxtest::maxAbs(out, 48000 + 4800 + 2400 + 6000) == 0.0f);
}

CM_TEST(echo_throw_block_independent_and_safe) {
    const int n = 48000;
    fxtest::Lcg rng;
    std::vector<float> in(n);
    for (auto& v : in) v = rng.next();
    auto p = echoParams();
    p[EP::Trigger] = 1.0f;   // latched at first call: no throw
    EchoThrow e;
    e.prepare(48000.0, n);
    std::vector<float> oL, oR;
    runStereo(e, in, in, oL, oR, 512, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL) == 0.0f);   // restored "on" state never fires
    // NaN input and parameters
    in[10] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> bad(EP::Count, std::numeric_limits<float>::infinity());
    e.reset();
    runStereo(e, in, in, oL, oR, 512, bad.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
}

// ------------------------------------------------------------------------------------------------ TapeDelay
namespace {
std::vector<float> tapeParams() {
    auto p = fxtest::defaults(TP::defs);
    p[TP::Feedback] = 0.0f; p[TP::Wow] = 0.0f; p[TP::Flutter] = 0.0f; p[TP::Age] = 0.0f; p[TP::Noise] = 0.0f;
    p[TP::Drive] = 0.0f; p[TP::LowCut] = 20.0f; p[TP::HighCut] = 20000.0f; p[TP::Heads] = 0.0f;
    return p;
}
// Relative peak-to-peak period deviation measured from upward zero crossings.
double periodDeviation(const std::vector<float>& v, size_t from) {
    std::vector<double> xs;
    for (size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f) xs.push_back(static_cast<double>(i - 1) + v[i - 1] / (v[i - 1] - v[i]));
    double mn = 1e9, mx = 0.0, sum = 0.0;
    for (size_t i = 1; i < xs.size(); ++i) { const double d = xs[i] - xs[i - 1]; mn = std::min(mn, d); mx = std::max(mx, d); sum += d; }
    const double mean = sum / static_cast<double>(xs.size() - 1);
    return (mx - mn) / mean;
}
}

CM_TEST(tape_delay_stability_and_saturation_bound) {
    for (double sr : { 44100.0, 96000.0 }) {
        TapeDelay t;
        t.prepare(sr, 512);
        auto p = tapeParams();
        p[TP::Feedback] = 3.0f; p[TP::Drive] = 1.0f; p[TP::Wow] = 1.0f; p[TP::Flutter] = 1.0f; p[TP::Age] = 1.0f;
        p[TP::Noise] = 1.0f; p[TP::TimeMs] = 20.0f; p[TP::Heads] = 1.0f;
        fxtest::Lcg rng;
        const int n = static_cast<int>(sr * 8);
        std::vector<float> in(static_cast<size_t>(n)), oL, oR;
        for (auto& v : in) v = 100.0f * rng.next();
        runStereo(t, in, in, oL, oR, 512, p.data(), FxContext {});
        CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
        CM_EXPECT(fxtest::maxAbs(oL) < 1.5f && fxtest::maxAbs(oR) < 1.5f);
        // Drive 0: still bounded by the record-head tanh.
        p[TP::Drive] = 0.0f;
        t.reset();
        runStereo(t, in, in, oL, oR, 512, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxAbs(oL) < 1.5f);
        // Silence afterwards decays (feedback < 0.92 effective): only hiss remains.
        std::vector<float> zero(static_cast<size_t>(sr * 3), 0.0f);
        runStereo(t, zero, zero, oL, oR, 512, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxAbs(oL, static_cast<size_t>(sr * 2)) < 0.01f);
    }
}

CM_TEST(tape_delay_time_and_wow_depth) {
    const double sr = 48000.0;
    TapeDelay t;
    t.prepare(sr, 256);
    auto p = tapeParams();
    // Clean: impulse arrives at the programmed time.
    std::vector<float> imp(30000, 0.0f), oL, oR;
    imp[0] = 1.0f;
    runStereo(t, imp, imp, oL, oR, 256, p.data(), FxContext {});
    size_t peak = 0;
    for (size_t i = 0; i < oL.size(); ++i) if (std::fabs(oL[i]) > std::fabs(oL[peak])) peak = i;
    CM_EXPECT(std::abs(static_cast<int>(peak) - static_cast<int>(std::lround(0.38 * sr))) <= 2);

    const int n = static_cast<int>(sr * 5);
    auto in = fxtest::sine(n, sr, 1000.0, 0.3f);
    t.reset();
    runStereo(t, in, in, oL, oR, 256, p.data(), FxContext {});
    const double clean = periodDeviation(oL, static_cast<size_t>(sr));
    CM_EXPECT(clean < 0.002);
    p[TP::Wow] = 1.0f;
    t.reset();
    runStereo(t, in, in, oL, oR, 256, p.data(), FxContext {});
    const double wow = periodDeviation(oL, static_cast<size_t>(sr));
    CM_EXPECT(wow > 0.008 && wow < 0.04);   // ~ +/-0.94 % pitch -> ~1.9 % p-p
    p[TP::Wow] = 0.0f; p[TP::Flutter] = 1.0f;
    t.reset();
    runStereo(t, in, in, oL, oR, 256, p.data(), FxContext {});
    const double fl = periodDeviation(oL, static_cast<size_t>(sr));
    CM_EXPECT(fl > 0.004 && fl < 0.04);
}

CM_TEST(tape_delay_deterministic_and_block_independent) {
    const int n = 48000;
    fxtest::Lcg rng;
    std::vector<float> in(n);
    for (auto& v : in) v = 0.5f * rng.next();
    auto p = fxtest::defaults(TP::defs);   // defaults include wow/flutter/age/noise
    TapeDelay t;
    t.prepare(48000.0, n);
    std::vector<float> rL, rR, oL, oR;
    runStereo(t, in, in, rL, rR, n, p.data(), FxContext {});
    for (int b : { 1, 64, 512, 2048 }) {
        t.reset();
        runStereo(t, in, in, oL, oR, b, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxDiff(oL, rL) <= 1e-5f && fxtest::maxDiff(oR, rR) <= 1e-5f);
    }
    // Bypass: fades then exact zeros.
    p[TP::Bypass] = 1.0f;
    runStereo(t, in, in, oL, oR, 64, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL, 960) == 0.0f);
    CM_EXPECT(fxtest::allFinite(oL));
}
