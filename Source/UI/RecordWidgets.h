#pragma once
// Record page building blocks: recorder source names (TapMap + ShowModel names), one track row, rolling waveform.
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "PluginProcessor.h"
#include "UI/Meters.h"

namespace crowdmike::ui {
// Display name of a recorder tap, using the operator's port names ("Raw: Front L", "Group: Rear Crowd", ...).
juce::String tapDisplayName(const ShowModel&, int tapId);
// Fills a ComboBox with every tap (item id = tapId + 2, "None" = 1), grouped by section headings.
void fillTapCombo(juce::ComboBox&, const ShowModel&, int activeInputs, int activeOutputs);

class RecordTrackRow final : public juce::Component {
public:
    RecordTrackRow(CrowdMikeAudioProcessor&, int track);
    void refreshFromModel(int activeInputs, int activeOutputs);    // rebuild the source list + states
    void updateStates();                                             // arm/mute/monitor/channels text from the model
    void setSelected(bool);
    void setLevel(float peak) { meter.feed(peak); }
    void setTrackStatus(bool error, int dropouts);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { if (onSelect) onSelect(track); }

    std::function<void(int)> onSelect;
    std::function<void()> onEdited;       // model changed -> apply
    juce::ComboBox source;
    juce::TextButton arm, mute, monitor, select;
    juce::TextEditor name;

private:
    RecTrackSetting& setting();
    CrowdMikeAudioProcessor& proc;
    int track;
    bool selected = false;
    juce::Label channels, status;
    LevelMeter meter { LevelMeter::Orientation::Horizontal };
};

// Rolling peak history of one recorder track (fed by the page from getTrackPeak; ring buffer, newest on the right).
class RollingWaveform final : public juce::Component {
public:
    static constexpr int kPoints = 480;
    void push(float peak);
    void clear() { history.fill(0.0f); writePos = 0; repaint(); }
    void setCaption(const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paint(juce::Graphics&) override;
    float latest() const noexcept { return history[static_cast<size_t>((writePos + kPoints - 1) % kPoints)]; }
private:
    std::array<float, kPoints> history {};
    int writePos = 0;
    juce::String caption;
};
}
