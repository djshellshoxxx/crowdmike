// StadiumReverb (EFFECTS_SPEC section 19): stability fuzz, bounded output, RT60 accuracy, reset / block-size determinism,
// freeze, tail kill, bypass, pre-delay (incl. tempo sync), width, low-cut, HF damping, NaN/Inf robustness.
#include "TestHarness.h"
#include "FxTestUtil.h"
#include "DSP/StadiumReverb.h"

using namespace crowdmike;
using namespace crowdmike::dsp;
using fxtest::runStereo;
namespace RP = params::ReverbP;

namespace {
const double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
const int kBlocks[] = { 1, 7, 16, 64, 333, 512, 2048 };

std::vector<float> verbParams() {
    auto p = fxtest::defaults(RP::defs);
    p[RP::On] = 1.0f;
    return p;
}
// Plain measurement settings: no HF damping, no low cut, no pre-delay, wet 0 dB.
std::vector<float> flatParams(float decay, float size) {
    auto p = verbParams();
    p[RP::Decay] = decay; p[RP::Size] = size; p[RP::PreDelay] = 0.0f; p[RP::HfDamp] = 20000.0f; p[RP::LowCut] = 20.0f;
    p[RP::Wet] = 0.0f; p[RP::Width] = 1.0f;
    return p;
}
std::vector<float> impulse(int n) { std::vector<float> v(static_cast<size_t>(n), 0.0f); v[0] = 1.0f; return v; }
std::vector<float> noise(int n, uint32_t seed, float amp = 0.5f) {
    fxtest::Lcg r; r.s = seed;
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = amp * r.next();
    return v;
}

// Cascaded 2 x 2nd-order Butterworth low-pass (4th order) used to look at the mid band only.
std::vector<float> lowpass(const std::vector<float>& x, double sr, float hz) {
    fx::FxBiquad a, b;
    a.setCoeffs(fx::fxDesignLowpass(sr, hz)); b.setCoeffs(fx::fxDesignLowpass(sr, hz));
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = b.process(a.process(x[i]));
    return y;
}
std::vector<float> highpass(const std::vector<float>& x, double sr, float hz) {
    fx::FxBiquad a, b;
    a.setCoeffs(fx::fxDesignHighpass(sr, hz)); b.setCoeffs(fx::fxDesignHighpass(sr, hz));
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = b.process(a.process(x[i]));
    return y;
}

// Schroeder backward-integrated decay curve, least-squares slope between -5 and -25 dB, extrapolated to 60 dB.
double measureRt60(const std::vector<float>& y, double sr) {
    std::vector<double> edc(y.size());
    double acc = 0.0;
    for (size_t i = y.size(); i-- > 0;) { acc += static_cast<double>(y[i]) * y[i]; edc[i] = acc; }
    if (edc[0] <= 0.0) return -1.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
    for (size_t i = 0; i < y.size(); ++i) {
        const double db = 10.0 * std::log10(std::max(edc[i], 1e-300) / edc[0]);
        if (db > -5.0) continue;
        if (db < -25.0) break;
        const double t = static_cast<double>(i) / sr;
        sx += t; sy += db; sxx += t * t; sxy += t * db; ++n;
    }
    if (n < 16) return -1.0;
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : -1.0;
}
int firstAbove(const std::vector<float>& v, float thr) {
    for (size_t i = 0; i < v.size(); ++i) if (std::fabs(v[i]) > thr) return static_cast<int>(i);
    return -1;
}
double rms(const std::vector<float>& v, size_t a, size_t b) { return std::sqrt(fxtest::energy(v, a, b) / std::max<size_t>(1, std::min(b, v.size()) - a)); }

// Random parameter vector covering the whole legal range, with occasional hard extremes and non-finite values.
std::vector<float> fuzzParams(fxtest::Lcg& r) {
    auto p = verbParams();
    const auto u = [&r] { return 0.5f * (r.next() + 1.0f); };
    for (int i = 0; i < RP::Count; ++i) {
        const auto& d = RP::defs[i];
        float v;
        if (d.kind == params::Kind::Bool) v = u() > 0.5f ? 1.0f : 0.0f;
        else if (d.kind == params::Kind::Choice) v = std::floor(u() * 13.0f);
        else {
            const float pick = u();
            v = pick < 0.15f ? d.lo : (pick < 0.3f ? d.hi : d.lo + u() * (d.hi - d.lo));
            if (u() < 0.02f) v = r.next() > 0.0f ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            if (u() < 0.02f) v = r.next() * 1.0e9f;   // far out of range
        }
        p[static_cast<size_t>(i)] = v;
    }
    p[RP::On] = u() < 0.9f ? 1.0f : 0.0f;
    return p;
}
}

