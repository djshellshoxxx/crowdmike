#pragma once
// Show Report UI (SHOW_REPORT_SPEC section 10): the energy sparkline on the Live page and the Show Report panel on the
// Record page. Both are read-only views of crowdmike::report::ShowReportService plus edits of ShowModel::report.
#include "UI/EditorContext.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
// Last two minutes of crowd level with peak ticks, the latest level and a "PEAKS n" counter. Refreshes only while showing.
class EnergySparkline final : public juce::Component, public juce::SettableTooltipClient, private Tickable {
public:
    static constexpr int kPoints = 120;
    explicit EnergySparkline(CrowdMikeAudioProcessor&);
    void paint(juce::Graphics&) override;
    void tick() override;
    void setActive(bool);               // the owning page calls this from pageShown()/pageHidden()
    juce::String peaksText() const { return "PEAKS " + juce::String(peaks); }
    juce::String levelText() const;
private:
    CrowdMikeAudioProcessor& proc;
    float values[kPoints] {};
    bool active = false;
    int peaks = 0;
    double stamp = -1.0;
    float level = -120.0f;
    std::vector<float> peakFractions;   // 0..1 across the window
};

// Open folder / Export now / auto-export / auto markers / peak sensitivity / status.
class ShowReportPanel final : public juce::Component, private Tickable, private EditorContext::Listener {
public:
    explicit ShowReportPanel(EditorContext&);
    ~ShowReportPanel() override;
    void resized() override;
    void tick() override;
    void refreshFromModel();
    void setActive(bool);               // the owning page calls this from pageShown()/pageHidden()
    juce::String statusText() const { return status.getText(); }
    static constexpr int kPreferredHeight = 24 + 8 + 26 * 2 + 6;   // section title + padding + two rows
private:
    void showModelChanged() override { refreshFromModel(); }
    void exportNow();
    void openFolder();

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::TextButton openButton, exportButton, autoExport, autoMarkers;
    juce::Label sensLabel, status;
    juce::Slider sensitivity;
    bool active = false;
    juce::String lastStatus;
};
}
