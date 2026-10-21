#include "CrowdMultiplier.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr int kTick = 16;
constexpr float kMinDelaySamples = 2.0f;
constexpr float kBaseSlew = 0.02f;            // max change of a voice's base delay: 2 % (= 34 cents) of time
constexpr float kToneCutoffHz = 1200.0f;
constexpr float kMaxTilt = 0.5f;
constexpr float kGainJitter = 0.3f;

inline float frac(float x) noexcept { return x - std::floor(x); }
inline float u01(fx::FxRng& r) noexcept { return 0.5f * (r.bipolar() + 1.0f); }
}

CrowdMultiplier::VoiceTraits CrowdMultiplier::traits(int seed, int voice) noexcept {
    const uint32_t sd = static_cast<uint32_t>(std::max(0, seed));
    const uint32_t v = static_cast<uint32_t>(std::clamp(voice, 0, kMaxVoices - 1));
    fx::FxRng r;
    r.seed(fx::fxHashSeed(sd, v + 1u));
    // Seed-wide offsets for the low-discrepancy sequences (identical for every voice of that seed).
    const float off1 = static_cast<float>(fx::fxHashSeed(sd, 9001u) >> 8) * (1.0f / 16777216.0f);
    const float off2 = static_cast<float>(fx::fxHashSeed(sd, 9002u) >> 8) * (1.0f / 16777216.0f);
    VoiceTraits t;
    const float baseU = frac(off1 + static_cast<float>(v) * 0.6180339887f + 0.05f * r.bipolar());
    t.baseMs = kMinBaseMs + kBaseSpanMs * baseU;
    const float panU = 2.0f * frac(off2 + static_cast<float>(v) * 0.7548776662f) - 1.0f;
    t.pan = clampf(0.85f * panU + 0.15f * r.bipolar(), -1.0f, 1.0f);
    t.gainJitter = r.bipolar();
    t.tone = r.bipolar();
    t.detune = ((v & 1u) != 0u ? 1.0f : -1.0f) * (0.4f + 0.6f * u01(r));
    t.stereoOffset = r.bipolar();
    t.rateRatio = 0.7f + 0.6f * u01(r);
    return t;
}

void CrowdMultiplier::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float maxMs = kMinBaseMs + kBaseSpanMs + 2.0f * kMaxDepthMs + kMaxStereoOffsetMs + 4.0f;
    bufL.allocate(static_cast<int>(std::ceil(maxMs * 0.001 * sr)) + 16);
    bufR.allocate(static_cast<int>(std::ceil(maxMs * 0.001 * sr)) + 16);
    maxDelay = static_cast<float>(bufL.capacity() - 4);
    windowSamples = std::max(8.0f, kRampWindowMs * 0.001f * static_cast<float>(sr));
    lpK = 1.0f - std::exp(-kTwoPi * kToneCutoffHz / static_cast<float>(sr));
    depthSm.prepare(sr, 50.0f); rateSm.prepare(sr, 50.0f); detuneSm.prepare(sr, 50.0f); scatterSm.prepare(sr, 50.0f);
    toneSm.prepare(sr, 50.0f); levelSm.prepare(sr, 20.0f); normSm.prepare(sr, 30.0f); bypassSm.prepare(sr, 10.0f);
    for (auto& v : voices) v.act.prepare(sr, 30.0f);
    reset();
}

void CrowdMultiplier::clearState() noexcept {
    bufL.clear(); bufR.clear();
    for (auto& v : voices) { v.dormant = true; v.act.snap(0.0f); v.lpL = v.lpR = 0.0f; }
    ctrlCounter = 0;
}

void CrowdMultiplier::reset() noexcept {
    clearState();
    appliedSeed = -1;
    fresh = true;
    cleared = true;
}

void CrowdMultiplier::initVoice(int idx, int seed) noexcept {
    auto& v = voices[idx];
    v.t = traits(seed, idx);
    v.rng.seed(fx::fxHashSeed(static_cast<uint32_t>(std::max(0, seed)), 100u + static_cast<uint32_t>(idx)));
    v.lfoA = v.rng.bipolar();
    v.lfoB = v.rng.bipolar();
    v.lfoPhase = u01(v.rng);
    v.ramp = u01(v.rng);
    v.base = v.t.baseMs * 0.001f * static_cast<float>(sr);
    v.slow = v.base; v.slowStep = 0.0f;
    v.offR = 0.0f; v.offRStep = 0.0f;
    v.dmax = 0.0f; v.up = false; v.tilt = 0.0f;
    v.lpL = v.lpR = 0.0f;
    v.gL = v.gR = 0.0f; v.gLStep = v.gRStep = 0.0f;
    v.snap = true;
}

