#pragma once
// Top-level plug-in state: versioned XML (CONTROL_SPEC sections 7 and 10).
//   schemaVersion 1 : first release. APVTS parameters + <RoutingMatrix r<in>_<out>=gain/> (direct matrix only)
//   schemaVersion 2 : adds <Routing> (direct+assign+bus), <Show>, <Scenes>, <EngineConfig>, appVersion, platform
// Migrations are explicit, forward-only and covered by tests (tests/engine/StateTest.cpp).
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "Core/SceneBank.h"

namespace crowdmike::StateIO {
constexpr int kSchemaVersion = 2;
extern const char* const kStateType;   // "CrowdMikeState"

struct Parts {
    juce::AudioProcessorValueTreeState& apvts;
    RoutingState& routing;
    ShowModel& show;
    SceneBank& scenes;
};
struct EngineConfigInfo { double sampleRate = 0.0; int blockSize = 0, inputs = 0, outputs = 0; };

enum class Result { Ok, Invalid, NewerThanSupported };

std::unique_ptr<juce::XmlElement> serialize(Parts parts, const EngineConfigInfo& engine);
// Applies a migration chain in place (v1 -> v2 ...). Returns the schema version found (or -1 when not a CrowdMike state).
int migrateToCurrent(juce::XmlElement& xml);
Result restore(Parts parts, const juce::XmlElement& xml, int activeInputs, int activeOutputs);
}