CM_TEST(reverb_param_defaults_off_and_sane) {
    CM_EXPECT(RP::defs[RP::On].def == 0.0f);
    for (int i = 0; i < RP::Count; ++i) {
        const auto& d = RP::defs[i];
        if (d.kind == params::Kind::Float) CM_EXPECT(d.def >= d.lo && d.def <= d.hi);
    }
    StadiumReverb v;
    v.prepare(48000.0, 256);
    auto p = fxtest::defaults(RP::defs);   // On = 0
    std::vector<float> in = noise(4800, 1), oL, oR;
    runStereo(v, in, in, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL) == 0.0f && fxtest::maxAbs(oR) == 0.0f);
}

CM_TEST(reverb_stability_fuzz_all_rates_and_blocks) {
    fxtest::Lcg r; r.s = 0xC0FFEEu;
    for (double sr : kRates) {
        StadiumReverb v;
        v.prepare(sr, 2048);
        for (int iter = 0; iter < 14; ++iter) {
            const int block = kBlocks[iter % 7];
            const int total = static_cast<int>(sr * 0.6);
            std::vector<float> inL(static_cast<size_t>(total)), inR(static_cast<size_t>(total));
            for (int i = 0; i < total; ++i) {
                const float burst = ((i / 4096) % 3 == 0) ? 4.0f : 1.0f;   // loud bursts (up to +12 dBFS) and spikes
                inL[static_cast<size_t>(i)] = burst * r.next();
                inR[static_cast<size_t>(i)] = burst * r.next();
            }
            if (iter % 5 == 0) { inL[100] = 1.0e4f; inR[200] = -1.0e30f; inL[300] = std::numeric_limits<float>::quiet_NaN(); inR[400] = std::numeric_limits<float>::infinity(); }
            // parameters change every 0.1 s while running (automation / scene recall), including the extremes
            std::vector<float> oL(inL.size()), oR(inR.size());
            for (int pos = 0, seg = 0; pos < total; ++seg) {
                const auto p = fuzzParams(r);
                const int segEnd = std::min(total, pos + static_cast<int>(sr * 0.1));
                for (; pos < segEnd; pos += block) {
                    const int n = std::min(block, segEnd - pos);
                    v.process(inL.data() + pos, inR.data() + pos, oL.data() + pos, oR.data() + pos, n, p.data(), FxContext {});
                }
            }
            CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
            // hard bound: state <= 8, 8 taps / sqrt 8, norm <= 2, wet <= +12 dB, width/low-cut |H| <= 1 (+ biquad overshoot)
            CM_EXPECT(fxtest::maxAbs(oL) < 260.0f && fxtest::maxAbs(oR) < 260.0f);
        }
    }
}

