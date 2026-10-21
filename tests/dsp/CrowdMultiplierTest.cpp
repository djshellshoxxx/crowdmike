// CrowdMultiplier (EFFECTS_SPEC section 20): stability fuzz, normalisation (loudness bounded), determinism (reset, seed,
// block size), voice count changes, detune accuracy, mono compatibility, bypass, NaN/Inf robustness.
#include "TestHarness.h"
#include "FxTestUtil.h"
#include "DSP/CrowdMultiplier.h"

using namespace crowdmike;
using namespace crowdmike::dsp;
using fxtest::runStereo;
namespace MP = params::MultiP;

namespace {
const double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
const int kBlocks[] = { 1, 7, 16, 64, 333, 512, 2048 };

std::vector<float> multiParams(int voices = 8) {
    auto p = fxtest::defaults(MP::defs);
    p[MP::On] = 1.0f;
    p[MP::Voices] = static_cast<float>(voices);
    return p;
}
std::vector<float> noise(int n, uint32_t seed, float amp = 0.5f) {
    fxtest::Lcg r; r.s = seed;
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v) x = amp * r.next();
    return v;
}
double rms(const std::vector<float>& v, size_t a, size_t b) { return std::sqrt(fxtest::energy(v, a, b) / std::max<size_t>(1, std::min(b, v.size()) - a)); }
double goertzelPower(const std::vector<float>& x, size_t a, size_t b, double sr, double hz) {
    const double w = 2.0 * 3.14159265358979323846 * hz / sr, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = a; i < b; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
double bandPower(const std::vector<float>& x, size_t a, size_t b, double sr, double hz, double halfWidth) {
    double m = 0.0;
    for (double f = hz - halfWidth; f <= hz + halfWidth + 1e-9; f += 0.25) m = std::max(m, goertzelPower(x, a, b, sr, f));
    return m;
}
std::vector<float> fuzzParams(fxtest::Lcg& r) {
    auto p = multiParams();
    const auto u = [&r] { return 0.5f * (r.next() + 1.0f); };
    for (int i = 0; i < MP::Count; ++i) {
        const auto& d = MP::defs[i];
        float v;
        if (d.kind == params::Kind::Bool) v = u() > 0.5f ? 1.0f : 0.0f;
        else {
            const float pick = u();
            v = pick < 0.15f ? d.lo : (pick < 0.3f ? d.hi : d.lo + u() * (d.hi - d.lo));
            if (u() < 0.02f) v = r.next() > 0.0f ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
            if (u() < 0.02f) v = r.next() * 1.0e9f;
        }
        p[static_cast<size_t>(i)] = v;
    }
    p[MP::On] = u() < 0.9f ? 1.0f : 0.0f;
    return p;
}
}

CM_TEST(multi_params_default_off_and_traits_are_deterministic) {
    CM_EXPECT(MP::defs[MP::On].def == 0.0f);
    for (int i = 0; i < MP::Count; ++i) CM_EXPECT(MP::defs[i].def >= MP::defs[i].lo && MP::defs[i].def <= MP::defs[i].hi);
    for (int seed : { 1, 2, 77, 9999 }) {
        float lo = 1e9f, hi = -1e9f;
        for (int v = 0; v < CrowdMultiplier::kMaxVoices; ++v) {
            const auto a = CrowdMultiplier::traits(seed, v), b = CrowdMultiplier::traits(seed, v);
            CM_EXPECT(a.baseMs == b.baseMs && a.pan == b.pan && a.detune == b.detune && a.tone == b.tone && a.rateRatio == b.rateRatio);
            CM_EXPECT(a.baseMs >= 2.0f && a.baseMs <= 26.0f && a.pan >= -1.0f && a.pan <= 1.0f && a.rateRatio >= 0.7f && a.rateRatio <= 1.3f);
            CM_EXPECT(std::fabs(a.detune) >= 0.4f && std::fabs(a.detune) <= 1.0f && std::fabs(a.gainJitter) <= 1.0f && std::fabs(a.stereoOffset) <= 1.0f);
            CM_EXPECT((v & 1) == (a.detune > 0.0f ? 1 : 0));   // detune direction alternates: voice 0 down, 1 up, ...
            if (v < 6) { lo = std::min(lo, a.baseMs); hi = std::max(hi, a.baseMs); }
        }
        CM_EXPECT(hi - lo > 8.0f);   // the first voices already spread over the delay range (golden-ratio sequence)
    }
    CM_EXPECT(CrowdMultiplier::traits(1, 3).baseMs != CrowdMultiplier::traits(2, 3).baseMs);
    CM_EXPECT_NEAR(CrowdMultiplier::normalisation(4), 0.5, 1e-6);
    CM_EXPECT_NEAR(CrowdMultiplier::normalisation(16), 0.25, 1e-6);
    CrowdMultiplier m;
    m.prepare(48000.0, 256);
    auto p = fxtest::defaults(MP::defs);   // On = 0
    std::vector<float> in = noise(4800, 1), oL, oR;
    runStereo(m, in, in, oL, oR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL) == 0.0f && fxtest::maxAbs(oR) == 0.0f);
}