void CrowdMultiplier::tick() noexcept {
    const float s = static_cast<float>(sr);
    const float depthS = clampf(depthSm.value(), 0.0f, kMaxDepthMs) * 0.001f * s;
    const float rate = clampf(rateSm.value(), 0.05f, 8.0f);
    const float cents = clampf(detuneSm.value(), 0.0f, kMaxDetuneCents);
    const float sc = clampf(scatterSm.value(), 0.0f, 1.0f);
    const float tn = clampf(toneSm.value(), 0.0f, 1.0f);
    const float invTick = 1.0f / static_cast<float>(kTick);
    for (auto& v : voices) {
        if (v.dormant) continue;
        // smoothed random LFO
        v.lfoPhase += rate * v.t.rateRatio * static_cast<float>(kTick) / s;
        int guard = 0;
        while (v.lfoPhase >= 1.0f && guard++ < 8) { v.lfoPhase -= 1.0f; v.lfoA = v.lfoB; v.lfoB = v.rng.bipolar(); }
        if (v.lfoPhase >= 1.0f) v.lfoPhase = 0.0f;
        const float sm = v.lfoPhase * v.lfoPhase * (3.0f - 2.0f * v.lfoPhase);
        const float m = v.lfoA + (v.lfoB - v.lfoA) * sm;
        // base delay slews toward its target (a seed change glides)
        const float baseT = v.t.baseMs * 0.001f * s;
        const float maxMove = kBaseSlew * static_cast<float>(kTick);
        v.base += clampf(baseT - v.base, -maxMove, maxMove);
        const float slowT = clampf(v.base + depthS * (1.0f + m), kMinDelaySamples, maxDelay - 8.0f);
        const float offT = v.t.stereoOffset * kMaxStereoOffsetMs * 0.001f * s * sc;
        // detune ramp amplitude
        const float c = cents * std::fabs(v.t.detune);
        v.up = v.t.detune > 0.0f;
        const float r = v.up ? std::exp2(c / 1200.0f) - 1.0f : 1.0f - std::exp2(-c / 1200.0f);
        v.dmax = r * windowSamples;
        // pan / jitter / tone
        const float p = clampf(v.t.pan * sc, -1.0f, 1.0f);
        const float gl0 = std::min(1.0f, 1.0f - p), gr0 = std::min(1.0f, 1.0f + p);
        const float cp = 1.0f / std::sqrt(0.5f * (gl0 * gl0 + gr0 * gr0));
        const float jit = 1.0f + kGainJitter * sc * v.t.gainJitter;
        v.tilt = clampf(kMaxTilt * tn * v.t.tone, -kMaxTilt, kMaxTilt);
        const float tiltNorm = 1.0f / std::sqrt(1.0f + v.tilt * v.tilt);
        const float gLt = gl0 * cp * jit * tiltNorm, gRt = gr0 * cp * jit * tiltNorm;
        if (v.snap) {
            v.slow = slowT; v.slowStep = 0.0f;
            v.offR = offT; v.offRStep = 0.0f;
            v.gL = gLt; v.gR = gRt; v.gLStep = v.gRStep = 0.0f;
            v.snap = false;
        } else {
            v.slowStep = (slowT - v.slow) * invTick;
            v.offRStep = (offT - v.offR) * invTick;
            v.gLStep = (gLt - v.gL) * invTick;
            v.gRStep = (gRt - v.gR) * invTick;
        }
        if (v.act.targetValue() == 0.0f && !v.act.isSmoothing() && v.act.value() == 0.0f) v.dormant = true;
    }
}