CM_TEST(reverb_extreme_settings_stay_bounded_with_sustained_loud_input) {
    for (double sr : { 44100.0, 192000.0 }) {
        StadiumReverb v;
        v.prepare(sr, 512);
        auto p = verbParams();
        p[RP::Decay] = 20.0f; p[RP::Size] = 1.0f; p[RP::Wet] = 12.0f; p[RP::HfDamp] = 20000.0f; p[RP::LowCut] = 20.0f;
        p[RP::Diffusion] = 1.0f; p[RP::ModDepth] = 1.0f; p[RP::ModRate] = 5.0f;
        for (int mode = 0; mode < 2; ++mode) {
            p[RP::Freeze] = mode == 1 ? 1.0f : 0.0f;
            const int total = static_cast<int>(sr * 6.0);
            auto in = noise(total, 7u + static_cast<uint32_t>(mode), 8.0f);   // +18 dBFS white noise for 6 s
            if (mode == 1) std::fill(in.begin() + static_cast<long>(sr * 0.5), in.end(), 0.0f);
            std::vector<float> oL, oR;
            runStereo(v, in, in, oL, oR, 512, p.data(), FxContext {});
            CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
            CM_EXPECT(fxtest::maxAbs(oL) < 260.0f && fxtest::maxAbs(oR) < 260.0f);
        }
    }
}

CM_TEST(reverb_rt60_matches_decay_within_20_percent) {
    struct Case { double sr; float decay, size; };
    std::vector<Case> cases;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float decay : { 0.5f, 2.0f, 8.0f })
            for (float size : { 0.3f, 1.0f }) cases.push_back({ sr, decay, size });
    cases.push_back({ 192000.0, 1.0f, 0.6f });
    cases.push_back({ 48000.0, 20.0f, 0.8f });
    cases.push_back({ 48000.0, 0.25f, 0.1f });
    for (const auto& c : cases) {
        StadiumReverb v;
        v.prepare(c.sr, 512);
        const int total = static_cast<int>(c.sr * (c.decay * 1.3 + 0.6));
        const auto in = impulse(total);
        auto p = flatParams(c.decay, c.size);
        std::vector<float> oL, oR;
        runStereo(v, in, std::vector<float>(in.size(), 0.0f), oL, oR, 512, p.data(), FxContext {});
        // The Decay control defines the mid-band reverberation time: look below 2 kHz.
        const double rt = measureRt60(lowpass(oL, c.sr, 2000.0f), c.sr);
        const double rtR = measureRt60(lowpass(oR, c.sr, 2000.0f), c.sr);
        if (!(std::fabs(rt / c.decay - 1.0) <= 0.20 && std::fabs(rtR / c.decay - 1.0) <= 0.20))
            std::cerr << "  RT60 case sr=" << c.sr << " decay=" << c.decay << " size=" << c.size << " measured L=" << rt << " R=" << rtR << '\n';
        CM_EXPECT(std::fabs(rt / c.decay - 1.0) <= 0.20);
        CM_EXPECT(std::fabs(rtR / c.decay - 1.0) <= 0.20);
    }
}

CM_TEST(reverb_decay_is_independent_of_size_and_level_is_normalised) {
    // Energy-normalised: the total impulse-response energy stays within +/-4 dB over the whole Decay x Size range.
    double lo = 1e30, hi = 0.0;
    for (float decay : { 0.3f, 1.0f, 4.0f, 12.0f })
        for (float size : { 0.0f, 0.5f, 1.0f }) {
            StadiumReverb v;
            v.prepare(48000.0, 512);
            const int total = static_cast<int>(48000.0 * (decay * 1.6 + 0.5));
            auto in = impulse(total);
            auto p = flatParams(decay, size);
            std::vector<float> oL, oR;
            runStereo(v, in, std::vector<float>(in.size(), 0.0f), oL, oR, 512, p.data(), FxContext {});
            const double e = fxtest::energy(oL, 0, oL.size());
            lo = std::min(lo, e); hi = std::max(hi, e);
        }
    CM_EXPECT(10.0 * std::log10(hi / lo) < 8.0);
}

