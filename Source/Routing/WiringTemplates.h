#pragma once
// Wiring presets (ROUTING_SPEC section 8). Each template rewrites the three routing matrices and sets a handful of
// parameters through `setParam` (actual units, by parameter ID) so the caller can make the whole change one undo step.
#include <juce_core/juce_core.h>
#include <functional>
#include "Core/ShowModel.h"
#include "Routing/RoutingState.h"

namespace crowdmike::WiringTemplates {
enum class Id : int {
    DirectPassthrough = 0,      // first-release behaviour: processed inputs pass straight to the outputs
    OneMicStereoPA,
    TwoMicStereoPA,
    FourMicRoom,
    FrontRearPABroadcast,
    CaptureOnly,
    VirtualSoundcheck,
    DjPerformanceFx,
    VenueMultiZone,
    Count
};

struct TemplateTarget {
    RoutingState& routing;
    ShowModel& show;
    int numInputs;
    int numOutputs;
    std::function<void(const juce::String& paramId, float actualValue)> setParam;
};

const char* name(Id) noexcept;
const char* description(Id) noexcept;
void apply(Id, TemplateTarget&);
}
