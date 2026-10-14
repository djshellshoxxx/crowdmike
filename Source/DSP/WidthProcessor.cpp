#include "WidthProcessor.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr float kApMs[4] = { 1.31f, 2.27f, 3.53f, 5.17f };
constexpr float kApG = 0.5f;
constexpr float kHaasSplitHz = 1000.0f;
// Butterworth 4th order = two biquads with these Q values.
constexpr float kQ4a = 0.5411961f, kQ4b = 1.3065630f;
}

void WidthProcessor::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (int i = 0; i < 4; ++i) ap[i].allocate(std::max(1, static_cast<int>(std::lround(kApMs[i] * 0.001 * sr))));
    haasBuf.allocate(static_cast<int>(std::ceil((kMaxHaasMs + 2.0f) * 0.001 * sr)) + 4);
    haasLp.setCoeffs(fx::fxDesignLowpass(sr, kHaasSplitHz));
    onGain.prepare(sr, 10.0f); width.prepare(sr, 20.0f); decorr.prepare(sr, 20.0f);
    haasSamples.prepare(sr, 50.0f); mono.prepare(sr, 20.0f); lfAmt.prepare(sr, 20.0f); lfHz.prepare(sr, 50.0f);
    reset();
}

void WidthProcessor::clearState() noexcept {
    lfHp1.reset(); lfHp2.reset(); decHp.reset(); haasLp.reset();
    for (auto& a : ap) a.reset();
    haasBuf.clear();
}

void WidthProcessor::reset() noexcept {
    clearState();
    appliedLf = -1.0f;
    ctrlCounter = 0;
    fresh = true; idle = false;
}

void WidthProcessor::updateFilters(bool force) noexcept {
    const float hz = lfHz.value();
    if (!force && hz == appliedLf) return;
    lfHp1.setCoeffs(fx::fxDesignHighpass(sr, hz, kQ4a));
    lfHp2.setCoeffs(fx::fxDesignHighpass(sr, hz, kQ4b));
    decHp.setCoeffs(fx::fxDesignHighpass(sr, std::max(hz, 150.0f)));
    appliedLf = hz;
}

void WidthProcessor::process(float* left, float* right, int n, const float* p) noexcept {
    namespace P = params::WidthP;
    if (n <= 0 || left == nullptr || right == nullptr) return;
    auto prm = [p](int i, float def, float lo, float hi) { return fx::fxParam(p, i, def, lo, hi); };

    const bool on = prm(P::On, 0.0f, 0.0f, 1.0f) > 0.5f;
    const float vWidth = prm(P::Width, 1.0f, 0.0f, 2.0f);
    const float vDec = prm(P::Decorr, 0.0f, 0.0f, 1.0f);
    const float vHaas = prm(P::HaasMs, 0.0f, 0.0f, kMaxHaasMs) * 0.001f * static_cast<float>(sr);
    const float vMono = prm(P::MonoCompat, 0.0f, 0.0f, 1.0f);
    const float vLfRaw = prm(P::LfMonoHz, 120.0f, 0.0f, 2000.0f);
    const float vLfAmt = vLfRaw > 20.0f ? 1.0f : 0.0f;
    const float vLf = std::max(vLfRaw, 20.0f);

    if (fresh) {
        onGain.snap(on ? 1.0f : 0.0f); width.snap(vWidth); decorr.snap(vDec); haasSamples.snap(vHaas);
        mono.snap(vMono); lfAmt.snap(vLfAmt); lfHz.snap(vLf);
        updateFilters(true);
        fresh = false;
        idle = !on;
    } else {
        if (on && idle) {
            // Waking from a settled bypass: start from a clean state (deterministic) and snap the controls.
            clearState();
            width.snap(vWidth); decorr.snap(vDec); haasSamples.snap(vHaas); mono.snap(vMono);
            lfAmt.snap(vLfAmt); lfHz.snap(vLf);
            updateFilters(true);
            idle = false;
        }
        onGain.setTarget(on ? 1.0f : 0.0f); width.setTarget(vWidth); decorr.setTarget(vDec);
        haasSamples.setTarget(vHaas); mono.setTarget(vMono); lfAmt.setTarget(vLfAmt); lfHz.setTarget(vLf);
    }

    if (!on && !onGain.isSmoothing() && onGain.value() == 0.0f) {
        idle = true;
        return;   // bit-exact pass-through
    }

    for (int i = 0; i < n; ++i) {
        lfHz.next();
        if (ctrlCounter == 0) updateFilters(false);
        ctrlCounter = (ctrlCounter + 1) & 15;

        const float dryL = left[i], dryR = right[i];
        const float L = sanitize(dryL), R = sanitize(dryR);
        const float M = 0.5f * (L + R), S = 0.5f * (L - R);

        const float la = lfAmt.next();
        const float sHp = lfHp2.process(lfHp1.process(S));
        const float s1 = la > 0.0f ? S + la * (sHp - S) : S;

        float d = decHp.process(M);
        for (auto& a : ap) d = a.process(d, kApG);
        const float dc = decorr.next();
        const float s2 = width.next() * (dc > 0.0f ? s1 + 0.7f * dc * d : s1);

        const float L1 = M + s2;
        const float R1 = M - s2;

        const float lo = haasLp.process(R1);
        const float hi = R1 - lo;
        haasBuf.push(hi);
        const float hs = haasSamples.next();
        const float R2 = (hs > 0.0f || haasSamples.isSmoothing()) ? lo + haasBuf.readLinear(hs + 1.0f) : R1;

        const float c = mono.next();
        float oL = L1, oR = R2;
        if (c > 0.0f) {
            const float m = 0.5f * (L1 + R2);
            oL += c * (m - oL);
            oR += c * (m - oR);
        }

        const float g = onGain.next();
        if (g >= 1.0f) { left[i] = oL; right[i] = oR; }
        else {
            const float sl = sanitize(dryL), sr2 = sanitize(dryR);
            left[i] = sl + g * (oL - sl);
            right[i] = sr2 + g * (oR - sr2);
        }
    }
}
}