CM_TEST(reverb_reset_is_deterministic_and_block_size_independent) {
    const double sr = 48000.0;
    const int total = static_cast<int>(sr * 2.0);
    const auto in = noise(total, 99u, 0.7f);
    auto p = verbParams();
    p[RP::PreDelay] = 33.3f; p[RP::Size] = 0.7f; p[RP::Decay] = 3.7f; p[RP::ModDepth] = 0.8f; p[RP::ModRate] = 1.7f;
    std::vector<float> ref, refR, tmpR;
    {
        StadiumReverb v;
        v.prepare(sr, 2048);
        runStereo(v, in, in, ref, refR, 2048, p.data(), FxContext {});
        // reset() on the same instance reproduces the first run bit for bit
        v.reset();
        std::vector<float> again, againR;
        runStereo(v, in, in, again, againR, 2048, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxDiff(ref, again) == 0.0f && fxtest::maxDiff(refR, againR) == 0.0f);
    }
    for (int block : kBlocks) {
        StadiumReverb v;
        v.prepare(sr, 2048);
        std::vector<float> o, oR;
        runStereo(v, in, in, o, oR, block, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxDiff(ref, o) == 0.0f);
        CM_EXPECT(fxtest::maxDiff(refR, oR) == 0.0f);
    }
    CM_EXPECT(fxtest::maxAbs(ref) > 0.01f);
}

CM_TEST(reverb_pre_delay_and_tempo_sync) {
    const double sr = 48000.0;
    const auto in = impulse(static_cast<int>(sr * 1.0));
    const std::vector<float> zero(in.size(), 0.0f);
    for (float ms : { 0.0f, 50.0f, 200.0f }) {
        StadiumReverb v;
        v.prepare(sr, 256);
        auto p = flatParams(2.0f, 0.5f);
        p[RP::PreDelay] = ms;
        std::vector<float> oL, oR;
        runStereo(v, in, zero, oL, oR, 256, p.data(), FxContext {});
        const int first = firstAbove(oL, 1.0e-6f);
        CM_EXPECT(first >= static_cast<int>(std::lround(ms * 0.001 * sr)));
        CM_EXPECT(first < static_cast<int>(std::lround(ms * 0.001 * sr)) + static_cast<int>(sr * 0.15));
    }
    // tempo sync: 1/16 at 120 bpm = 125 ms; 1/4 = 500 ms clamps to the 250 ms maximum; without a tempo the ms value is used
    struct Sync { int div; bool hasTempo; double expectMs; };
    for (const Sync& s : { Sync { 2, true, 125.0 }, Sync { 8, true, 250.0 }, Sync { 2, false, 10.0 } }) {
        StadiumReverb v;
        v.prepare(sr, 256);
        auto p = flatParams(2.0f, 0.3f);
        p[RP::PreDelay] = 10.0f; p[RP::PreSync] = 1.0f; p[RP::PreDiv] = static_cast<float>(s.div);
        FxContext ctx; ctx.bpm = 120.0; ctx.hasTempo = s.hasTempo;
        std::vector<float> oL, oR;
        runStereo(v, in, zero, oL, oR, 256, p.data(), ctx);
        const int first = firstAbove(oL, 1.0e-6f);
        CM_EXPECT(first >= static_cast<int>(std::lround(s.expectMs * 0.001 * sr)));
        CM_EXPECT(first < static_cast<int>(std::lround(s.expectMs * 0.001 * sr)) + static_cast<int>(sr * 0.1));
    }
}

