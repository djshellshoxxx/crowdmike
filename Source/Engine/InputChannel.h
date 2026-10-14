#pragma once
// One input strip (or a linked stereo pair): the default input chain of EFFECTS_SPEC section 18
//   Trim -> Polarity -> HPF/LPF -> Notch EQ -> Phase -> Align Delay -> Gate -> De-esser -> Compressor -> Transient -> Riding
// Mute/solo are folded into the strip gain so they ramp click-free.
#include <atomic>
#include "Core/ParamTables.h"
#include "DSP/AllpassPhase.h"
#include "DSP/ChannelDelay.h"
#include "DSP/Compressor.h"
#include "DSP/DeEsser.h"
#include "DSP/EqBank.h"
#include "DSP/Gate.h"
#include "DSP/GainRider.h"
#include "DSP/InputStrip.h"
#include "DSP/TransientEnhancer.h"

namespace crowdmike {
class InputChannel {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // `params` points at this input's InputP block of the effective parameter array. `nch` is 1 or 2 (linked pair).
    // `preDynamics` (optional, same shape as ch) receives a copy taken just before the gate when non-null.
    void process(float* const* ch, int nch, int n, const float* params, bool mutedBySolo, float* const* preDynamics) noexcept;
    float getCompressorGrDb() const noexcept { return comp.getGainReductionDb(); }
    float getGateGrDb() const noexcept { return gate.getGainReductionDb(); }
    float getDeEssGrDb() const noexcept { return deess.getGainReductionDb(); }
private:
    InputStrip strip;
    dsp::EqBank eq;
    dsp::AllpassPhase phase;
    dsp::ChannelDelay align;
    dsp::Gate gate;
    dsp::DeEsser deess;
    dsp::Compressor comp;
    dsp::TransientEnhancer trans;
    dsp::GainRider rider;
    float eqBands[5 * params::kEqBandStride] {};
    double sr = 48000.0;
};
}
