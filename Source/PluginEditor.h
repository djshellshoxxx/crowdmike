#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class CrowdMikeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit CrowdMikeAudioProcessorEditor(CrowdMikeAudioProcessor&);
    ~CrowdMikeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class InputControls;
    class RoutingPanel;
    void showRoutingPage(bool);
    void timerCallback() override;

    CrowdMikeAudioProcessor& processor;
    juce::Label title;
    juce::Label limiterLabel;
    juce::Slider limiterSlider;
    juce::TextButton liveButton { "LIVE" }, routingButton { "MATRIX" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> limiterAttachment;
    juce::OwnedArray<InputControls> inputControls;
    std::unique_ptr<RoutingPanel> routingPanel;
    bool routingPageVisible = false;
};
