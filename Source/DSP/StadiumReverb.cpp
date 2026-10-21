#include "StadiumReverb.h"
#include "Core/ParamTables.h"
#include "DigitalDelay.h"

namespace crowdmike::dsp {
namespace {
constexpr float kBaseMs[StadiumReverb::kLines] = { 37.3f, 48.1f, 57.9f, 69.7f, 83.9f, 97.3f, 113.1f, 131.9f };
constexpr float kLfoRatio[StadiumReverb::kLines] = { 1.00f, 0.83f, 1.17f, 0.71f, 1.29f, 0.93f, 1.09f, 0.77f };
// Input diffuser lengths in samples at 48 kHz (mutually prime, L/R differ so the two channels decorrelate).
constexpr int kApSamples48L[StadiumReverb::kDiffusers] = { 142, 107, 379, 277 };
constexpr int kApSamples48R[StadiumReverb::kDiffusers] = { 151, 113, 409, 307 };
// Output tap signs: orthogonal Hadamard rows (dot product 0 => decorrelated L/R).
constexpr float kSignL[StadiumReverb::kLines] = { 1, 1, -1, -1, 1, 1, -1, -1 };
constexpr float kSignR[StadiumReverb::kLines] = { 1, -1, 1, -1, -1, 1, -1, 1 };
constexpr float kInvSqrt8 = 0.35355339059327373f;
constexpr float kMaxModMs = 1.2f;
constexpr int kTick = 16;
constexpr float kLn1000 = 6.907755278982137f;   // ln(10^3): 60 dB = factor 10^-3 in amplitude

// 4-point cubic Lagrange read at fractional delay d >= 2 (gain <= 1 at every fraction, far flatter in the highs than linear
// interpolation, so the modulated lines do not low-pass the tail on every pass).
inline float readCubic(const fx::FxDelayBuffer& b, float d) noexcept {
    const int i = static_cast<int>(d);
    const float f = d - static_cast<float>(i);
    const float p0 = b.read(i - 1), p1 = b.read(i), p2 = b.read(i + 1), p3 = b.read(i + 2);
    const float c0 = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
    const float c1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
    const float c2 = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
    const float c3 = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);
    return c0 * p0 + c1 * p1 + c2 * p2 + c3 * p3;
}

inline void hadamard8(float* v) noexcept {
    for (int len = 1; len < StadiumReverb::kLines; len <<= 1)
        for (int i = 0; i < StadiumReverb::kLines; i += len << 1)
            for (int j = i; j < i + len; ++j) {
                const float a = v[j], b = v[j + len];
                v[j] = a + b;
                v[j + len] = a - b;
            }
}
}

float StadiumReverb::lineMs(int line, float size) noexcept {
    const int i = std::clamp(line, 0, kLines - 1);
    const float s = clampf(std::isfinite(size) ? size : 0.5f, 0.0f, 1.0f);
    return kBaseMs[i] * 0.15f * std::pow(1.0f / 0.15f, s);
}

void StadiumReverb::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int lineMax = static_cast<int>(std::ceil((kBaseMs[kLines - 1] + kMaxModMs + 4.0f) * 0.001 * sr)) + 8;
    for (auto& l : lines) l.buf.allocate(lineMax);
    const int preMax = static_cast<int>(std::ceil((kMaxPreDelayMs + 4.0f) * 0.001 * sr)) + 8;
    preL.allocate(preMax);
    preR.allocate(preMax);
    for (int i = 0; i < kDiffusers; ++i) {
        apL[i].allocate(std::max(1, static_cast<int>(std::lround(kApSamples48L[i] * sr / 48000.0))));
        apR[i].allocate(std::max(1, static_cast<int>(std::lround(kApSamples48R[i] * sr / 48000.0))));
    }
    sizeSm.prepare(sr, 400.0f); decaySm.prepare(sr, 60.0f); preSm.prepare(sr, 80.0f); hfSm.prepare(sr, 50.0f);
    lowCutSm.prepare(sr, 50.0f); diffSm.prepare(sr, 50.0f); modDepthSm.prepare(sr, 50.0f); modRateSm.prepare(sr, 50.0f);
    widthSm.prepare(sr, 30.0f); wetSm.prepare(sr, 20.0f); freezeSm.prepare(sr, 50.0f); killSm.prepare(sr, 40.0f);
    bypassSm.prepare(sr, 10.0f);
    reset();
}

