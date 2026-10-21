#pragma once
// Plain-text diagnostics snapshot (Diagnostics page + "copy diagnostics to clipboard"). Message thread.
#include "PluginProcessor.h"

namespace crowdmike::ui {
struct LatencyBreakdown {
    double sampleRate = 0.0;
    int algorithmic = 0, block = 0, deviceIn = 0, deviceOut = 0;
    int total() const noexcept { return algorithmic + block + deviceIn + deviceOut; }
};
LatencyBreakdown latencyBreakdown(CrowdMikeAudioProcessor&);
juce::String samplesToMs(int samples, double sampleRate);
// Section titled lines: "Engine", "Latency", "Recorder", "Analysis", "Outputs".
juce::StringArray diagnosticsLines(CrowdMikeAudioProcessor&);
juce::String buildDiagnosticsReport(CrowdMikeAudioProcessor&);
}