void CrowdMultiplier::process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                              const FxContext& ctx) noexcept {
    namespace P = params::MultiP;
    (void) ctx;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, P::defs[i].def, lo, hi); };

    const bool on = prm(P::On, 0.0f, 1.0f) > 0.5f;
    const int nv = static_cast<int>(std::lround(prm(P::Voices, static_cast<float>(kMinVoices), static_cast<float>(kMaxVoices))));
    const float depth = prm(P::Depth, 0.0f, kMaxDepthMs);
    const float rate = prm(P::Rate, 0.05f, 8.0f);
    const float detune = prm(P::Detune, 0.0f, kMaxDetuneCents);
    const float scat = prm(P::Scatter, 0.0f, 1.0f);
    const float toneV = prm(P::Tone, 0.0f, 1.0f);
    const int seed = static_cast<int>(std::lround(prm(P::Seed, 1.0f, 9999.0f)));
    const float level = dbToGain(prm(P::Level, -60.0f, 12.0f));
    const float norm = normalisation(nv);

    if (fresh) {
        depthSm.snap(depth); rateSm.snap(rate); detuneSm.snap(detune); scatterSm.snap(scat); toneSm.snap(toneV);
        levelSm.snap(level); normSm.snap(norm); bypassSm.snap(on ? 1.0f : 0.0f);
        fresh = false;
    } else {
        depthSm.setTarget(depth); rateSm.setTarget(rate); detuneSm.setTarget(detune); scatterSm.setTarget(scat);
        toneSm.setTarget(toneV); levelSm.setTarget(level); normSm.setTarget(norm); bypassSm.setTarget(on ? 1.0f : 0.0f);
    }

    if (!on && !bypassSm.isSmoothing() && bypassSm.value() == 0.0f) {
        if (!cleared) { clearState(); appliedSeed = -1; cleared = true; }
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    cleared = false;

    // Seed change: new traits, the LFO position and detune windows continue (delays and gains glide).
    if (seed != appliedSeed) {
        for (int v = 0; v < kMaxVoices; ++v) {
            auto& s = voices[v];
            if (appliedSeed >= 0 && !s.dormant) {
                s.t = traits(seed, v);
                s.rng.seed(fx::fxHashSeed(static_cast<uint32_t>(seed), 100u + static_cast<uint32_t>(v)));
            }
        }
        appliedSeed = seed;
    }
    // Voice count: wake voices that became active (fully re-initialised => history independent), fade the others out.
    for (int v = 0; v < kMaxVoices; ++v) {
        auto& s = voices[v];
        if (v < nv) {
            if (s.dormant) { initVoice(v, seed); s.dormant = false; s.act.snap(0.0f); }
            s.act.setTarget(1.0f);
        } else if (!s.dormant) {
            s.act.setTarget(0.0f);
        }
    }

    const float rampInc = 1.0f / windowSamples;
    for (int i = 0; i < n; ++i) {
        depthSm.next(); rateSm.next(); detuneSm.next(); scatterSm.next(); toneSm.next();
        const float lvl = levelSm.next();
        const float nrm = normSm.next();
        const float bg = bypassSm.next();
        if (ctrlCounter == 0) tick();
        ctrlCounter = (ctrlCounter + 1) & (kTick - 1);

        bufL.push(clampf(sanitize(inL != nullptr ? inL[i] : 0.0f), -kInputClamp, kInputClamp) * bg);
        bufR.push(clampf(sanitize(inR != nullptr ? inR[i] : 0.0f), -kInputClamp, kInputClamp) * bg);

        float accL = 0.0f, accR = 0.0f;
        for (auto& v : voices) {
            if (v.dormant) continue;
            const float a = v.act.next();
            v.slow += v.slowStep; v.offR += v.offRStep; v.gL += v.gLStep; v.gR += v.gRStep;
            const float phi = v.ramp;
            v.ramp += rampInc;
            if (v.ramp >= 1.0f) v.ramp -= 1.0f;
            const float wA = 1.0f - std::fabs(2.0f * phi - 1.0f), wB = 1.0f - wA;
            const float psiB = phi >= 0.5f ? phi - 0.5f : phi + 0.5f;
            const float offA = v.up ? v.dmax * (1.0f - phi) : v.dmax * phi;
            const float offB = v.up ? v.dmax * (1.0f - psiB) : v.dmax * psiB;
            const float dLA = clampf(v.slow + offA, kMinDelaySamples, maxDelay);
            const float dLB = clampf(v.slow + offB, kMinDelaySamples, maxDelay);
            const float dRA = clampf(v.slow + v.offR + offA, kMinDelaySamples, maxDelay);
            const float dRB = clampf(v.slow + v.offR + offB, kMinDelaySamples, maxDelay);
            float yL = wA * bufL.readLinear(dLA) + wB * bufL.readLinear(dLB);
            float yR = wA * bufR.readLinear(dRA) + wB * bufR.readLinear(dRB);
            v.lpL = fx::fxFlush(v.lpL + lpK * (yL - v.lpL));
            v.lpR = fx::fxFlush(v.lpR + lpK * (yR - v.lpR));
            yL += v.tilt * (2.0f * v.lpL - yL);
            yR += v.tilt * (2.0f * v.lpR - yR);
            accL += a * v.gL * yL;
            accR += a * v.gR * yR;
        }
        const float og = nrm * lvl * bg;
        outL[i] = accL * og;
        outR[i] = accR * og;
    }
}
}