void StadiumReverb::clearState() noexcept {
    for (int i = 0; i < kLines; ++i) {
        auto& l = lines[i];
        l.buf.clear();
        l.lp = 0.0f;
        l.phase = static_cast<float>(i) / static_cast<float>(kLines);
        l.len = 0.0f;
        l.lenStep = 0.0f;
        l.gain = 0.0f;
    }
    preL.clear(); preR.clear();
    for (int i = 0; i < kDiffusers; ++i) { apL[i].reset(); apR[i].reset(); }
    lowCutL.reset(); lowCutR.reset();
    appliedLowCut = -1.0f;
    ctrlCounter = 0;
    lensDirty = true;
}

void StadiumReverb::reset() noexcept {
    clearState();
    fresh = true;
    cleared = true;
}

void StadiumReverb::controlTick() noexcept {
    const float size = sizeSm.value();
    const float decay = clampf(decaySm.value(), kMinDecaySec, kMaxDecaySec);
    const float fz = freezeSm.value();
    const float kl = killSm.value();
    const float cap = static_cast<float>(lines[0].buf.capacity() - 4);
    float meanG2 = 0.0f;
    for (int i = 0; i < kLines; ++i) {
        auto& l = lines[i];
        const float nominal = lineMs(i, size) * 0.001f * static_cast<float>(sr);
        const float mod = modDepthSamples * std::sin(kTwoPi * l.phase);
        const float target = clampf(nominal + mod, 4.0f, cap);
        if (lensDirty) { l.len = target; l.lenStep = 0.0f; }
        else l.lenStep = (target - l.len) / static_cast<float>(kTick);
        l.phase += modRateInc * kLfoRatio[i] * static_cast<float>(kTick);
        l.phase -= std::floor(l.phase);
        const float g = std::exp(-kLn1000 * nominal / (decay * static_cast<float>(sr)));
        meanG2 += g * g;
        l.gain = (g + fz * (kFreezeLoopGain - g)) * kl;
    }
    lensDirty = false;
    meanG2 /= static_cast<float>(kLines);
    outNorm = std::sqrt(std::min(4.0f, 8.0f * (1.0f - meanG2)));
    const float hf = clampf(hfSm.value(), 200.0f, 20000.0f);
    // HF Damping at the top of its range (>= 20 kHz) is fully transparent; the one-pole fades in over 16..20 kHz.
    dampA = std::exp(-kTwoPi * hf / static_cast<float>(sr)) * clampf((20000.0f - hf) * (1.0f / 4000.0f), 0.0f, 1.0f) * (1.0f - fz);
    diffG = 0.72f * clampf(diffSm.value(), 0.0f, 1.0f);
    modDepthSamples = clampf(modDepthSm.value(), 0.0f, 1.0f) * kMaxModMs * 0.001f * static_cast<float>(sr);
    modRateInc = clampf(modRateSm.value(), 0.05f, 5.0f) / static_cast<float>(sr);
    const float lc = lowCutSm.value();
    if (lc != appliedLowCut) {
        const auto k = fx::fxDesignHighpass(sr, lc);
        lowCutL.setCoeffs(k); lowCutR.setCoeffs(k);
        appliedLowCut = lc;
    }
}

