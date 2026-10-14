#include "NoiseEnhancer.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr float kSqrt3 = 1.7320508f;
constexpr float kPinkNorm = 0.30f;     // Kellet economy pink of unit-RMS white -> ~unit RMS
constexpr float kBrightNorm = 0.743f;  // 1 / sqrt(1 + 0.9^2)
}

void NoiseEnhancer::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    brownCoef = std::exp(-kTwoPi * 40.0f / static_cast<float>(sr));
    brownNorm = std::sqrt(1.0f - brownCoef * brownCoef);
    fastAtk = msToCoef(1.0f, sr); fastRel = msToCoef(30.0f, sr);
    slowAtk = msToCoef(80.0f, sr); slowRel = msToCoef(300.0f, sr);
    tiltL.setCutoff(sr, 1000.0f); tiltR.setCutoff(sr, 1000.0f);
    for (Smoother* s : { &amount, &follow, &transFollow, &width, &tiltLo, &tiltHi }) s->prepare(sr, 20.0f);
    for (auto& g : colourGain) g.prepare(sr, 20.0f);
    bypassGain.prepare(sr, 10.0f);
    hpHz.prepare(sr, 50.0f); lpHz.prepare(sr, 50.0f);
    reset();
}

void NoiseEnhancer::reseed(uint32_t s) noexcept {
    currentSeed = s;
    rngC.seed(fx::fxHashSeed(s, 1u));
    rngL.seed(fx::fxHashSeed(s, 2u));
    rngR.seed(fx::fxHashSeed(s, 3u));
}

void NoiseEnhancer::clearState() noexcept {
    colL.reset(); colR.reset();
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    tiltL.reset(); tiltR.reset();
    env = fastEnv = slowEnv = 0.0f;
    reseed(currentSeed);
}

void NoiseEnhancer::reset() noexcept {
    currentSeed = 1u;
    clearState();
    appliedHp = appliedLp = -1.0f;
    ctrlCounter = 0;
    fresh = true; cleared = true;
}

