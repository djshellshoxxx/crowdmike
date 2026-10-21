#include "UI/ReportPanels.h"
#include <algorithm>
#include <cmath>
#include "UI/Meters.h"
#include "UI/PageKit.h"

namespace crowdmike::ui {
// ============================================================================================ EnergySparkline
EnergySparkline::EnergySparkline(CrowdMikeAudioProcessor& p) : proc(p) {
    setComponentID("live.energy");
    setTitle("Crowd energy");
    setTooltip("Crowd level over the last two minutes. Triangles mark detected crowd peaks.");
    for (auto& v : values) v = -120.0f;
}

juce::String EnergySparkline::levelText() const {
    return level <= -119.0f ? juce::String("-inf dB") : juce::String(level, 1) + " dB";
}

void EnergySparkline::setActive(bool a) {
    active = a;
    if (a) { stamp = -2.0; tick(); }
}

void EnergySparkline::tick() {
    if (!active) return;
    const auto& svc = proc.getReport();
    const auto& trk = svc.tracker();
    const double now = trk.empty() ? -1.0 : trk.lastTime();
    const int n = svc.getPeakCount();
    if (now == stamp && n == peaks) return;
    stamp = now;
    peaks = n;
    svc.getRecentLevels(values, kPoints);
    level = svc.getCurrentCrowdDb();
    peakFractions.clear();
    if (!trk.empty())
        for (const auto& pk : trk.peaks()) {
            const double age = now - pk.t;
            if (age >= 0.0 && age <= kPoints - 1) peakFractions.push_back(static_cast<float>(1.0 - age / (kPoints - 1)));
        }
    repaint();
}

void EnergySparkline::paint(juce::Graphics& g) {
    const auto& p = palette();
    const auto r = getLocalBounds().toFloat();
    g.setColour(p.panel);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(p.panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);

    auto inner = r.reduced(6.0f, 3.0f);
    const float textW = juce::jmin(112.0f, inner.getWidth() * 0.45f);
    auto textArea = inner.removeFromRight(textW);
    inner.removeFromRight(6.0f);

    // line: -72..-6 dBFS mapped onto the height
    const float lo = -72.0f, hi = -6.0f;
    juce::Path line;
    for (int i = 0; i < kPoints; ++i) {
        const float x = inner.getX() + inner.getWidth() * static_cast<float>(i) / (kPoints - 1);
        const float norm = juce::jlimit(0.0f, 1.0f, (values[i] - lo) / (hi - lo));
        const float y = inner.getBottom() - norm * inner.getHeight();
        if (i == 0) line.startNewSubPath(x, y); else line.lineTo(x, y);
    }
    g.setColour(p.accent);
    g.strokePath(line, juce::PathStrokeType(1.4f));
    g.setColour(p.warn);
    for (const float f : peakFractions) {
        const float x = inner.getX() + inner.getWidth() * f;
        juce::Path tri;
        tri.addTriangle(x - 3.5f, inner.getY(), x + 3.5f, inner.getY(), x, inner.getY() + 6.0f);
        g.fillPath(tri);
    }
    g.setColour(p.text);
    g.setFont(font(12.0f, true));
    g.drawText(peaksText(), textArea.removeFromTop(textArea.getHeight() * 0.5f), juce::Justification::centredRight, true);
    g.setColour(p.textMuted);
    g.setFont(monoFont(11.5f));
    g.drawText(levelText(), textArea, juce::Justification::centredRight, true);
}

// ============================================================================================ ShowReportPanel
ShowReportPanel::ShowReportPanel(EditorContext& c) : ctx(c), proc(c.proc) {
    ctx.addListener(this);
    openButton.setButtonText("Open folder");
    kit::setupButton(openButton, "Open the folder of the last Show Report (or the record folder)", "report.openFolder");
    openButton.onClick = [this] { openFolder(); };
    exportButton.setButtonText("Export now");
    kit::setupButton(exportButton, "Write show_report.html / .csv / .json now (current or last take, or the whole show so far)", "report.exportNow");
    exportButton.onClick = [this] { exportNow(); };
    kit::setupToggle(autoExport, "Auto-export", "Write the report automatically when recording stops and when the app exits", palette().accentDim);
    autoExport.setComponentID("report.autoExport");
    autoExport.onClick = [this] {
        proc.getShow().report.autoExport = autoExport.getToggleState();
        kit::refreshToggleText(autoExport, "Auto-export");
        ctx.showModelChanged();
    };
    kit::setupToggle(autoMarkers, "Auto markers", "Drop 'Crowd peak' recorder markers on applause peaks while recording", palette().accentDim);
    autoMarkers.setComponentID("report.autoMarkers");
    autoMarkers.onClick = [this] {
        proc.getShow().report.autoMarkers = autoMarkers.getToggleState();
        kit::refreshToggleText(autoMarkers, "Auto markers");
        ctx.showModelChanged();
    };
    kit::setupToggle(tracking, "Tracking", "Track crowd energy (peaks, averages, report data). Off stops sampling and auto markers.", palette().accentDim);
    tracking.setComponentID("report.tracking");
    tracking.onClick = [this] {
        proc.getShow().report.enabled = tracking.getToggleState();
        kit::refreshToggleText(tracking, "Tracking");
        ctx.showModelChanged();
    };
    kit::styleLabel(sensLabel, 12.0f, false, true);
    sensLabel.setText("Peak sensitivity", juce::dontSendNotification);
    sensitivity.setSliderStyle(juce::Slider::LinearBar);
    sensitivity.setRange(0.0, 100.0, 1.0);
    sensitivity.setTextValueSuffix(" %");
    sensitivity.setTitle("Peak sensitivity");
    sensitivity.setTooltip("Higher finds more (and smaller) crowd peaks and allows them closer together");
    sensitivity.setComponentID("report.sensitivity");
    sensitivity.setWantsKeyboardFocus(true);
    sensitivity.onValueChange = [this] {
        proc.getShow().report.peakSensitivity = static_cast<float>(sensitivity.getValue() / 100.0);
        ctx.showModelChanged();
    };
    kit::styleLabel(status, 12.0f, false, true);
    status.setComponentID("report.status");
    for (auto* x : std::initializer_list<juce::Component*> { &openButton, &exportButton, &autoExport, &autoMarkers, &tracking, &sensLabel, &sensitivity, &status })
        addAndMakeVisible(x);
    refreshFromModel();
}

ShowReportPanel::~ShowReportPanel() { ctx.removeListener(this); }

void ShowReportPanel::refreshFromModel() {
    const auto& r = proc.getShow().report;
    autoExport.setToggleState(r.autoExport, juce::dontSendNotification);
    kit::refreshToggleText(autoExport, "Auto-export");
    autoMarkers.setToggleState(r.autoMarkers, juce::dontSendNotification);
    kit::refreshToggleText(autoMarkers, "Auto markers");
    tracking.setToggleState(r.enabled, juce::dontSendNotification);
    kit::refreshToggleText(tracking, "Tracking");
    sensitivity.setValue(static_cast<double>(r.peakSensitivity) * 100.0, juce::dontSendNotification);
}

void ShowReportPanel::resized() {
    auto r = getLocalBounds();
    auto row1 = r.removeFromTop(kit::kRowH);
    r.removeFromTop(6);
    auto row2 = r.removeFromTop(kit::kRowH);
    kit::layoutRow(row1, { { &openButton, 1.0f }, { &exportButton, 1.0f }, { &autoExport, 1.15f }, { &autoMarkers, 1.15f }, { &tracking, 1.0f } });
    kit::layoutRow(row2, { { &sensLabel, -104.0f }, { &sensitivity, 1.0f }, { &status, 1.7f } });
}

void ShowReportPanel::exportNow() {
    const auto res = proc.getReport().exportReport();
    if (res.ok) ctx.say("Show report saved: " + res.folder.getFullPathName());
    else ctx.say("Show report: " + res.error, true);
    lastStatus = {};
    tick();
}

void ShowReportPanel::openFolder() {
    const auto folder = proc.getReport().getReportFolderForUi();
    if (!folder.isDirectory()) { ctx.say("Report folder does not exist yet: export a report first", true); return; }
    if (!folder.startAsProcess()) ctx.say("Cannot open " + folder.getFullPathName(), true);
}

void ShowReportPanel::setActive(bool a) {
    active = a;
    if (a) { refreshFromModel(); tick(); }
}

void ShowReportPanel::tick() {
    if (!active) return;
    const auto& svc = proc.getReport();
    juce::String text;
    if (!proc.getShow().report.enabled) text = "Tracking off";
    else if (svc.tracker().empty()) text = "Waiting for audio...";
    else {
        text = (svc.hasOpenTake() ? "Take: " : "Tracking ") + kit::formatClock(svc.getTrackedSeconds()) + ", " + juce::String(svc.getPeakCount()) + " peaks";
        if (svc.getLastExportMessage().isNotEmpty()) text = svc.getLastExportMessage().startsWith("Show report saved") ? text + ", report saved" : text + ", export failed";
    }
    if (text != lastStatus) {
        lastStatus = text;
        status.setText(text, juce::dontSendNotification);
        status.setTooltip(svc.getLastExportMessage());
    }
}
}