CM_TEST(multi_stability_fuzz_all_rates_and_blocks) {
    fxtest::Lcg r; r.s = 0xBADCAFEu;
    for (double sr : kRates) {
        CrowdMultiplier m;
        m.prepare(sr, 2048);
        for (int iter = 0; iter < 12; ++iter) {
            const int block = kBlocks[iter % 7];
            const int total = static_cast<int>(sr * 0.6);
            std::vector<float> inL(static_cast<size_t>(total)), inR(static_cast<size_t>(total));
            for (int i = 0; i < total; ++i) {
                const float burst = ((i / 4096) % 3 == 0) ? 4.0f : 1.0f;
                inL[static_cast<size_t>(i)] = burst * r.next();
                inR[static_cast<size_t>(i)] = burst * r.next();
            }
            if (iter % 4 == 0) { inL[100] = 1.0e4f; inR[200] = -1.0e30f; inL[300] = std::numeric_limits<float>::quiet_NaN(); inR[400] = std::numeric_limits<float>::infinity(); }
            std::vector<float> oL(inL.size()), oR(inR.size());
            for (int pos = 0; pos < total;) {
                const auto p = fuzzParams(r);
                const int segEnd = std::min(total, pos + static_cast<int>(sr * 0.1));
                for (; pos < segEnd; pos += block) {
                    const int n = std::min(block, segEnd - pos);
                    m.process(inL.data() + pos, inR.data() + pos, oL.data() + pos, oR.data() + pos, n, p.data(), FxContext {});
                }
            }
            CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
            // |out| <= 2.47 sqrt(16) * 64 (input clamp) * level(+12 dB)
            CM_EXPECT(fxtest::maxAbs(oL) < 2.47f * 4.0f * 64.0f * 4.0f && fxtest::maxAbs(oR) < 2.47f * 4.0f * 64.0f * 4.0f);
        }
    }
}

CM_TEST(multi_output_is_normalised_and_loudness_stays_bounded) {
    const double sr = 48000.0;
    const int total = static_cast<int>(sr * 4.0);
    const auto in = noise(total, 31u, 0.5f);
    double lowest = 1e9, highest = -1e9;
    for (int voices : { 2, 3, 4, 8, 12, 16 }) {
        CrowdMultiplier m;
        m.prepare(sr, 512);
        auto p = multiParams(voices);
        p[MP::Tone] = 0.0f;
        std::vector<float> oL, oR;
        runStereo(m, in, in, oL, oR, 512, p.data(), FxContext {});
        const double inRms = rms(in, static_cast<size_t>(sr), in.size());
        const double power = 0.5 * (rms(oL, static_cast<size_t>(sr), oL.size()) * rms(oL, static_cast<size_t>(sr), oL.size())
                                  + rms(oR, static_cast<size_t>(sr), oR.size()) * rms(oR, static_cast<size_t>(sr), oR.size()));
        const double gainDb = 10.0 * std::log10(power / (inRms * inRms));
        lowest = std::min(lowest, gainDb); highest = std::max(highest, gainDb);
        CM_EXPECT(gainDb < 0.5 && gainDb > -4.0);   // about unity for uncorrelated (crowd-like) material (linear ramp crossfades cost up to ~2 dB on noise)
    }
    CM_EXPECT(highest - lowest < 2.5);
    // fully coherent worst case (a sine) never exceeds 2.47 sqrt(N)
    CrowdMultiplier m;
    m.prepare(sr, 512);
    auto p = multiParams(16);
    p[MP::Scatter] = 1.0f; p[MP::Tone] = 1.0f;
    const auto s = fxtest::sine(total, sr, 220.0, 1.0f);
    std::vector<float> oL, oR;
    runStereo(m, s, s, oL, oR, 512, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(oL) < 2.47f * 4.0f && fxtest::maxAbs(oR) < 2.47f * 4.0f);
    // Level control
    CrowdMultiplier a, b;
    a.prepare(sr, 512); b.prepare(sr, 512);
    auto p0 = multiParams(8), p6 = multiParams(8);
    p6[MP::Level] = -6.0206f;
    std::vector<float> aL, aR, bL, bR;
    runStereo(a, in, in, aL, aR, 512, p0.data(), FxContext {});
    runStereo(b, in, in, bL, bR, 512, p6.data(), FxContext {});
    CM_EXPECT_NEAR(rms(bL, static_cast<size_t>(sr), bL.size()) / rms(aL, static_cast<size_t>(sr), aL.size()), 0.5, 0.005);
}

