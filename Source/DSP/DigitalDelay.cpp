#include "DigitalDelay.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr double kDivBeats[13] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 4.0 };
constexpr float kApMsL[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
constexpr float kApMsR[4] = { 5.21f, 3.87f, 11.93f, 8.67f };
constexpr float kApGain = 0.62f;
constexpr float kXfadeSec = 0.04f;
}

double DigitalDelay::divisionBeats(int index) noexcept { return kDivBeats[std::clamp(index, 0, 12)]; }

void DigitalDelay::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const int maxSamples = static_cast<int>(std::ceil((kMaxDelayMs + kMaxOffsetMs) * 0.001 * sr)) + 8;
    bufL.allocate(maxSamples + 1);
    bufR.allocate(maxSamples + 1);
    for (int i = 0; i < 4; ++i) {
        apL[i].allocate(std::max(1, static_cast<int>(std::lround(kApMsL[i] * 0.001 * sr))));
        apR[i].allocate(std::max(1, static_cast<int>(std::lround(kApMsR[i] * 0.001 * sr))));
    }
    inGain.prepare(sr, 20.0f); outGain.prepare(sr, 20.0f); fbAmt.prepare(sr, 30.0f); diffAmt.prepare(sr, 30.0f);
    freezeAmt.prepare(sr, 50.0f); bypassGain.prepare(sr, 10.0f); duck.prepare(sr, 8.0f);
    hpHz.prepare(sr, 50.0f); lpHz.prepare(sr, 50.0f);
    reset();
}

void DigitalDelay::clearState() noexcept {
    bufL.clear(); bufR.clear();
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    for (int i = 0; i < 4; ++i) { apL[i].reset(); apR[i].reset(); }
}

void DigitalDelay::reset() noexcept {
    clearState();
    xfActive = false; xfPos = 0.0f; xfStep = 0.0f;
    ctrlCounter = 0;
    appliedHp = appliedLp = -1.0f;
    fresh = true;
    cleared = true;
}

void DigitalDelay::buildCfg(Cfg& c, int dL, int dR) const noexcept {
    c.dL = dL; c.dR = dR;
    for (int k = 1; k <= kMaxTaps; ++k) {
        const int t = std::min(k, activeTaps);
        c.tapL[k - 1] = std::max(1, static_cast<int>(std::lround(static_cast<double>(t) * dL / activeTaps)));
        c.tapR[k - 1] = std::max(1, static_cast<int>(std::lround(static_cast<double>(t) * dR / activeTaps)));
    }
}

void DigitalDelay::readCfg(const Cfg& c, float& yL, float& yR, float& fL, float& fR) const noexcept {
    fL = bufL.read(c.dL);
    fR = bufR.read(c.dR);
    if (activeMode != 2) { yL = fL; yR = fR; return; }
    // Multi-tap: decaying taps, alternating pan, normalised so two taps sum to unity-ish.
    const float norm = 1.0f / std::sqrt(0.5f * static_cast<float>(activeTaps));
    float g = norm, sL = 0.0f, sR = 0.0f;
    for (int k = 0; k < activeTaps; ++k) {
        const float pl = (k & 1) == 0 ? 1.0f : 0.55f, pr = (k & 1) == 0 ? 0.55f : 1.0f;
        sL += g * pl * bufL.read(c.tapL[k]);
        sR += g * pr * bufR.read(c.tapR[k]);
        g *= 0.85f;
    }
    yL = sL; yR = sR;
}

void DigitalDelay::updateFilters(bool force) noexcept {
    const float h = hpHz.value(), l = lpHz.value();
    if (force || h != appliedHp) {
        const auto k = fx::fxDesignHighpass(sr, h);
        hpL.setCoeffs(k); hpR.setCoeffs(k); appliedHp = h;
    }
    if (force || l != appliedLp) {
        const auto k = fx::fxDesignLowpass(sr, l);
        lpL.setCoeffs(k); lpR.setCoeffs(k); appliedLp = l;
    }
}

