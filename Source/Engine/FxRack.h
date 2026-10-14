#pragma once
// The five send/return effects (Delay, Echo, Tape, Reverse, Noise). Inputs are the stereo send buses; outputs are the
// wet returns, already scaled by each effect's Return level.
#include "Core/ParamBank.h"
#include "DSP/DigitalDelay.h"
#include "DSP/DspCommon.h"
#include "DSP/EchoThrow.h"
#include "DSP/FxContext.h"
#include "DSP/NoiseEnhancer.h"
#include "DSP/ReverseChunk.h"
#include "DSP/TapeDelay.h"

namespace crowdmike {
class FxRack {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // sendL/sendR/retL/retR have kNumFx entries. preL/preR is the stereo pre-group input mix (Reverse "pre" source).
    void process(const float* const* sendL, const float* const* sendR, const float* preL, const float* preR,
                 float* const* retL, float* const* retR, int n, const ParamBank& bank, const dsp::FxContext& ctx, float rampMs) noexcept;
    dsp::ReverseChunk::Viz getReverseViz() const noexcept { return reverse.getViz(); }
private:
    dsp::DigitalDelay delay;
    dsp::EchoThrow echo;
    dsp::TapeDelay tape;
    dsp::ReverseChunk reverse;
    dsp::NoiseEnhancer noise;
    std::array<dsp::Smoother, kNumFx> returnGain {};
    bool first = true;
};
}