CM_TEST(multi_reset_seed_and_block_size_determinism) {
    const double sr = 48000.0;
    const auto in = noise(static_cast<int>(sr * 2.0), 77u, 0.6f);
    auto p = multiParams(10);
    p[MP::Depth] = 9.0f; p[MP::Rate] = 2.5f; p[MP::Detune] = 25.0f; p[MP::Scatter] = 0.9f; p[MP::Tone] = 0.8f; p[MP::Seed] = 1234.0f;
    std::vector<float> ref, refR;
    {
        CrowdMultiplier m;
        m.prepare(sr, 2048);
        runStereo(m, in, in, ref, refR, 2048, p.data(), FxContext {});
        m.reset();
        std::vector<float> again, againR;
        runStereo(m, in, in, again, againR, 2048, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxDiff(ref, again) == 0.0f && fxtest::maxDiff(refR, againR) == 0.0f);
    }
    for (int block : kBlocks) {
        CrowdMultiplier m;
        m.prepare(sr, 2048);
        std::vector<float> o, oR;
        runStereo(m, in, in, o, oR, block, p.data(), FxContext {});
        CM_EXPECT(fxtest::maxDiff(ref, o) == 0.0f && fxtest::maxDiff(refR, oR) == 0.0f);
    }
    // another seed gives a clearly different crowd; the same seed on another instance is identical
    CrowdMultiplier other;
    other.prepare(sr, 2048);
    auto p2 = p; p2[MP::Seed] = 1235.0f;
    std::vector<float> o2, o2R;
    runStereo(other, in, in, o2, o2R, 2048, p2.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(ref, o2) > 0.05f);
    CM_EXPECT(fxtest::maxAbs(ref) > 0.05f);
}

CM_TEST(multi_voice_count_changes_are_click_free_and_history_independent) {
    const double sr = 48000.0;
    const int total = static_cast<int>(sr * 3.0);
    const auto in = fxtest::sine(total, sr, 300.0, 0.5f);
    CrowdMultiplier m;
    m.prepare(sr, 256);
    auto p = multiParams(4);
    p[MP::Depth] = 0.0f; p[MP::Detune] = 0.0f;
    std::vector<float> oL(in.size()), oR(in.size());
    for (int pos = 0; pos < total; pos += 256) {
        const int n = std::min(256, total - pos);
        p[MP::Voices] = pos < total / 3 ? 4.0f : (pos < 2 * total / 3 ? 16.0f : 2.0f);
        m.process(in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n, p.data(), FxContext {});
    }
    CM_EXPECT(fxtest::allFinite(oL));
    // a 300 Hz sine can move at most ~2*pi*300/48000*A per sample; allow 3x for the summed voices
    CM_EXPECT(fxtest::maxJump(oL, 2000) < 3.0f * 2.0f * 3.1416f * 300.0f / 48000.0f * 2.5f * 4.0f * 0.5f);
    // voice v is identical for any Voices count: 2 voices after going 16 -> 2 equal a fresh 2-voice instance (once settled)
    CrowdMultiplier fresh;
    fresh.prepare(sr, 256);
    p[MP::Voices] = 2.0f;
    std::vector<float> fL(in.size()), fR(in.size());
    for (int pos = 0; pos < total; pos += 256) {
        const int n = std::min(256, total - pos);
        fresh.process(in.data() + pos, in.data() + pos, fL.data() + pos, fR.data() + pos, n, p.data(), FxContext {});
    }
    // Same traits and a static, deterministic LFO-free configuration (Depth = 0, Detune = 0): outputs match after the fade
    const size_t from = static_cast<size_t>(total) - static_cast<size_t>(sr * 0.5);
    double d = 0.0;
    for (size_t i = from; i < oL.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(oL[i] - fL[i])));
    CM_EXPECT(d < 1.0e-3);
}

