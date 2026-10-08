#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
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
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
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
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    float getAndResetInputPeak(int inputIndex) noexcept;

private:
    struct InputParameterPointers {
        std::atomic<float>* trimDb = nullptr;
        std::atomic<float>* muted = nullptr;
        std::atomic<float>* polarityInverted = nullptr;
        std::atomic<float>* highPassEnabled = nullptr;
        std::atomic<float>* highPassHz = nullptr;
        std::atomic<float>* lowPassEnabled = nullptr;
        std::atomic<float>* lowPassHz = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::String inputParameterId(int inputIndex, const juce::String& suffix);

    juce::AudioProcessorValueTreeState parameters;
    std::array<InputParameterPointers, crowdmike::RoutingMatrix::maxChannels> inputParameters {};
    std::atomic<float>* limiterCeilingDb = nullptr;
    std::array<crowdmike::InputStrip, crowdmike::RoutingMatrix::maxChannels> inputs;
    int preparedInputCount = 0;
    juce::AudioBuffer<float> inputScratch;
    crowdmike::RoutingMatrix routing;
    crowdmike::SafetyLimiter limiter;
};
