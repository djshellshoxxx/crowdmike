#pragma once
// Performer ducker strip for the Live page (docs/DUCKER_SPEC.md section 6): on/off, key input, threshold, depth, attack, hold,
// release, target, exclude-key, a key-level bar with the threshold marker and a gain-reduction bar. Every control is a
// parameter control, so learn/macro assignment and undo work as everywhere else.
#include "UI/EditKit.h"
#include "UI/EditorContext.h"
#include "UI/LiveStrips.h"
#include "UI/Meters.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
class DuckerStrip final : public juce::Component, private Tickable {
public:
    DuckerStrip(EditorContext&, const LiveFlag&);
    ~DuckerStrip() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void tick() override;
    static int preferredHeight() noexcept;
    // test hooks
    ParamToggle& onToggle() { return on; }
    ParamChoice& keyChoice() { return key; }
    ParamChoice& targetChoice() { return target; }
    GainReductionMeter& grMeter() { return gr; }
    LevelMeter& keyMeter() { return keyLevel; }
    juce::String statusText() const { return status.getText(); }
private:
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    const LiveFlag& live;
    ParamToggle on, exclude;
    ParamChoice key, target;
    kit::Captioned thr, depth, attack, hold, release;
    LevelMeter keyLevel { LevelMeter::Orientation::Horizontal };
    GainReductionMeter gr { 30.0f };
    juce::Label status;
    float thresholdDb = -35.0f;
};
}