// Fraction of the signal power that is NOT the pure tone at `hz` (least-squares sine/cosine fit removed).
static double offCarrierFraction(const std::vector<float>& x, size_t a, double sr, double hz) {
    const double w = 2.0 * 3.14159265358979323846 * hz / sr;
    double sc = 0, ss = 0, cc = 0, ssn = 0, xx = 0;
    size_t n = 0;
    for (size_t i = a; i < x.size(); ++i, ++n) {
        const double c = std::cos(w * static_cast<double>(i)), s = std::sin(w * static_cast<double>(i));
        sc += x[i] * c; ss += x[i] * s; cc += c * c; ssn += s * s; xx += static_cast<double>(x[i]) * x[i];
    }
    const double fit = sc * sc / cc + ss * ss / ssn;   // c and s are orthogonal over whole periods
    return xx > 0.0 ? std::max(0.0, (xx - fit) / xx) : 0.0;
}

CM_TEST(multi_detune_spreads_the_tone_in_proportion_to_the_setting) {
    // Delay-ramp detune: a pure tone stays a pure tone with Detune = 0 and is smeared into shifted components with Detune > 0,
    // more so for larger settings. (The ramp resets make the long-term mean pitch unchanged: this is a chorus-type detune.)
    const double sr = 48000.0, f0 = 480.0;
    const int total = static_cast<int>(sr * 3.0);
    const auto s = fxtest::sine(total, sr, f0, 0.5f);
    const size_t a = static_cast<size_t>(sr * 0.5);
    double frac[3] = {};
    int k = 0;
    for (float cents : { 0.0f, 10.0f, 50.0f }) {
        CrowdMultiplier m;
        m.prepare(sr, 512);
        auto p = multiParams(2);
        p[MP::Depth] = 0.0f; p[MP::Scatter] = 0.0f; p[MP::Tone] = 0.0f; p[MP::Detune] = cents;
        std::vector<float> oL, oR;
        runStereo(m, s, s, oL, oR, 512, p.data(), FxContext {});
        // two static delayed copies of a sine are a sine again; no change of frequency or amplitude modulation
        frac[k++] = offCarrierFraction(oL, a, sr, f0);
    }
    CM_EXPECT(frac[0] < 1.0e-4);
    CM_EXPECT(frac[1] > 1.0e-6);
    CM_EXPECT(frac[2] > 10.0 * frac[1]);
}

CM_TEST(multi_static_voices_are_exact_delayed_copies) {
    // Depth = Detune = Scatter = Tone = 0: each voice is the input delayed by its base time; the sum has 1/sqrt(N) scale.
    const double sr = 48000.0;
    CrowdMultiplier m;
    m.prepare(sr, 512);
    auto p = multiParams(4);
    p[MP::Depth] = 0.0f; p[MP::Detune] = 0.0f; p[MP::Scatter] = 0.0f; p[MP::Tone] = 0.0f;
    std::vector<float> in(static_cast<size_t>(sr * 0.5), 0.0f), oL, oR;
    in[static_cast<size_t>(sr * 0.2)] = 1.0f;
    runStereo(m, in, in, oL, oR, 512, p.data(), FxContext {});
    const int seed = static_cast<int>(MP::defs[MP::Seed].def);
    double total = 0.0;
    for (int v = 0; v < 4; ++v) {
        const double dSamples = CrowdMultiplier::traits(seed, v).baseMs * 0.001 * sr;
        // readLinear(d) puts the impulse on samples floor(d) - 1 and floor(d) after the input (interpolation weights 1-f, f)
        const size_t idx = static_cast<size_t>(sr * 0.2) + static_cast<size_t>(std::floor(dSamples));
        total += std::fabs(oL[idx - 1]) + std::fabs(oL[idx]);
    }
    CM_EXPECT_NEAR(total, 4.0 * 0.5, 0.05);   // 4 voices x gain 1 x 1/sqrt(4); the 30 ms fade-in has long finished by 0.2 s
}

