#pragma once
// Bus sources (group L/R, FX return L/R, playback L/R, master L/R) -> output channels, with ramped gains.
// Targets are synced from RoutingState once per revision; ramps use the scene-recall time when one is active.
#include <array>
#include "Core/Limits.h"
#include "DSP/DspCommon.h"
#include "Routing/RoutingState.h"

namespace crowdmike {
class BusMatrix {
public:
    void prepare(double sampleRate, float defaultRampMs) noexcept;
    // Re-targets every cell from the routing state. rampMs <= 0 uses the default.
    void syncTargets(const RoutingState&, int numOutputs, float rampMs) noexcept;
    void snapToTargets() noexcept;
    // Adds the routed sources into dst[0..numOut). `sources` has kBusSrcCount entries (nullptr = silent).
    void process(const float* const* sources, float* const* dst, int numOut, int n) noexcept;
private:
    std::array<dsp::Smoother, kBusSrcCount * kMaxOutputs> cells {};
};
}
