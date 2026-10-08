#pragma once
#include <JuceHeader.h>
#include "DSP/InputStrip.h"
#include "DSP/RoutingMatrix.h"
#include "DSP/SafetyLimiter.h"

class CrowdMikeAudioProcessor final : public juce::AudioProcessor {
public:
    CrowdMikeAudioProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "CrowdMike"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
private:
    std::vector<crowdmike::InputStrip> inputs;
    juce::AudioBuffer<float> inputScratch;
    crowdmike::RoutingMatrix routing;
    crowdmike::SafetyLimiter limiter;
};