CM_TEST(reverb_width_and_decorrelation) {
    const double sr = 48000.0;
    const auto in = noise(static_cast<int>(sr * 2), 5u);
    auto p = flatParams(2.0f, 0.6f);
    StadiumReverb v;
    v.prepare(sr, 256);
    p[RP::Width] = 0.0f;
    std::vector<float> oL, oR;
    runStereo(v, in, in, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(oL, oR) < 1.0e-6f);
    CM_EXPECT(fxtest::maxAbs(oL) > 0.01f);
    // full width: L/R of the wet signal are decorrelated (|correlation| small)
    StadiumReverb w;
    w.prepare(sr, 256);
    p[RP::Width] = 1.0f;
    runStereo(w, in, in, oL, oR, 256, p.data(), FxContext {});
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = static_cast<size_t>(sr * 0.5); i < oL.size(); ++i) { lr += oL[i] * oR[i]; ll += oL[i] * oL[i]; rr += oR[i] * oR[i]; }
    CM_EXPECT(std::fabs(lr / std::sqrt(ll * rr)) < 0.35);
}

CM_TEST(reverb_low_cut_and_hf_damping_shape_the_wet) {
    const double sr = 48000.0;
    const int total = static_cast<int>(sr * 10.0);
    const auto in = impulse(total);   // impulse response energy: independent of how far a steady-state build-up has progressed
    const auto level = [&](float lowCut, float hf, float lo, float hi) {
        StadiumReverb v;
        v.prepare(sr, 256);
        auto p = flatParams(8.0f, 0.6f);
        p[RP::LowCut] = lowCut; p[RP::HfDamp] = hf;
        std::vector<float> oL, oR;
        runStereo(v, in, in, oL, oR, 256, p.data(), FxContext {});
        auto band = oL;
        if (hi > 0.0f) band = lowpass(band, sr, hi);
        if (lo > 0.0f) band = highpass(band, sr, lo);
        return rms(band, 0, band.size());
    };
    // low cut: 500 Hz removes the low band (below 150 Hz) by > 12 dB relative to 20 Hz
    const double lowOpen = level(20.0f, 20000.0f, 0.0f, 150.0f), lowCut = level(500.0f, 20000.0f, 0.0f, 150.0f);
    CM_EXPECT(20.0 * std::log10(lowOpen / lowCut) > 12.0);
    // HF damping: 2 kHz damping lowers the band above 6 kHz by > 6 dB relative to 20 kHz and keeps the low band
    const double hfOpen = level(20.0f, 20000.0f, 6000.0f, 0.0f), hfDamped = level(20.0f, 2000.0f, 6000.0f, 0.0f);
    CM_EXPECT(20.0 * std::log10(hfOpen / hfDamped) > 6.0);
    const double lfOpen = level(20.0f, 20000.0f, 0.0f, 1000.0f), lfDamped = level(20.0f, 2000.0f, 0.0f, 1000.0f);
    CM_EXPECT(std::fabs(20.0 * std::log10(lfOpen / lfDamped)) < 3.0);
}

