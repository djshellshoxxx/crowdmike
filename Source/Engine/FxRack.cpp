#include "FxRack.h"

namespace crowdmike {
using namespace params;

void FxRack::prepare(double sr, int maxBlock) {
    delay.prepare(sr, maxBlock);
    echo.prepare(sr, maxBlock);
    tape.prepare(sr, maxBlock);
    reverse.prepare(sr, maxBlock);
    noise.prepare(sr, maxBlock);
    for (auto& g : returnGain) g.prepare(sr, 20.0f);
    first = true;
}
void FxRack::reset() { delay.reset(); echo.reset(); tape.reset(); reverse.reset(); noise.reset(); first = true; }

void FxRack::process(const float* const* sL, const float* const* sR, const float* preL, const float* preR,
                     float* const* rL, float* const* rR, int n, const ParamBank& bank, const dsp::FxContext& ctx, float rampMs) noexcept {
    const float* pDelay = bank.effective(Mod::FxDelay);
    const float* pEcho = bank.effective(Mod::FxEcho);
    const float* pTape = bank.effective(Mod::FxTape);
    const float* pRev = bank.effective(Mod::FxReverse);
    const float* pNoise = bank.effective(Mod::FxNoise);
    delay.process(sL[0], sR[0], rL[0], rR[0], n, pDelay, ctx);
    echo.process(sL[1], sR[1], rL[1], rR[1], n, pEcho, ctx);
    tape.process(sL[2], sR[2], rL[2], rR[2], n, pTape, ctx);
    reverse.process(sL[3], sR[3], preL, preR, rL[3], rR[3], n, pRev, ctx);
    noise.process(sL[4], sR[4], rL[4], rR[4], n, pNoise, ctx);
    const float returns[kNumFx] = { pDelay[DelayP::Return], pEcho[EchoP::Return], pTape[TapeP::Return], pRev[ReverseP::Return], pNoise[NoiseP::Return] };
    for (int f = 0; f < kNumFx; ++f) {
        auto& g = returnGain[static_cast<size_t>(f)];
        const float target = dsp::dbToGainOrZero(returns[f]);
        if (first) g.snap(target);
        g.setTarget(target, rampMs);
        float* l = rL[f];
        float* r = rR[f];
        if (g.isSmoothing()) for (int i = 0; i < n; ++i) { const float v = g.next(); l[i] *= v; r[i] *= v; }
        else if (g.value() != 1.0f) for (int i = 0; i < n; ++i) { l[i] *= g.value(); r[i] *= g.value(); }
    }
    first = false;
}
}