void DigitalDelay::process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                           const FxContext& ctx) noexcept {
    namespace P = params::DelayP;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, params::DelayP::defs[i].def, lo, hi); };

    const bool bypass = prm(P::Bypass, 0.0f, 1.0f) > 0.5f;
    const int mode = static_cast<int>(std::lround(prm(P::Mode, 0.0f, 2.0f)));
    const bool sync = prm(P::Sync, 0.0f, 1.0f) > 0.5f;
    const float timeMs = prm(P::TimeMs, 1.0f, kMaxDelayMs);
    const int div = static_cast<int>(std::lround(prm(P::Division, 0.0f, 12.0f)));
    const float fb = prm(P::Feedback, 0.0f, 0.949f);
    const float mix = prm(P::Mix, 0.0f, 1.0f);
    const float hpf = prm(P::Hpf, 10.0f, 20000.0f);
    const float lpf = prm(P::Lpf, 200.0f, 24000.0f);
    const float offMs = prm(P::Offset, 0.0f, kMaxOffsetMs);
    const float diff = prm(P::Diffusion, 0.0f, 1.0f);
    const int taps = static_cast<int>(std::lround(prm(P::Taps, 2.0f, static_cast<float>(kMaxTaps))));
    const float inG = dbToGain(prm(P::InGain, -60.0f, 24.0f));
    const float outG = dbToGain(prm(P::OutGain, -60.0f, 24.0f)) * mix;

    double dMs = timeMs;
    if (sync && ctx.hasTempo && std::isfinite(ctx.bpm) && ctx.bpm > 1.0)
        dMs = divisionBeats(div) * 60000.0 / ctx.bpm;
    dMs = std::min(dMs, static_cast<double>(kMaxDelayMs));
    targetD = std::max(1, static_cast<int>(std::lround(dMs * 0.001 * sr)));
    targetDR = targetD + static_cast<int>(std::lround(offMs * 0.001 * sr));

    if (fresh) {
        inGain.snap(inG); outGain.snap(outG); fbAmt.snap(fb); diffAmt.snap(diff);
        freezeAmt.snap(ctx.freeze ? 1.0f : 0.0f); bypassGain.snap(bypass ? 0.0f : 1.0f); duck.snap(1.0f);
        hpHz.snap(hpf); lpHz.snap(lpf);
        activeMode = wantMode = mode; activeTaps = wantTaps = taps;
        buildCfg(cur, targetD, targetDR);
        updateFilters(true);
        fresh = false;
    } else {
        inGain.setTarget(inG); outGain.setTarget(outG); fbAmt.setTarget(fb); diffAmt.setTarget(diff);
        freezeAmt.setTarget(ctx.freeze ? 1.0f : 0.0f); bypassGain.setTarget(bypass ? 0.0f : 1.0f);
        hpHz.setTarget(hpf); lpHz.setTarget(lpf);
        wantMode = mode; wantTaps = taps;
    }

    if (bypass && !bypassGain.isSmoothing() && bypassGain.value() == 0.0f) {
        if (!cleared) {
            clearState();
            xfActive = false;
            activeMode = wantMode; activeTaps = wantTaps;
            duck.snap(1.0f);
            buildCfg(cur, targetD, targetDR);
            cleared = true;
        }
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    cleared = false;

    for (int i = 0; i < n; ++i) {
        hpHz.next(); lpHz.next();
        if (ctrlCounter == 0) updateFilters(false);
        ctrlCounter = (ctrlCounter + 1) & 15;

        // Mode / tap-count change: duck, switch while silent, come back.
        if (wantMode != activeMode || wantTaps != activeTaps) {
            duck.setTarget(0.0f);
            if (!duck.isSmoothing() && duck.value() == 0.0f) {
                activeMode = wantMode; activeTaps = wantTaps;
                buildCfg(cur, cur.dL, cur.dR);
                if (xfActive) buildCfg(next, next.dL, next.dR);
                duck.setTarget(1.0f);
            }
        }
        // Delay-time change: crossfade read taps (one change at a time).
        if (!xfActive && (cur.dL != targetD || cur.dR != targetDR)) {
            buildCfg(next, targetD, targetDR);
            xfActive = true; xfPos = 0.0f;
            xfStep = 1.0f / std::max(1.0f, kXfadeSec * static_cast<float>(sr));
        }

        const float bg = bypassGain.next();
        const float fz = freezeAmt.next();
        const float ig = inGain.next() * bg * (1.0f - fz);
        const float xL = sanitize(inL != nullptr ? inL[i] : 0.0f) * ig;
        const float xR = sanitize(inR != nullptr ? inR[i] : 0.0f) * ig;

        float yL, yR, fL, fR;
        readCfg(cur, yL, yR, fL, fR);
        if (xfActive) {
            float y2L, y2R, f2L, f2R;
            readCfg(next, y2L, y2R, f2L, f2R);
            const float a = xfPos;
            yL += a * (y2L - yL); yR += a * (y2R - yR);
            fL += a * (f2L - fL); fR += a * (f2R - fR);
            xfPos += xfStep;
            if (xfPos >= 1.0f) { cur = next; xfActive = false; }
        }

        // Feedback path: HPF -> LPF -> (diffusion mix) ; freeze fades the filtering out.
        float lL = lpL.process(hpL.process(fL));
        float lR = lpR.process(hpR.process(fR));
        float aL = lL, aR = lR;
        for (int s = 0; s < 4; ++s) { aL = apL[s].process(aL, kApGain); aR = apR[s].process(aR, kApGain); }
        const float d = diffAmt.next();
        lL += d * (aL - lL); lR += d * (aR - lR);
        lL += fz * (fL - lL); lR += fz * (fR - lR);

        const float fbv = fbAmt.next();
        const float fbe = fbv + fz * (0.97f - fbv);
        float wL, wR;
        if (activeMode == 1) { wL = 0.5f * (xL + xR) + fbe * lR; wR = fbe * lL; }
        else { wL = xL + fbe * lL; wR = xR + fbe * lR; }
        wL = fx::fxFlush(clampf(fx::fxSoftLimit(wL, 1.0f), -2.0f, 2.0f));
        wR = fx::fxFlush(clampf(fx::fxSoftLimit(wR, 1.0f), -2.0f, 2.0f));
        bufL.push(wL); bufR.push(wR);

        const float og = outGain.next() * bg * duck.next();
        outL[i] = yL * og;
        outR[i] = yR * og;
    }
}
}
