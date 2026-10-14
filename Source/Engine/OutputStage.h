#pragma once
// Per-output-channel stage: polarity/trim/mute -> HPF/LPF protection -> delay -> lookahead limiter (IO_SPEC section 5).
// Every channel carries the same limiter lookahead, so total plug-in latency is one constant.
#include "Core/ParamTables.h"
#include "DSP/ChannelDelay.h"
#include "DSP/DspCommon.h"
#include "DSP/Limiter.h"
#include "DSP/SlopeFilter.h"

namespace crowdmike {
class OutputStage {
public:
    static constexpr float kLookaheadMs = 1.5f;
    void prepare(double sampleRate, int maxBlock);
    void reset();
    int getLatencySamples() const noexcept { return limiter.getLatencySamples(); }
    // limiterEnabled is the *effective* state (forced on unless the user unlocked output limiters).
    void process(float* ch, int n, const float* params, bool limiterEnabled, float ceilingDb, float releaseMs, float rampMs) noexcept;
    float getLimiterGrDb() const noexcept { return limiter.getGainReductionDb(); }
private:
    dsp::SlopeFilter hp, lp;
    dsp::ChannelDelay delay;
    dsp::Limiter limiter;
    dsp::Smoother gain;
    bool first = true;
};
}