CM_TEST(reverb_freeze_holds_a_bounded_tail) {
    for (double sr : { 48000.0, 96000.0 }) {
        StadiumReverb v;
        v.prepare(sr, 512);
        auto p = verbParams();
        p[RP::Decay] = 1.5f; p[RP::Size] = 0.8f; p[RP::ModDepth] = 0.6f; p[RP::HfDamp] = 6000.0f; p[RP::Wet] = 0.0f;
        const int total = static_cast<int>(sr * 40.0);
        auto in = noise(total, 21u, 0.6f);
        std::fill(in.begin() + static_cast<long>(sr * 1.0), in.end(), 0.0f);   // 1 s of noise, then silence
        std::vector<float> oL(in.size()), oR(in.size());
        const int block = 512;
        for (int pos = 0; pos < total; pos += block) {
            const int n = std::min(block, total - pos);
            p[RP::Freeze] = pos >= static_cast<int>(sr * 1.0) ? 1.0f : 0.0f;   // freeze right as the input stops
            v.process(in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n, p.data(), FxContext {});
        }
        CM_EXPECT(fxtest::allFinite(oL) && fxtest::maxAbs(oL) < 260.0f);
        const double early = rms(oL, static_cast<size_t>(sr * 15.0), static_cast<size_t>(sr * 16.0));
        const double late = rms(oL, static_cast<size_t>(sr * 38.0), static_cast<size_t>(sr * 39.0));
        CM_EXPECT(early > 1.0e-3);
        // the held tail is steady: 23 s later (spectrum settled; only the linear-interpolation HF loss remains) it is within 2.5 dB and never grows
        const double db = 20.0 * std::log10(late / early);
            CM_EXPECT(db < 1.5 && db > -2.5);
        // the global Freeze flag (FxContext) behaves the same as the parameter
        StadiumReverb g;
        g.prepare(sr, 512);
        p[RP::Freeze] = 0.0f;
        std::vector<float> aL(in.size()), aR(in.size());
        for (int pos = 0; pos < total; pos += block) {
            const int n = std::min(block, total - pos);
            FxContext ctx; ctx.freeze = pos >= static_cast<int>(sr * 1.0);
            g.process(in.data() + pos, in.data() + pos, aL.data() + pos, aR.data() + pos, n, p.data(), ctx);
        }
        CM_EXPECT(fxtest::maxDiff(aL, oL) < 1.0e-3f);
        // releasing the freeze lets the tail decay at the RT60 again
        p[RP::Freeze] = 0.0f;
        std::vector<float> rL(static_cast<size_t>(sr * 4.0)), rR(rL.size()), zeros(rL.size(), 0.0f);
        v.process(zeros.data(), zeros.data(), rL.data(), rR.data(), static_cast<int>(rL.size()), p.data(), FxContext {});
        CM_EXPECT(rms(rL, static_cast<size_t>(sr * 3.0), rL.size()) < 0.01 * late);
    }
}

CM_TEST(reverb_tail_kill_fades_fast_and_drains_the_loop) {
    const double sr = 48000.0;
    StadiumReverb v;
    v.prepare(sr, 256);
    auto p = verbParams();
    p[RP::Decay] = 12.0f; p[RP::Size] = 1.0f; p[RP::Wet] = 0.0f;
    const int blk = 256;
    auto in = noise(static_cast<int>(sr * 2.0), 3u, 0.5f);
    std::vector<float> o(in.size()), o2(in.size());
    for (size_t pos = 0; pos < in.size(); pos += blk) v.process(in.data() + pos, in.data() + pos, o.data() + pos, o2.data() + pos, blk, p.data(), FxContext {});
    const double before = rms(o, static_cast<size_t>(sr * 1.5), static_cast<size_t>(sr * 2.0));
    CM_EXPECT(before > 1.0e-3);
    p[RP::TailKill] = 1.0f;
    std::vector<float> zeros(static_cast<size_t>(sr * 0.5), 0.0f), k(zeros.size()), k2(zeros.size());
    for (size_t pos = 0; pos + blk <= zeros.size(); pos += blk) v.process(zeros.data(), zeros.data(), k.data() + pos, k2.data() + pos, blk, p.data(), FxContext {});
    CM_EXPECT(rms(k, static_cast<size_t>(sr * 0.08), static_cast<size_t>(sr * 0.12)) < 1.0e-3 * before);   // > 60 dB down within 80 ms
    CM_EXPECT(rms(k, static_cast<size_t>(sr * 0.3), zeros.size()) == 0.0);
    // released: nothing of the old tail comes back
    p[RP::TailKill] = 0.0f;
    std::vector<float> r(zeros.size()), r2(zeros.size());
    for (size_t pos = 0; pos + blk <= zeros.size(); pos += blk) v.process(zeros.data(), zeros.data(), r.data() + pos, r2.data() + pos, blk, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(r) < 1.0e-4f * static_cast<float>(before));
    // and it is usable again afterwards
    std::vector<float> back(in.size()), back2(in.size());
    for (size_t pos = 0; pos < in.size(); pos += blk) v.process(in.data() + pos, in.data() + pos, back.data() + pos, back2.data() + pos, blk, p.data(), FxContext {});
    CM_EXPECT(rms(back, static_cast<size_t>(sr * 1.5), static_cast<size_t>(sr * 2.0)) > 0.3 * before);
}

