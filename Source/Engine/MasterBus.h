#pragma once
// Master crowd bus: EQ(8) -> Glue compressor -> Width -> Level/Mute -> Limiter (EFFECTS_SPEC section 18).
#include "Core/ParamTables.h"
#include "DSP/Compressor.h"
#include "DSP/DspCommon.h"
#include "DSP/EqBank.h"
#include "DSP/Limiter.h"
#include "DSP/WidthProcessor.h"

namespace crowdmike {
class MasterBus {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    void process(float* left, float* right, int n, const float* params, float rampMs) noexcept;
    float getCompressorGrDb() const noexcept { return comp.getGainReductionDb(); }
    float getLimiterGrDb() const noexcept { return limiter.getGainReductionDb(); }
private:
    dsp::EqBank eq;
    dsp::Compressor comp;
    dsp::WidthProcessor width;
    dsp::Limiter limiter;
    dsp::Smoother level;
    bool first = true;
};
}
