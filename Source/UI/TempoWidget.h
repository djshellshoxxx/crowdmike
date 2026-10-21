#pragma once
// Compact tempo widget for the status bar (docs/BEAT_SYNC_SPEC.md section 10): source selector, BPM read-out, TAP button,
// beat LED (accent on the downbeat), confidence bar, trigger-quantise selector. Reads TempoHub::snapshot() from the
// shared 30 Hz ticker; never touches the audio thread.
#include "UI/EditorContext.h"
#include "UI/Ticker.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
class TempoWidget final : public juce::Component, private Tickable {
public:
    static constexpr int kPreferredWidth = 400;
    explicit TempoWidget(EditorContext&);
    void resized() override;
    void paint(juce::Graphics&) override;
    void tick() override;

    // test hooks
    juce::String getBpmText() const { return bpmLabel.getText(); }
    bool isBeatLedOn() const noexcept { return ledOn; }
    bool isDownbeatLed() const noexcept { return ledDownbeat; }
    float getConfidenceShown() const noexcept { return confShown; }
    bool isLowConfidence() const noexcept { return lowConf; }

private:
    CrowdMikeAudioProcessor& proc;
    ParamChoice source, quantise;
    juce::TextButton tapBtn;   // taps go straight to TempoHub on mouse-down (the tempo.tap parameter is polled at 20 Hz: +-50 ms jitter, short clicks lost)
    bool tapWasDown = false;
    juce::Label bpmLabel;
    juce::Rectangle<int> ledArea, confArea;
    bool ledOn = false, ledDownbeat = false, lowConf = false;
    float confShown = 0.0f;
};
}
