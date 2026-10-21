#pragma once
// The seven send/return effects (Delay, Echo, Tape, Reverse, Noise, Stadium Reverb, Crowd Multiplier). Inputs are the stereo send
// buses; outputs are the wet returns, already scaled by each effect's Return level. Slot order = Limits.h kNumFx order.
#include "Core/ParamBank.h"
#include "DSP/CrowdMultiplier.h"
#include "DSP/DigitalDelay.h"
#include "DSP/DspCommon.h"
#include "DSP/EchoThrow.h"
#include "DSP/FxContext.h"
#include "DSP/NoiseEnhancer.h"
#include "DSP/ReverseChunk.h"
#include "DSP/StadiumReverb.h"
#include "DSP/TapeDelay.h"

namespace crowdmike {
// Beat-quantised momentary triggers (docs/BEAT_SYNC_SPEC.md section 7). When `*Override` is set the engine owns the
// trigger: the effect sees Trigger = 0 for the whole block, except from sample `*FireAt` (>= 0) where it sees a rising edge.
struct FxTriggerPlan {
    bool echoOverride = false, reverseOverride = false;
    int echoFireAt = -1, reverseFireAt = -1;
    // Panic: the Stadium Reverb tail is killed while panic is engaged, so a held (Freeze) or long tail cannot resurface at full
    // level when the operator releases panic (ENGINE_SPEC section 9). The other effects keep their historic behaviour.
    bool panicKillReverb = false;
};

class FxRack {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // sendL/sendR/retL/retR have kNumFx entries. preL/preR is the stereo pre-group input mix (Reverse "pre" source).
    void process(const float* const* sendL, const float* const* sendR, const float* preL, const float* preR,
                 float* const* retL, float* const* retR, int n, const ParamBank& bank, const dsp::FxContext& ctx, float rampMs,
                 const FxTriggerPlan* plan = nullptr) noexcept;
    dsp::ReverseChunk::Viz getReverseViz() const noexcept { return reverse.getViz(); }
    std::uint32_t getReverseTriggerCount() const noexcept { return reverse.getTriggerCount(); }   // Show Report counter
private:
    dsp::DigitalDelay delay;
    dsp::EchoThrow echo;
    dsp::TapeDelay tape;
    dsp::ReverseChunk reverse;
    dsp::NoiseEnhancer noise;
    dsp::StadiumReverb reverb;
    dsp::CrowdMultiplier multi;
    std::array<dsp::Smoother, kNumFx> returnGain {};
    bool first = true;
    double sampleRateHz = 48000.0;
};
}