CM_TEST(multi_is_mono_compatible) {
    const double sr = 48000.0;
    const auto in = noise(static_cast<int>(sr * 3.0), 123u, 0.5f);
    for (int voices : { 2, 6, 16 })
        for (float scatter : { 0.0f, 0.7f, 1.0f }) {
            CrowdMultiplier m;
            m.prepare(sr, 512);
            auto p = multiParams(voices);
            p[MP::Scatter] = scatter;
            std::vector<float> oL, oR;
            runStereo(m, in, in, oL, oR, 512, p.data(), FxContext {});
            std::vector<float> mono(oL.size());
            for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (oL[i] + oR[i]);
            const size_t a = static_cast<size_t>(sr);
            const double pm = rms(mono, a, mono.size());
            const double pl = 0.5 * (rms(oL, a, oL.size()) + rms(oR, a, oR.size()));
            CM_EXPECT(pm > 0.55 * pl && pm < 1.05 * pl);   // no cancellation: within -5 dB of the channel level
        }
}

CM_TEST(multi_bypass_clears_and_restarts_deterministically) {
    const double sr = 48000.0;
    CrowdMultiplier m;
    m.prepare(sr, 256);
    auto p = multiParams(8);
    const auto in = noise(static_cast<int>(sr * 1.0), 9u, 0.5f);
    std::vector<float> a, aR;
    runStereo(m, in, in, a, aR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(a) > 0.05f);
    p[MP::On] = 0.0f;
    const std::vector<float> zeros(static_cast<size_t>(sr * 0.5), 0.0f);
    std::vector<float> off, offR;
    runStereo(m, zeros, zeros, off, offR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(off, static_cast<size_t>(sr * 0.02)) == 0.0f);
    p[MP::On] = 1.0f;
    std::vector<float> silent, silentR;
    runStereo(m, zeros, zeros, silent, silentR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxAbs(silent) == 0.0f);
    std::vector<float> second, secondR, fresh, freshR;
    runStereo(m, in, in, second, secondR, 256, p.data(), FxContext {});
    CrowdMultiplier f;   // same off period first (a brand new instance starts On without the 10 ms fade-in)
    f.prepare(sr, 256);
    auto pOff = p; pOff[MP::On] = 0.0f;
    std::vector<float> d1, d2;
    runStereo(f, zeros, zeros, d1, d2, 256, pOff.data(), FxContext {});
    runStereo(f, zeros, zeros, d1, d2, 256, p.data(), FxContext {});   // and the same silent On period
    runStereo(f, in, in, fresh, freshR, 256, p.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(second, fresh) < 1.0e-5f);
}

CM_TEST(multi_non_finite_and_extreme_values_are_safe) {
    CrowdMultiplier m;
    m.prepare(48000.0, 512);
    std::vector<float> bad(MP::Count, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> in(4800, std::numeric_limits<float>::infinity()), oL(4800), oR(4800);
    m.process(in.data(), in.data(), oL.data(), oR.data(), 4800, bad.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
    m.process(nullptr, nullptr, oL.data(), oR.data(), 4800, nullptr, FxContext {});
    CM_EXPECT(fxtest::allFinite(oL));
    m.process(in.data(), in.data(), nullptr, nullptr, 100, bad.data(), FxContext {});
    std::vector<float> huge(MP::Count, 1.0e30f);
    m.process(in.data(), in.data(), oL.data(), oR.data(), 4800, huge.data(), FxContext {});
    CM_EXPECT(fxtest::allFinite(oL) && fxtest::allFinite(oR));
}

CM_TEST(multi_depth_and_rate_modulate_the_voices) {
    const double sr = 48000.0;
    const auto in = noise(static_cast<int>(sr * 2.0), 5u, 0.5f);
    CrowdMultiplier a, b;
    a.prepare(sr, 512); b.prepare(sr, 512);
    auto pa = multiParams(8), pb = multiParams(8);
    pa[MP::Depth] = 0.0f; pa[MP::Detune] = 0.0f;
    pb[MP::Depth] = 12.0f; pb[MP::Detune] = 0.0f; pb[MP::Rate] = 3.0f;
    std::vector<float> aL, aR, bL, bR;
    runStereo(a, in, in, aL, aR, 512, pa.data(), FxContext {});
    runStereo(b, in, in, bL, bR, 512, pb.data(), FxContext {});
    CM_EXPECT(fxtest::maxDiff(aL, bL) > 0.1f);
    CM_EXPECT(fxtest::allFinite(bL));
}
