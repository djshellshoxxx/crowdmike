#include "GroupChannel.h"
#include "Engine/Panning.h"

namespace crowdmike {
using namespace params;

void GroupChannel::prepare(double sr, int maxBlock) {
    hp.prepare(sr, maxBlock, 2, true);
    lp.prepare(sr, maxBlock, 2, false);
    eq.prepare(sr, maxBlock, 2, 4);
    comp.prepare(sr, maxBlock, 2);
    width.prepare(sr, maxBlock);
    limiter.prepare(sr, maxBlock, 2, 0.0f);
    delay.prepare(sr, maxBlock, 2, 500.0f);
    gainL.prepare(sr, 20.0f);
    gainR.prepare(sr, 20.0f);
    first = true;
}

void GroupChannel::reset() {
    hp.reset(); lp.reset(); eq.reset(); comp.reset(); width.reset(); limiter.reset(); delay.reset();
    first = true;
}

void GroupChannel::process(float* left, float* right, int n, const float* p, bool mutedBySolo, float rampMs) noexcept {
    using P = GroupP;
    float* ch[2] = { left, right };
    hp.process(ch, 2, n, p[P::HpOn] >= 0.5f, p[P::HpHz], 0);
    lp.process(ch, 2, n, p[P::LpOn] >= 0.5f, p[P::LpHz], 0);
    eq.process(ch, 2, n, p + P::Eq1On, 4);
    comp.process(ch, 2, n, p + P::GrpCompOn);

    const float widthParams[6] = { std::abs(p[P::Width] - 1.0f) > 0.001f ? 1.0f : 0.0f, p[P::Width], 0.0f, 0.0f, 0.0f, 120.0f };
    width.process(left, right, n, widthParams);
    limiter.process(ch, 2, n, p + P::GrpLimOn, true);
    delay.process(ch, 2, n, p[P::DelayMs]);

    float bl, br;
    balanceGains(p[P::Pan], bl, br);
    const bool silent = p[P::Mute] >= 0.5f || mutedBySolo;
    const float base = silent ? 0.0f : dsp::dbToGain(p[P::Trim]) * (p[P::Pol] >= 0.5f ? -1.0f : 1.0f);
    if (first) { gainL.snap(base * bl); gainR.snap(base * br); first = false; }
    gainL.setTarget(base * bl, rampMs);
    gainR.setTarget(base * br, rampMs);
    if (gainL.isSmoothing() || gainR.isSmoothing()) {
        for (int i = 0; i < n; ++i) { left[i] *= gainL.next(); right[i] *= gainR.next(); }
    } else {
        for (int i = 0; i < n; ++i) { left[i] *= gainL.value(); right[i] *= gainR.value(); }
    }
}
}
