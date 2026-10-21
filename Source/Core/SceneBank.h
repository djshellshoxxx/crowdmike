#pragma once
// Scenes: live snapshots of the whole show (PRODUCT_SPEC section 5, CONTROL_SPEC section 3).
// A scene stores every parameter that differs from its default, the routing matrices, port names and macro maps.
// Recall supports partial scopes, locked parameters and a safe ramp (the engine stretches its smoothers for the ramp time).
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include "Core/Limits.h"
#include "Core/ParamBank.h"
#include "Core/ShowModel.h"
#include "Routing/RoutingState.h"

namespace crowdmike {
enum SceneScope : unsigned {
    ScopeInputs = 1u << 0, ScopeGroups = 1u << 1, ScopeFx = 1u << 2, ScopeMaster = 1u << 3, ScopeOutputs = 1u << 4,
    ScopeRouting = 1u << 5, ScopeNames = 1u << 6, ScopeMacros = 1u << 7, ScopeAll = 0xFFu
};

struct Scene {
    bool used = false;
    juce::String name, notes;
    juce::ValueTree data;     // "SceneData": Params (non-default props), Routing, Show
};

class SceneBank {
public:
    SceneBank(juce::AudioProcessorValueTreeState& apvts, RoutingState& routing, ShowModel& show);

    // Parameters that are commands/momentary state rather than show configuration; never stored or recalled.
    static bool isTransientParam(int globalIndex);
    static unsigned scopeForParam(int globalIndex);

    void capture(int slot, const juce::String& name = {});
    // Message thread only. `rampMs < 0` uses show.ui.recallRampMs. Returns false for an empty/invalid slot.
    bool recall(int slot, unsigned scope = ScopeAll, float rampMs = -1.0f);
    bool next(unsigned scope = ScopeAll);       // wraps to the next used scene; `scope` as for recall()
    bool previous(unsigned scope = ScopeAll);
    int current() const noexcept { return currentSlot; }
    int countUsed() const;
    Scene& at(int slot) { return scenes[static_cast<size_t>(slot)]; }
    const Scene& at(int slot) const { return scenes[static_cast<size_t>(slot)]; }
    void clear(int slot);
    void duplicate(int from, int to);

    // Called with the ramp time right before parameters change so the engine can stretch its smoothers.
    std::function<void(float rampMs)> onRecallRamp;
    std::function<void()> onRecalled;

    juce::ValueTree toValueTree() const;
    bool fromValueTree(const juce::ValueTree&);

private:
    juce::AudioProcessorValueTreeState& apvts;
    RoutingState& routing;
    ShowModel& show;
    std::array<Scene, kNumScenes> scenes;
    int currentSlot = -1;
};
}
