#include "FeedbackView.h"
#include "UI/LiveStrips.h"
#include <cmath>

namespace crowdmike::ui {
namespace {
constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kTopDb = 0.0f, kBottomDb = -100.0f;
float hzToProp(float hz) { return std::log(juce::jlimit(kMinHz, kMaxHz, hz) / kMinHz) / std::log(kMaxHz / kMinHz); }
juce::String hzText(float hz) { return hz >= 1000.0f ? juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz"; }
const char* severityWord(float s) { return s >= 0.66f ? "HIGH" : (s >= 0.33f ? "MED" : "LOW"); }
juce::String tapName(const CrowdMikeAudioProcessor& p, int tap) {
    auto& show = const_cast<CrowdMikeAudioProcessor&>(p).getShow();
    if (tap <= 0) return "Master";
    if (tap <= kMaxInputs) return show.inputDisplayName(tap - 1);
    return show.groupDisplayName(tap - 1 - kMaxInputs);
}
}

FeedbackView::FeedbackView(EditorContext& c) : ctx(c), proc(c.proc) {
    styleLiveLabel(title, 15.0f, true, palette().accent);
    title.setText("FEEDBACK VIEW", juce::dontSendNotification);
    styleLiveLabel(advisory, 12.0f, false, palette().warn);
    advisory.setText(juce::String::fromUTF8("\xe2\x9a\xa0 Analysis only - advisory. Notches are applied only when you press Apply notch."), juce::dontSendNotification);
    styleLiveLabel(tapLabel, 12.0f, false, palette().textMuted, juce::Justification::centredRight);
    tapLabel.setText("Monitoring tap", juce::dontSendNotification);
    styleLiveLabel(listHeader, 11.5f, true, palette().textMuted);
    listHeader.setText("#  FREQUENCY   SEVERITY      PERSIST   SUGGESTED NOTCH", juce::dontSendNotification);
    listHeader.setFont(monoFont(11.5f));
    tapSelector.setTitle("Feedback monitoring tap");
    tapSelector.setTooltip("Signal analysed by the feedback detector (master bus, a processed input or a group)");
    tapSelector.onChange = [this] { const int id = tapSelector.getSelectedId(); if (id > 0) selectTap(id - 1); };
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &title, &advisory, &tapLabel, &tapSelector, &listHeader })
        addAndMakeVisible(comp);
    for (int r = 0; r < kRows; ++r) {
        auto& row = rows[static_cast<size_t>(r)];
        styleLiveLabel(row.text, 12.0f);
        row.text.setFont(monoFont(12.0f));
        row.target.setTitle("Notch target input for candidate " + juce::String(r + 1));
        row.target.setTooltip("Input that receives the notch when you press Apply notch");
        row.apply.setTooltip("Operator action: set this input's dedicated notch to the suggested frequency / Q / depth (undoable)");
        row.apply.setTitle("Apply notch for candidate " + juce::String(r + 1));
        row.apply.onClick = [this, r] { applyRow(r); };
        addAndMakeVisible(row.text);
        addAndMakeVisible(row.target);
        addAndMakeVisible(row.apply);
    }
    tap = proc.getEngine().getFeedbackTap();
    rebuildTargets();
    refreshRows();
}

FeedbackView::~FeedbackView() { setActive(false); }

void FeedbackView::setActive(bool on) {
    if (on == active) return;
    active = on;
    proc.setFeedbackAnalysisActive(on);
    if (on) { rebuildTargets(); tap = proc.getEngine().getFeedbackTap(); tapSelector.setSelectedId(tap + 1, juce::dontSendNotification); }
}

void FeedbackView::rebuildTargets() {
    const int ins = juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount());
    auto& show = proc.getShow();
    tapSelector.clear(juce::dontSendNotification);
    tapSelector.addItem("Master bus", 1);
    tapSelector.addSectionHeading("Inputs (processed)");
    for (int i = 0; i < ins; ++i) tapSelector.addItem(juce::String(i + 1) + ": " + show.inputDisplayName(i), 2 + i);
    tapSelector.addSectionHeading("Groups");
    for (int g = 0; g < kMaxGroups; ++g) tapSelector.addItem("G" + juce::String(g + 1) + ": " + show.groupDisplayName(g), 2 + kMaxInputs + g);
    tapSelector.setSelectedId(tap + 1, juce::dontSendNotification);
    targetInputs = ins;
    for (auto& row : rows) {
        const int keep = row.target.getSelectedId();
        row.target.clear(juce::dontSendNotification);
        for (int i = 0; i < ins; ++i) row.target.addItem("In " + juce::String(i + 1) + ": " + show.inputDisplayName(i), i + 1);
        row.target.setSelectedId(keep > 0 && keep <= ins ? keep : ((tap >= 1 && tap <= ins) ? tap : 1), juce::dontSendNotification);
    }
}

