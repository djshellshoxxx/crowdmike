#include "WiringTemplates.h"
#include <cmath>

namespace crowdmike::WiringTemplates {
namespace {
const char* const kNames[] = {
    "Direct pass-through", "1 mic > stereo PA", "2 mic stereo crowd > stereo PA", "4 mic room > stereo crowd bus",
    "Front/rear crowd > PA + broadcast", "Crowd capture only", "Virtual soundcheck", "DJ performance FX", "Venue multi-zone" };
const char* const kDescriptions[] = {
    "Processed inputs go straight to outputs. No groups or effects (the default of the first release).",
    "One microphone, centre-panned into the Front Crowd group, master to outputs 1/2.",
    "Inputs 1/2 panned hard left/right into the Front Crowd group, master to outputs 1/2.",
    "Four microphones spread left to right across the Front Crowd group, master to outputs 1/2.",
    "Inputs 1-2 front, 3-4 rear. Master feeds PA outputs 1/2, a Broadcast group (all inputs, lower level) feeds outputs 3/4.",
    "All inputs grouped and recorded; no reinforcement outputs (rehearsal mode on).",
    "As the stereo crowd template with Virtual Soundcheck switched on (load a session in the Playback page).",
    "Stereo crowd into the PA with delay, echo and reverse sends open on the Front Crowd group for performance FX.",
    "Inputs in pairs feed four groups, each group routed to its own output pair for multi-zone venues." };

float safeShare(int count) { return count <= 1 ? 1.0f : 1.0f / std::sqrt(static_cast<float>(count)); }

void routeMaster(TemplateTarget& t, int firstOut) {
    if (t.numOutputs <= firstOut) return;
    if (t.numOutputs >= firstOut + 2) {
        t.routing.setBus(RoutingState::busRowForMaster(0), firstOut, 1.0f);
        t.routing.setBus(RoutingState::busRowForMaster(1), firstOut + 1, 1.0f);
    } else {
        t.routing.setBus(RoutingState::busRowForMaster(0), firstOut, 0.5f);
        t.routing.setBus(RoutingState::busRowForMaster(1), firstOut, 0.5f);
    }
}
void routeGroupToOutputs(TemplateTarget& t, int group, int firstOut) {
    if (t.numOutputs <= firstOut) return;
    if (t.numOutputs >= firstOut + 2) {
        t.routing.setBus(RoutingState::busRowForGroup(group, 0), firstOut, 1.0f);
        t.routing.setBus(RoutingState::busRowForGroup(group, 1), firstOut + 1, 1.0f);
    } else {
        t.routing.setBus(RoutingState::busRowForGroup(group, 0), firstOut, 0.5f);
        t.routing.setBus(RoutingState::busRowForGroup(group, 1), firstOut, 0.5f);
    }
}
void setPan(TemplateTarget& t, int input, float pan) { t.setParam("input" + juce::String(input + 1) + ".pan", pan); }
void crowdInputDefaults(TemplateTarget& t, int count) {
    for (int i = 0; i < count; ++i) {
        t.setParam("input" + juce::String(i + 1) + ".highPassEnabled", 1.0f);
        t.setParam("input" + juce::String(i + 1) + ".highPassHz", 100.0f);
    }
}
void assignRange(TemplateTarget& t, int firstInput, int count, int group) {
    count = juce::jmin(count, t.numInputs - firstInput);
    if (count <= 0) return;
    for (int i = 0; i < count; ++i) t.routing.setAssign(firstInput + i, group, safeShare(count));
}
}

const char* name(Id id) noexcept { return kNames[static_cast<int>(id)]; }
const char* description(Id id) noexcept { return kDescriptions[static_cast<int>(id)]; }

void apply(Id id, TemplateTarget& t) {
    t.routing.clearAll();
    t.routing.setHasSavedDirect(true);
    const int ins = juce::jlimit(1, kMaxInputs, t.numInputs);
    switch (id) {
        case Id::DirectPassthrough:
            t.routing.resetDirectDefault(ins, t.numOutputs);
            t.routing.setHasSavedDirect(false);
            break;
        case Id::OneMicStereoPA:
            t.routing.setAssign(0, 0, 1.0f);
            setPan(t, 0, 0.0f);
            crowdInputDefaults(t, 1);
            routeMaster(t, 0);
            break;
        case Id::TwoMicStereoPA:
        case Id::VirtualSoundcheck: {
            assignRange(t, 0, 2, 0);
            setPan(t, 0, -1.0f);
            if (ins > 1) setPan(t, 1, 1.0f);
            crowdInputDefaults(t, juce::jmin(2, ins));
            routeMaster(t, 0);
            if (id == Id::VirtualSoundcheck) t.setParam("soundcheck", 1.0f);
            break;
        }
        case Id::FourMicRoom: {
            assignRange(t, 0, 4, 0);
            static const float pans[] = { -1.0f, -0.35f, 0.35f, 1.0f };
            for (int i = 0; i < juce::jmin(4, ins); ++i) setPan(t, i, pans[i]);
            crowdInputDefaults(t, juce::jmin(4, ins));
            routeMaster(t, 0);
            break;
        }
        case Id::FrontRearPABroadcast: {
            assignRange(t, 0, 2, 0);
            assignRange(t, 2, 2, 1);
            for (int i = 0; i < juce::jmin(4, ins); ++i) {
                setPan(t, i, (i % 2 == 0) ? -0.8f : 0.8f);
                // Broadcast group (5) gets every input at a lower level.
                t.routing.setAssign(i, 4, 0.5f * safeShare(juce::jmin(4, ins)));
            }
            crowdInputDefaults(t, juce::jmin(4, ins));
            routeMaster(t, 0);
            routeGroupToOutputs(t, 4, 2);
            break;
        }
        case Id::CaptureOnly:
            assignRange(t, 0, ins, 0);
            for (int i = 0; i < ins; ++i) setPan(t, i, ins == 1 ? 0.0f : (i % 2 == 0 ? -1.0f : 1.0f));
            crowdInputDefaults(t, ins);
            t.setParam("rehearsal", 1.0f);
            break;
        case Id::DjPerformanceFx:
            assignRange(t, 0, juce::jmin(2, ins), 0);
            setPan(t, 0, ins > 1 ? -1.0f : 0.0f);
            if (ins > 1) setPan(t, 1, 1.0f);
            crowdInputDefaults(t, juce::jmin(2, ins));
            t.setParam("group1.fxSend1", -12.0f);
            t.setParam("group1.fxSend2", -12.0f);
            t.setParam("group1.fxSend4", -18.0f);
            routeMaster(t, 0);
            break;
        case Id::VenueMultiZone:
            for (int g = 0; g < 4; ++g) {
                assignRange(t, g * 2, 2, g);
                if (g * 2 < ins) setPan(t, g * 2, -1.0f);
                if (g * 2 + 1 < ins) setPan(t, g * 2 + 1, 1.0f);
                routeGroupToOutputs(t, g, g * 2);
            }
            crowdInputDefaults(t, ins);
            break;
        case Id::Count: break;
    }
    if (id != Id::CaptureOnly) t.setParam("rehearsal", 0.0f);
}
}
