#pragma once
// Line Check panel (docs/LINE_CHECK_SPEC.md section 6): a non-modal section of the Inputs page. Arms the engine's per-input
// probe for 10 s, shows plain-language findings per input and applies proposals (all or per input) as undoable edits.
#include <memory>
#include <vector>
#include "Engine/LineCheck.h"
#include "Engine/LineCheckApply.h"
#include "UI/EditKit.h"
#include "UI/EditorContext.h"
#include "UI/Meters.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
class LineCheckPanel final : public kit::Card, private Tickable {
public:
    enum class State { Idle, Listening, Results };

    explicit LineCheckPanel(EditorContext&);
    ~LineCheckPanel() override;
    void resized() override;
    void tick() override;

    // Wizard actions (also bound to the buttons; public so tests and shortcuts can drive them).
    void startListening();
    void stopListening(bool analyse);                 // analyse = false cancels
    bool applyAll();                                  // trims + HPF suggestions of all inputs; one undo step. false = nothing to do
    bool applyInput(int input, bool includePolarity = false);
    bool undoLastApply();
    void quickStart();                                // wiring template for the channel count + HPF defaults
    void setActive(bool);                             // page visible: probe is disarmed when hidden

    State state() const noexcept { return currentState; }
    const std::vector<linecheck::Result>& results() const noexcept { return lastResults; }
    float progress() const noexcept { return collector.progress(); }
    void setTargetRmsDb(float db) { settings.targetRmsDb = juce::jlimit(-30.0f, -10.0f, db); }
    float getTargetRmsDb() const noexcept { return settings.targetRmsDb; }
    static int preferredHeight(int rows) noexcept;
    // test hooks
    juce::TextButton& startButton() { return startBtn; }
    juce::TextButton& applyAllButton() { return applyAllBtn; }
    juce::TextButton& undoButton() { return undoBtn; }
    juce::TextButton& quickButton() { return quickBtn; }
    juce::String statusText() const { return status.getText(); }
    int numRows() const noexcept { return rows.size(); }

private:
    struct Row;
    void analyseNow();
    void rebuildRows();
    void refreshButtons();
    linecheck::ApplyOptions optionsFor(bool polarity) const;
    std::vector<linecheck::InputContext> contexts() const;

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    State currentState = State::Idle;
    bool active = false, applied = false;
    linecheck::Collector collector;
    linecheck::Settings settings;
    std::vector<linecheck::Result> lastResults;
    int listeningTicks = 0;
    double progressValue = 0.0;

    juce::TextButton startBtn { "Start 10 s listen" }, stopBtn { "Stop" }, applyAllBtn { "Apply all" }, undoBtn { "Undo" }, quickBtn { "Quick start..." };
    juce::Label status, targetLabel;
    juce::Slider target { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ProgressBar bar { progressValue };
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<Row> rows;
};
}
