#include "OutputStage.h"

namespace crowdmike {
using namespace params;

void OutputStage::prepare(double sr, int maxBlock) {
    hp.prepare(sr, maxBlock, 1, true);
    lp.prepare(sr, maxBlock, 1, false);
    delay.prepare(sr, maxBlock, 1, 500.0f);
    limiter.prepare(sr, maxBlock, 1, kLookaheadMs);
    gain.prepare(sr, 20.0f);
    first = true;
}
void OutputStage::reset() { hp.reset(); lp.reset(); delay.reset(); limiter.reset(); first = true; }

void OutputStage::process(float* ch, int n, const float* p, bool limiterEnabled, float ceilingDb, float releaseMs, float rampMs) noexcept {
    namespace P = OutputP;
    float* ptr[1] = { ch };
    const float target = p[P::Mute] >= 0.5f ? 0.0f : dsp::dbToGain(p[P::Trim]) * (p[P::Pol] >= 0.5f ? -1.0f : 1.0f);
    if (first) { gain.snap(target); first = false; }
    gain.setTarget(target, rampMs);
    if (gain.isSmoothing()) for (int i = 0; i < n; ++i) ch[i] *= gain.next();
    else if (gain.value() != 1.0f) for (int i = 0; i < n; ++i) ch[i] *= gain.value();
    hp.process(ptr, 1, n, p[P::HpOn] >= 0.5f, p[P::HpHz], 0);
    lp.process(ptr, 1, n, p[P::LpOn] >= 0.5f, p[P::LpHz], 0);
    delay.process(ptr, 1, n, p[P::DelayMs]);
    const float lim[4] = { limiterEnabled ? 1.0f : 0.0f, ceilingDb, releaseMs, 0.0f };
    limiter.process(ptr, 1, n, lim, false);
}
}