void FeedbackView::selectTap(int id) {
    tap = juce::jlimit(0, kMaxInputs + kMaxGroups, id);
    proc.setFeedbackTap(tap);
    proc.getEngine().feedback().reset();   // candidates of the previous tap are meaningless now
    tapSelector.setSelectedId(tap + 1, juce::dontSendNotification);
    if (tap >= 1 && tap <= kMaxInputs)
        for (auto& row : rows) row.target.setSelectedId(tap, juce::dontSendNotification);
    if (!frozenForTest) numCandidates = 0;
    refreshRows();
    repaint();
}

void FeedbackView::setCandidatesForTesting(const dsp::FeedbackCandidate* c, int n) {
    frozenForTest = true;
    numCandidates = juce::jlimit(0, static_cast<int>(candidates.size()), n);
    for (int k = 0; k < numCandidates; ++k) candidates[static_cast<size_t>(k)] = c[k];
    refreshRows();
}

bool FeedbackView::applyRow(int r) {
    if (r < 0 || r >= juce::jmin(kRows, numCandidates)) return false;
    const auto& c = candidates[static_cast<size_t>(r)];
    const int input = rows[static_cast<size_t>(r)].target.getSelectedId() - 1;
    if (input < 0 || input >= proc.getActiveInputChannelCount()) { ctx.say("Choose an input for the notch first", true); return false; }
    const float hz = juce::jlimit(100.0f, 10000.0f, c.frequencyHz);
    const float q = juce::jlimit(2.0f, 60.0f, c.suggestedQ);
    const float depth = juce::jlimit(-24.0f, 0.0f, c.suggestedDepthDb);
    proc.applyNotchSuggestion(input, hz, q, depth);
    ctx.say("Notch " + hzText(hz) + " Q" + juce::String(q, 1) + " " + juce::String(depth, 1) + " dB applied to "
            + proc.getShow().inputDisplayName(input) + " (Ctrl+Z to undo)");
    return true;
}

void FeedbackView::refreshRows() {
    for (int r = 0; r < kRows; ++r) {
        auto& row = rows[static_cast<size_t>(r)];
        const bool has = r < numCandidates;
        row.apply.setVisible(has);
        row.target.setVisible(has);
        if (!has) { row.text.setText(r == 0 ? "-  no stable peaks detected on " + tapName(proc, tap) : juce::String(), juce::dontSendNotification); continue; }
        const auto& c = candidates[static_cast<size_t>(r)];
        juce::String s;
        s << juce::String(r + 1) << "  " << hzText(c.frequencyHz).paddedRight(' ', 10) << "  "
          << (juce::String(juce::roundToInt(c.severity * 100.0f)) + "% " + severityWord(c.severity)).paddedRight(' ', 12) << "  "
          << (juce::String(c.persistenceSec, 1) + " s").paddedRight(' ', 8) << "  "
          << juce::String(juce::roundToInt(c.frequencyHz)) << " Hz Q" << juce::String(c.suggestedQ, 0) << " " << juce::String(c.suggestedDepthDb, 1) << " dB";
        row.text.setText(s, juce::dontSendNotification);
        row.text.setColour(juce::Label::textColourId, c.severity >= 0.66f ? palette().danger : (c.severity >= 0.33f ? palette().warn : palette().text));
    }
}

void FeedbackView::tick() {
    if (!active) return;
    if (targetInputs != juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount())) rebuildTargets();
    auto& det = proc.getEngine().feedback();
    det.getSpectrumDb(spectrum.data(), kSpectrumBins);
    if (!frozenForTest && ++tickCount % 6 == 0) {     // list at ~5 Hz so it stays readable
        numCandidates = det.getCandidates(candidates.data(), static_cast<int>(candidates.size()));
        refreshRows();
    }
    repaint(spectrumBounds);
}

