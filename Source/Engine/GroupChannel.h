#pragma once
// One crowd group (stereo submix) after the input mix: HPF/LPF -> EQ -> Compressor -> Width -> Limiter -> Delay -> fader
// (ENGINE_SPEC section 7, EFFECTS_SPEC section 18). Sends tap the post-fader signal.
#include "Core/ParamTables.h"
#include "DSP/ChannelDelay.h"
#include "DSP/Compressor.h"
#include "DSP/DspCommon.h"
#include "DSP/EqBank.h"
#include "DSP/Limiter.h"
#include "DSP/SlopeFilter.h"
#include "DSP/WidthProcessor.h"

namespace crowdmike {
class GroupChannel {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // In-place on left/right. `params` = this group's GroupP block. rampMs <= 0 -> default fader smoothing.
    void process(float* left, float* right, int n, const float* params, bool mutedBySolo, float rampMs) noexcept;
    float getCompressorGrDb() const noexcept { return comp.getGainReductionDb(); }
    float getLimiterGrDb() const noexcept { return limiter.getGainReductionDb(); }
private:
    dsp::SlopeFilter hp, lp;
    dsp::EqBank eq;
    dsp::Compressor comp;
    dsp::WidthProcessor width;
    dsp::Limiter limiter;
    dsp::ChannelDelay delay;
    dsp::Smoother gainL, gainR;
    bool first = true;
};
}
