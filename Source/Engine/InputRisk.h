#pragma once
// InputRisk -- gathers the per-input feedback-risk model inputs (ROUTING_SPEC section 6, GUI_SPEC section 3) from the
// message-thread state and scores them with dsp::scoreRisk. Advisory only; never touches audio.
//
// For input i every active output o is examined along each path that exists:
//   direct  : in.trim + 20log|direct(i,o)|                                + out.trim
//   group   : in.trim + 20log(assign(i,g)) + group.trim + 20log|bus(G,o)| + out.trim        (G = group L/R bus row)
//   master  : in.trim + 20log(assign(i,g)) + group.trim + master.level + 20log|bus(M,o)| + out.trim
//   effect  : in.trim + send(i,f) + fx return (+ reverb wet / multiplier level) + [master.level + 20log|bus(M,o)| | 20log|bus(FX f,o)|] + out.trim
//             (only for an open send into a running effect; FX returns always sum into the master)
// Muted inputs / groups / master / outputs break their paths. The loudest path wins; the result for that path also gets
//   * compressor makeup of the input (when its compressor is on),
//   * the delay/tape feedback amount when the input sends to that (non-bypassed) effect (recirculation boost),
//   * the persistent-peak severity reported by the feedback detector when its tap is this input (or the master and
//     the winning path runs through the master),
//   * sameZone = the input and the output share a non-empty ShowModel zone.
// Score is in 0..1 (see dsp::scoreRisk); level thresholds dsp::risk::kCautionThreshold / kHighThreshold.
#include "Core/ParamBank.h"
#include "Core/ShowModel.h"
#include "DSP/RouteRisk.h"
#include "Routing/RoutingState.h"

namespace crowdmike {
struct InputRiskContext {
    const ParamBank& bank;            // rawAt() gives actual units (dB / 0..1 / bool)
    const RoutingState& routing;
    const ShowModel& show;
    int numOutputs = 2;
    int feedbackTap = 0;              // 0 master, 1..16 inputs, 17..24 groups (CrowdEngine::setFeedbackTap)
    float tapSeverity = 0.0f;         // strongest persistent candidate severity on that tap (0 if none / unknown)
};

struct InputRiskReport {
    dsp::RiskInputs inputs;           // the worst path's model inputs
    dsp::RiskResult result;
    int worstOutput = -1;
};

InputRiskReport evaluateInputRisk(const InputRiskContext&, int input);
}