juce::Rectangle<int> FeedbackView::plotArea() const { return spectrumBounds.reduced(34, 6).withTrimmedBottom(12); }

void FeedbackView::paint(juce::Graphics& g) {
    const auto& p = palette();
    g.setColour(p.panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 5.0f);
    g.setColour(p.background.darker(0.2f));
    g.fillRect(spectrumBounds);
    const auto plot = plotArea().toFloat();
    if (plot.getWidth() < 20 || plot.getHeight() < 20) return;
    g.setFont(font(10.0f));
    for (float db = kBottomDb; db <= kTopDb; db += 20.0f) {
        const float y = plot.getBottom() - (db - kBottomDb) / (kTopDb - kBottomDb) * plot.getHeight();
        g.setColour(p.panelEdge);
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        g.setColour(p.textMuted);
        g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(plot.getX() - 32.0f, y - 6.0f, 28.0f, 12.0f), juce::Justification::centredRight);
    }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) {
        const float x = plot.getX() + hzToProp(hz) * plot.getWidth();
        g.setColour(p.panelEdge);
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        g.setColour(p.textMuted);
        g.drawText(hz >= 1000.0f ? juce::String(juce::roundToInt(hz / 1000.0f)) + "k" : juce::String(juce::roundToInt(hz)),
                   juce::Rectangle<float>(x - 16.0f, plot.getBottom() + 1.0f, 32.0f, 11.0f), juce::Justification::centred);
    }
    juce::Path path;
    for (int k = 0; k < kSpectrumBins; ++k) {
        const float x = plot.getX() + plot.getWidth() * static_cast<float>(k) / (kSpectrumBins - 1);
        const float db = juce::jlimit(kBottomDb, kTopDb, spectrum[static_cast<size_t>(k)]);
        const float y = plot.getBottom() - (db - kBottomDb) / (kTopDb - kBottomDb) * plot.getHeight();
        if (k == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    juce::Path fill(path);
    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.lineTo(plot.getX(), plot.getBottom());
    fill.closeSubPath();
    g.setColour(p.accentDim.withAlpha(0.35f));
    g.fillPath(fill);
    g.setColour(p.accent);
    g.strokePath(path, juce::PathStrokeType(1.5f));
    // Stable peaks: numbered markers (number + line, not colour alone).
    for (int k = 0; k < numCandidates; ++k) {
        const auto& c = candidates[static_cast<size_t>(k)];
        const float x = plot.getX() + hzToProp(c.frequencyHz) * plot.getWidth();
        const auto col = c.severity >= 0.66f ? p.danger : (c.severity >= 0.33f ? p.warn : p.text);
        g.setColour(col);
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine({ x, plot.getY() + 14.0f, x, plot.getBottom() }, dash, 2, 1.5f);
        g.setFont(font(11.0f, true));
        g.drawText(juce::String(k + 1), juce::Rectangle<float>(x - 10.0f, plot.getY(), 20.0f, 13.0f), juce::Justification::centred);
    }
    g.setColour(p.textMuted);
    g.setFont(font(11.0f));
    g.drawText("Spectrum: " + tapName(proc, tap) + (active ? "" : "  (paused)"), plot.toNearestInt().reduced(4, 2), juce::Justification::topRight);
}

void FeedbackView::resized() {
    auto r = getLocalBounds().reduced(8);
    const int row = juce::jlimit(20, 30, r.getHeight() / 22);
    auto top = r.removeFromTop(row + 2);
    tapSelector.setBounds(top.removeFromRight(juce::jmin(240, top.getWidth() / 3)));
    tapLabel.setBounds(top.removeFromRight(110));
    title.setBounds(top);
    advisory.setBounds(r.removeFromTop(row));
    r.removeFromTop(4);
    auto list = r.removeFromBottom(row + kRows * (row + 2) + 4);
    spectrumBounds = r.reduced(0, 2);
    listHeader.setBounds(list.removeFromTop(row));
    for (auto& rw : rows) {
        auto line = list.removeFromTop(row + 2).reduced(0, 1);
        rw.apply.setBounds(line.removeFromRight(juce::jmin(110, line.getWidth() / 6)));
        line.removeFromRight(4);
        rw.target.setBounds(line.removeFromRight(juce::jmin(170, line.getWidth() / 4)));
        line.removeFromRight(4);
        rw.text.setBounds(line);
    }
}
}
