#include "MasterBus.h"

namespace crowdmike {
using namespace params;

void MasterBus::prepare(double sr, int maxBlock) {
    eq.prepare(sr, maxBlock, 2, 8);
    comp.prepare(sr, maxBlock, 2);
    width.prepare(sr, maxBlock);
    limiter.prepare(sr, maxBlock, 2, 0.0f);
    level.prepare(sr, 20.0f);
    first = true;
}
void MasterBus::reset() { eq.reset(); comp.reset(); width.reset(); limiter.reset(); first = true; }

void MasterBus::process(float* left, float* right, int n, const float* p, float rampMs) noexcept {
    namespace P = MasterP;
    float* ch[2] = { left, right };
    eq.process(ch, 2, n, p[P::EqOn] >= 0.5f ? p + P::Eq1On : nullptr, 8);
    comp.process(ch, 2, n, p + P::MstCompOn);
    width.process(left, right, n, p + P::WidthOn);
    const float target = p[P::Mute] >= 0.5f ? 0.0f : dsp::dbToGainOrZero(p[P::Level], -79.9f);
    if (first) { level.snap(target); first = false; }
    level.setTarget(target, rampMs);
    if (level.isSmoothing()) for (int i = 0; i < n; ++i) { const float g = level.next(); left[i] *= g; right[i] *= g; }
    else if (level.value() != 1.0f) for (int i = 0; i < n; ++i) { left[i] *= level.value(); right[i] *= level.value(); }
    limiter.process(ch, 2, n, p + P::MstLimOn, true);
}
}
