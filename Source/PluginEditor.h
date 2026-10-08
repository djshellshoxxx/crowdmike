#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class CrowdMikeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit CrowdMikeAudioProcessorEditor(CrowdMikeAudioProcessor&);
    ~CrowdMikeAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class InputControls;
    void timerCallback() override;

    CrowdMikeAudioProcessor& processor;
    juce::Label title;
    juce::Label limiterLabel;
    juce::Slider limiterSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> limiterAttachment;
    juce::OwnedArray<InputControls> inputControls;
};
