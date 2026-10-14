#pragma once
// Building blocks of the Live page (GUI_SPEC section 3): signal-flow ribbon, input strip, group strip, master section.
// All periodic refresh goes through ui::Tickable; meters only consume engine peaks while `live` is true (page shown),
// so a hidden Live page never steals peak values from the page that is on screen.
#include "UI/EditorContext.h"
#include "UI/Meters.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
struct LiveFlag { bool on = false; };

// Text helpers shared by the live components.
void styleLiveLabel(juce::Label&, float height, bool bold = false, juce::Colour = palette().text,
                    juce::Justification = juce::Justification::centredLeft);
juce::String formatDb(float db, int decimals = 1);
// "G1 G3", "direct", "G2 +dir" or "no route" for input i.
juce::String inputDestinationText(CrowdMikeAudioProcessor&, int input);
// Record-arm helpers: a track whose source is the raw input tap.
bool isInputRecordArmed(const ShowModel&, int input);
// Adds (armed) or removes the raw-input track; returns false when no free track slot was available.
bool setInputRecordArmed(ShowModel&, int input, bool armed);
// Risk badge text/level from a 0..1 score (dsp::risk thresholds).
void riskBadgeFor(float score, juce::String& text, StatusBadge::Level& level);

class FlowRibbon final : public juce::Component {
public:
    explicit FlowRibbon(EditorContext&);
    void resized() override;
    void paint(juce::Graphics&) override;
    static constexpr int kStages = 7;
    juce::TextButton& stage(int i) { return *buttons[i]; }
private:
    EditorContext& ctx;
    juce::OwnedArray<juce::TextButton> buttons;
};

class LiveInputStrip final : public juce::Component, private Tickable {
public:
    LiveInputStrip(EditorContext&, int index, const LiveFlag&);
    ~LiveInputStrip() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void tick() override;
    void refreshNames();
    int getIndex() const noexcept { return index; }
    // test hooks
    juce::Label& nameLabel() { return name; }
    juce::TextButton& recArmButton() { return recArm; }
    ParamToggle& muteToggle() { return mute; }
    ParamSlider& trimSlider() { return trim; }
    StatusBadge& riskBadge() { return risk; }
    juce::String destinationText() const { return dest.getText(); }
    juce::String delayText() const { return delay.getText(); }
private:
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    const int index;
    const LiveFlag& live;
    juce::Label name, dest, delay, readout;
    HistoryStrip history;
    LevelMeter meter;
    GainReductionMeter gr;
    ParamSlider trim;
    ParamToggle mute, solo, pol;
    StatusBadge risk;
    juce::TextButton recArm;
};

class LiveGroupStrip final : public juce::Component, private Tickable {
public:
    LiveGroupStrip(EditorContext&, int index, const LiveFlag&);
    ~LiveGroupStrip() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void tick() override;
    void refreshNames();
private:
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    const int index;
    const LiveFlag& live;
    juce::Label name, readout;
    LevelMeter meterL, meterR;
    GainReductionMeter gr;
    ParamSlider trim;
    ParamToggle mute, solo;
};

class LiveMasterSection final : public juce::Component, private Tickable {
public:
    LiveMasterSection(EditorContext&, const LiveFlag&);
    ~LiveMasterSection() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void tick() override;
    void setNumOutputs(int);
    juce::TextButton& masterButton() { return header; }
private:
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    const LiveFlag& live;
    juce::TextButton header { "MASTER" };
    juce::Rectangle<int> intensityArea, corrArea, fxArea, limArea, outArea;
    float intensity = 0.0f, correlation = 1.0f, width = 0.0f, masterLimGr = 0.0f, outLimGr = 0.0f;
    HistoryStrip history;
    LevelMeter masterL, masterR;
    juce::OwnedArray<LevelMeter> fxMeters;
    juce::OwnedArray<juce::Label> fxLabels;
    GainReductionMeter limGr, outGr;
    juce::Label limLabel, outLimLabel;
    juce::OwnedArray<LevelMeter> outMeters;
    juce::OwnedArray<juce::Label> outLabels;
    int numOutputs = 0;
};
}
