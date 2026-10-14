#pragma once
// Feedback view (GUI_SPEC section 11): monitoring-tap selector, live log-frequency spectrum with the detector's stable
// peaks marked, and a ranked candidate list with suggested notch parameters and an operator "Apply notch" button.
// Analysis is advisory only: nothing is ever inserted automatically. The detector is analysed by the processor's
// AnalysisThread, which this view holds active (reference counted) while setActive(true).
#include <array>
#include "DSP/FeedbackDetector.h"
#include "UI/EditorContext.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
class FeedbackView final : public juce::Component, private Tickable {
public:
    static constexpr int kRows = 6;
    static constexpr int kSpectrumBins = 240;

    explicit FeedbackView(EditorContext&);
    ~FeedbackView() override;
    void setActive(bool);                     // starts / stops the analysis worker
    bool isActive() const noexcept { return active; }
    void resized() override;
    void paint(juce::Graphics&) override;
    void tick() override;

    // Tap ids: 0 master, 1..16 inputs, 17..24 groups (CrowdEngine::setFeedbackTap).
    void selectTap(int tapId);
    int getSelectedTap() const noexcept { return tap; }
    // Applies the cached candidate `row` as a notch on the input chosen in that row (operator action).
    bool applyRow(int row);
    int getNumCandidates() const noexcept { return numCandidates; }
    // Tests: inject a candidate list instead of the detector's.
    void setCandidatesForTesting(const dsp::FeedbackCandidate* c, int n);
    juce::ComboBox& tapBox() { return tapSelector; }
    juce::ComboBox& rowTarget(int row) { return rows[static_cast<size_t>(row)].target; }
    juce::TextButton& rowApply(int row) { return rows[static_cast<size_t>(row)].apply; }

private:
    struct Row {
        juce::Label text;
        juce::ComboBox target;
        juce::TextButton apply { "Apply notch" };
    };
    void refreshRows();
    void rebuildTargets();
    juce::Rectangle<int> plotArea() const;

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    bool active = false, frozenForTest = false;
    int tap = 0, numCandidates = 0, tickCount = 0, targetInputs = 0;
    juce::Label title, advisory, tapLabel, listHeader;
    juce::ComboBox tapSelector;
    std::array<Row, kRows> rows;
    std::array<dsp::FeedbackCandidate, dsp::FeedbackDetector::kMaxCandidates> candidates {};
    std::array<float, kSpectrumBins> spectrum {};
    juce::Rectangle<int> spectrumBounds;
};
}
