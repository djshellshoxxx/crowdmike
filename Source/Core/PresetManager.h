#pragma once
// Versioned presets with scopes (CONTROL_SPEC section 2). A preset is a list of parameter *suffix* -> value pairs plus
// optional routing/show payloads, so a preset saved from "Input 3" loads into any other input, and a compressor preset
// loads into an input, group or master compressor.
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "Core/ParamBank.h"
#include "Core/ShowModel.h"
#include "Routing/RoutingState.h"

namespace crowdmike {
enum class PresetScope { Show, Input, Group, Effect, MasterChain, Routing, Block };

struct FactoryPreset {
    const char* name;
    PresetScope scope;
    const char* target;                 // module hint: "input" "group" "master" "delay" "echo" "tape" "reverse" "noise" or "" for any
    const char* blockPrefix;            // for Block scope: suffix prefix filter, e.g. "comp"
    std::vector<std::pair<const char*, float>> values;
};

namespace PresetManager {
constexpr int kPresetSchemaVersion = 1;
extern const char* const kPresetTag;     // "CrowdMikePreset"

const std::vector<FactoryPreset>& factoryPresets();
std::vector<const FactoryPreset*> factoryPresetsFor(const juce::String& target, const juce::String& blockPrefix = {});

// Capture parameter suffix/value pairs of one module instance (optionally only suffixes starting with `blockPrefix`).
juce::ValueTree capture(juce::AudioProcessorValueTreeState&, Mod module, int instance, const juce::String& blockPrefix = {});
// Apply to another instance. Unknown suffixes are ignored; values clamp to range. Returns number of parameters changed.
int apply(juce::AudioProcessorValueTreeState&, const juce::ValueTree& preset, Mod module, int instance);
int applyFactory(juce::AudioProcessorValueTreeState&, const FactoryPreset&, Mod module, int instance);

juce::ValueTree captureShow(juce::AudioProcessorValueTreeState&, const RoutingState&, const ShowModel&);
bool applyShow(juce::AudioProcessorValueTreeState&, RoutingState&, ShowModel&, const juce::ValueTree&);
juce::ValueTree captureRouting(const RoutingState&);
bool applyRouting(RoutingState&, const juce::ValueTree&);

bool saveToFile(const juce::ValueTree& preset, const juce::File&);
juce::ValueTree loadFromFile(const juce::File&);     // invalid tree on failure / newer schema
juce::File userPresetFolder();
}
}