void StadiumReverb::process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                            const FxContext& ctx) noexcept {
    namespace P = params::ReverbP;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, P::defs[i].def, lo, hi); };

    const bool on = prm(P::On, 0.0f, 1.0f) > 0.5f;
    const float size = prm(P::Size, 0.0f, 1.0f);
    const float decay = prm(P::Decay, kMinDecaySec, kMaxDecaySec);
    float preMs = prm(P::PreDelay, 0.0f, kMaxPreDelayMs);
    if (prm(P::PreSync, 0.0f, 1.0f) > 0.5f && ctx.hasTempo && std::isfinite(ctx.bpm) && ctx.bpm > 1.0) {
        const int div = static_cast<int>(std::lround(prm(P::PreDiv, 0.0f, 12.0f)));
        preMs = static_cast<float>(std::min(static_cast<double>(kMaxPreDelayMs), DigitalDelay::divisionBeats(div) * 60000.0 / ctx.bpm));
    }
    const float hf = prm(P::HfDamp, 1000.0f, 20000.0f);
    const float lowCut = prm(P::LowCut, 20.0f, 1000.0f);
    const float diff = prm(P::Diffusion, 0.0f, 1.0f);
    const float modDepth = prm(P::ModDepth, 0.0f, 1.0f);
    const float modRate = prm(P::ModRate, 0.05f, 5.0f);
    const float width = prm(P::Width, 0.0f, 1.0f);
    const float wet = dbToGain(prm(P::Wet, -60.0f, 12.0f));
    const bool freeze = prm(P::Freeze, 0.0f, 1.0f) > 0.5f || ctx.freeze;
    const bool kill = prm(P::TailKill, 0.0f, 1.0f) > 0.5f;

    if (fresh) {
        sizeSm.snap(size); decaySm.snap(decay); preSm.snap(preMs); hfSm.snap(hf); lowCutSm.snap(lowCut); diffSm.snap(diff);
        modDepthSm.snap(modDepth); modRateSm.snap(modRate); widthSm.snap(width); wetSm.snap(wet);
        freezeSm.snap(freeze ? 1.0f : 0.0f); killSm.snap(kill ? 0.0f : 1.0f); bypassSm.snap(on ? 1.0f : 0.0f);
        modDepthSamples = modDepthSm.value() * kMaxModMs * 0.001f * static_cast<float>(sr);
        modRateInc = modRateSm.value() / static_cast<float>(sr);
        fresh = false;
    } else {
        sizeSm.setTarget(size); decaySm.setTarget(decay); preSm.setTarget(preMs); hfSm.setTarget(hf); lowCutSm.setTarget(lowCut);
        diffSm.setTarget(diff); modDepthSm.setTarget(modDepth); modRateSm.setTarget(modRate); widthSm.setTarget(width);
        wetSm.setTarget(wet); freezeSm.setTarget(freeze ? 1.0f : 0.0f); killSm.setTarget(kill ? 0.0f : 1.0f);
        bypassSm.setTarget(on ? 1.0f : 0.0f);
    }

    if (!on && !bypassSm.isSmoothing() && bypassSm.value() == 0.0f) {
        if (!cleared) { clearState(); cleared = true; }
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    cleared = false;

    const float sampleRate = static_cast<float>(sr);
    for (int i = 0; i < n; ++i) {
        sizeSm.next(); decaySm.next(); hfSm.next(); lowCutSm.next(); diffSm.next(); modDepthSm.next(); modRateSm.next();
        const float preSamples = preSm.next() * 0.001f * sampleRate;
        const float wd = widthSm.next();
        const float wg = wetSm.next();
        const float fz = freezeSm.next();
        const float kl = killSm.next();
        const float bg = bypassSm.next();
        if (ctrlCounter == 0) controlTick();
        ctrlCounter = (ctrlCounter + 1) & (kTick - 1);

        const float gate = bg * (1.0f - fz) * kl;
        const float xL = clampf(sanitize(inL != nullptr ? inL[i] : 0.0f), -kInputClamp, kInputClamp) * gate;
        const float xR = clampf(sanitize(inR != nullptr ? inR[i] : 0.0f), -kInputClamp, kInputClamp) * gate;
        preL.push(xL); preR.push(xR);
        float dL = preL.readLinear(1.0f + preSamples);
        float dR = preR.readLinear(1.0f + preSamples);
        for (int k = 0; k < kDiffusers; ++k) { dL = apL[k].process(dL, diffG); dR = apR[k].process(dR, diffG); }

        float y[kLines], z[kLines];
        float oL = 0.0f, oR = 0.0f;
        for (int k = 0; k < kLines; ++k) {
            auto& l = lines[k];
            l.len += l.lenStep;
            y[k] = readCubic(l.buf, l.len);
            oL += kSignL[k] * y[k];
            oR += kSignR[k] * y[k];
            l.lp = fx::fxFlush(y[k] + dampA * (l.lp - y[k]));   // (1-a) y + a lp
            z[k] = l.lp * l.gain;
        }
        hadamard8(z);
        for (int k = 0; k < kLines; ++k) {
            const float u = 0.5f * ((k & 1) == 0 ? dL : dR);
            const float w = u + z[k] * kInvSqrt8;
            lines[k].buf.push(fx::fxFlush(clampf(fx::fxSoftLimit(w, kStateKnee), -kStateClamp, kStateClamp)));
        }

        oL *= kInvSqrt8 * outNorm;
        oR *= kInvSqrt8 * outNorm;
        const float m = 0.5f * (oL + oR), s = 0.5f * (oL - oR) * wd;
        float aL = lowCutL.process(m + s);
        float aR = lowCutR.process(m - s);
        const float og = wg * bg * kl;
        outL[i] = aL * og;
        outR[i] = aR * og;
    }
}
}
