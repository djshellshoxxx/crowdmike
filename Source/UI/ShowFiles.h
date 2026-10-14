#pragma once
// Show-file helpers for the Settings page. A .crowdshow file is exactly the processor state blob
// (getStateInformation / setStateInformation), so it carries parameters, routing, show model and scenes.
#include "PluginProcessor.h"

namespace crowdmike::ui {
constexpr const char* kShowFileExtension = ".crowdshow";
bool saveShowFile(CrowdMikeAudioProcessor&, const juce::File&, juce::String& error);
// Restores the blob; returns false (state untouched) when the file is missing or not a CrowdMike state.
bool loadShowFile(CrowdMikeAudioProcessor&, const juce::File&, juce::String& error);
// Every parameter to its default, show model defaults, default direct routing; one undo transaction for parameters.
void resetShowToDefaults(CrowdMikeAudioProcessor&);
}