CM_TEST(reverb_bypass_fades_clears_and_restarts_cleanly) {
    const double sr = 48000.0;
    StadiumReverb v;
    v.prepare(sr, 256);
    auto p = verbParams();
    p[RP::Decay] = 6.0f; p[RP::Wet] = 0.0f;
    const auto in = noise(static_cast<int>(sr * 1.0), 17u, 0.5f);
    std::vector<float> a, aR;
    runStereo(v, in, in, a, aR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(a) > 0.01f);
    p[RP::On] = 0.0f;
    const std::vector<float> zeros(static_cast<size_t>(sr * 0.5), 0.0f);
    std::vector<float> off, offR;
    runStereo(v, zeros, zeros, off, offR, 256, p.data(), FxContext {});
    // fades over 10 ms, silent afterwards
    CM_EXPECT(fxtest::maxAbs(off, static_cast<size_t>(sr * 0.02)) == 0.0f);
    // switching back on: no stale tail, and the response equals a fresh instance's (deterministic restart)
    p[RP::On] = 1.0f;
    std::vector<float> silent, silentR;
    runStereo(v, zeros, zeros, silent, silentR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(silent) == 0.0f);
    std::vector<float> second, secondR, fresh, freshR;
    runStereo(v, in, in, second, secondR, 256, p.data(), FxContext {});
    StadiumReverb f;   // a reference that went through the same off period (a brand new instance starts On without the 10 ms fade-in)
    f.prepare(sr, 256);
    auto pOff = p; pOff[RP::On] = 0.0f;
    std::vector<float> d1, d2;
    runStereo(f, zeros, zeros, d1, d2, 256, pOff.data(), FxContext {});
    runStereo(f, zeros, zeros, d1, d2, 256, p.data(), FxContext {});   // and the same silent On period
    runStereo(f, in, in, fresh, freshR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(second, fresh) < 1.0e-5f);
}

CM_TEST(reverb_non_finite_and_extreme_values_are_safe) {
    StadiumReverb v;
    v.prepare(48000.0, 512);
    std::vector<float> bad(RP::Count, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> in(4800, std::numeric_limits<float>::infinity()), oL(4800), oR(4800);
    v.process(in.data(), in.data(), oL.data(), oR.data(), 4800, bad.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
    v.process(nullptr, nullptr, oL.data(), oR.data(), 4800, nullptr, FxContext {});   // null params => defaults (Off), null inputs => silence
    CM_EXPECT(fxtest::allFinite(oL));
    v.process(in.data(), in.data(), nullptr, nullptr, 100, bad.data(), FxContext {});   // null outputs: no crash
    FxContext ctx; ctx.bpm = std::numeric_limits<double>::quiet_NaN(); ctx.hasTempo = true;
    auto p = verbParams(); p[RP::PreSync] = 1.0f;
    v.process(in.data(), in.data(), oL.data(), oR.data(), 4800, p.data(), ctx);
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
}

CM_TEST(reverb_line_lengths_are_distinct_and_scale_with_size) {
    for (float size : { 0.0f, 0.5f, 1.0f }) {
        for (int i = 1; i < StadiumReverb::kLines; ++i) CM_EXPECT(StadiumReverb::lineMs(i, size) > StadiumReverb::lineMs(i - 1, size));
    }
    CM_EXPECT(StadiumReverb::lineMs(7, 1.0f) > 3.0f * StadiumReverb::lineMs(7, 0.2f));
    CM_EXPECT(StadiumReverb::lineMs(7, 1.0f) <= 132.0f && StadiumReverb::lineMs(0, 0.0f) >= 5.0f);
    CM_EXPECT_NEAR(StadiumReverb::tailSeconds(50.0f), 20.0, 1e-6);
}