void NoiseEnhancer::process(const float* sendL, const float* sendR, float* outL, float* outR, int n, const float* p,
                            const FxContext& ctx) noexcept {
    namespace P = params::NoiseP;
    (void) ctx;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, params::NoiseP::defs[i].def, lo, hi); };

    const bool bypass = prm(P::Bypass, 0.0f, 1.0f) > 0.5f;
    const float vAmount = prm(P::Amount, 0.0f, 1.0f);
    const int colour = static_cast<int>(std::lround(prm(P::Color, 0.0f, 3.0f)));
    const float tilt = prm(P::Tilt, -1.0f, 1.0f);
    const float vHp = prm(P::Hpf, 10.0f, 20000.0f), vLp = prm(P::Lpf, 200.0f, 24000.0f);
    const float vFollow = prm(P::Follow, 0.0f, 1.0f), vTrans = prm(P::TransFollow, 0.0f, 1.0f);
    const float atk = msToCoef(prm(P::Attack, 0.1f, 5000.0f), sr);
    const float rel = msToCoef(prm(P::Release, 1.0f, 10000.0f), sr);
    const float vWidth = prm(P::Width, 0.0f, 1.0f);
    const uint32_t seed = static_cast<uint32_t>(std::lround(prm(P::Seed, 1.0f, 9999.0f)));
    const float gLo = dbToGain(-6.0f * tilt), gHi = dbToGain(6.0f * tilt);

    if (fresh || seed != currentSeed) reseed(seed);
    if (fresh) {
        amount.snap(vAmount); follow.snap(vFollow); transFollow.snap(vTrans); width.snap(vWidth);
        tiltLo.snap(gLo); tiltHi.snap(gHi); bypassGain.snap(bypass ? 0.0f : 1.0f);
        hpHz.snap(vHp); lpHz.snap(vLp);
        for (int c = 0; c < 4; ++c) colourGain[c].snap(c == colour ? 1.0f : 0.0f);
        fresh = false;
    } else {
        amount.setTarget(vAmount); follow.setTarget(vFollow); transFollow.setTarget(vTrans); width.setTarget(vWidth);
        tiltLo.setTarget(gLo); tiltHi.setTarget(gHi); bypassGain.setTarget(bypass ? 0.0f : 1.0f);
        hpHz.setTarget(vHp); lpHz.setTarget(vLp);
        for (int c = 0; c < 4; ++c) colourGain[c].setTarget(c == colour ? 1.0f : 0.0f);
    }

    if (bypass && !bypassGain.isSmoothing() && bypassGain.value() == 0.0f) {
        if (!cleared) { clearState(); cleared = true; }
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    cleared = false;

    auto colourise = [this](Colour& s, float w, const float* cg) {
        // w: unit-RMS white
        s.b0 = 0.99765f * s.b0 + w * 0.0990460f;
        s.b1 = 0.96300f * s.b1 + w * 0.2965164f;
        s.b2 = 0.57000f * s.b2 + w * 1.0526913f;
        const float pink = (s.b0 + s.b1 + s.b2 + w * 0.1848f) * kPinkNorm;
        s.brown = fx::fxFlush(brownCoef * s.brown + brownNorm * w);
        const float bright = (w - 0.9f * s.prev) * kBrightNorm;
        s.prev = w;
        s.b0 = fx::fxFlush(s.b0); s.b1 = fx::fxFlush(s.b1); s.b2 = fx::fxFlush(s.b2);
        return cg[0] * w + cg[1] * pink + cg[2] * s.brown + cg[3] * bright;
    };

    for (int i = 0; i < n; ++i) {
        const float h = hpHz.next(), l = lpHz.next();
        if (ctrlCounter == 0) {
            if (h != appliedHp) { const auto k = fx::fxDesignHighpass(sr, h); hpL.setCoeffs(k); hpR.setCoeffs(k); appliedHp = h; }
            if (l != appliedLp) { const auto k = fx::fxDesignLowpass(sr, l); lpL.setCoeffs(k); lpR.setCoeffs(k); appliedLp = l; }
        }
        ctrlCounter = (ctrlCounter + 1) & 15;

        // Envelope / transient followers on the send.
        const float x = std::max(std::fabs(sanitize(sendL != nullptr ? sendL[i] : 0.0f)),
                                 std::fabs(sanitize(sendR != nullptr ? sendR[i] : 0.0f)));
        env = fx::fxFlush(x + (x > env ? atk : rel) * (env - x));
        fastEnv = fx::fxFlush(x + (x > fastEnv ? fastAtk : fastRel) * (fastEnv - x));
        slowEnv = fx::fxFlush(x + (x > slowEnv ? slowAtk : slowRel) * (slowEnv - x));
        const float trans = clampf(0.5f * (fastEnv - slowEnv) / (slowEnv + 0.01f), 0.0f, 1.0f);

        // Decorrelated streams.
        const float w = width.next();
        const float wa = std::cos(0.5f * kPi * w), wb = std::sin(0.5f * kPi * w);
        const float c = rngC.bipolar();
        const float nL = (wa * c + wb * rngL.bipolar()) * kSqrt3;
        const float nR = (wa * c + wb * rngR.bipolar()) * kSqrt3;

        const float cg[4] = { colourGain[0].next(), colourGain[1].next(), colourGain[2].next(), colourGain[3].next() };
        float yL = colourise(colL, nL, cg), yR = colourise(colR, nR, cg);
        yL = lpL.process(hpL.process(yL));
        yR = lpR.process(hpR.process(yR));
        const float tl = tiltLo.next(), th = tiltHi.next();
        const float loL = tiltL.lowpass(yL), loR = tiltR.lowpass(yR);
        yL = loL * tl + (yL - loL) * th;
        yR = loR * tl + (yR - loR) * th;

        const float fo = follow.next(), tf = transFollow.next();
        const float level = amount.next() * kBaseLevel
                            * ((1.0f - fo) + fo * clampf(env * 4.0f, 0.0f, 1.0f) + tf * trans);
        const float bg = bypassGain.next();
        const float oL = kCeiling * std::tanh(yL * level / kCeiling);
        const float oR = kCeiling * std::tanh(yR * level / kCeiling);
        outL[i] = clampf(oL, -kCeiling, kCeiling) * bg;
        outR[i] = clampf(oR, -kCeiling, kCeiling) * bg;
    }
}
}
