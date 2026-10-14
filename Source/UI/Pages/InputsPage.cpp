// Inputs page (GUI_SPEC section 4): one row per active input with naming, source, link, trim, polarity, mute/solo,
// HPF/LPF, notch, alignment delay (ms / samples / metres), de-esser and compressor summaries, group assignment cells,
// record arm and a clip-latching meter. An expandable advanced section shows the selected input's filter response,
// live spectrum, waveform history, pair correlation, compressor transfer curve and feedback candidates.
#include "UI/Pages.h"
#include "Core/PresetManager.h"
#include "Engine/TapMap.h"
#include "UI/EditKit.h"
#include "UI/InputAnalysis.h"
#include "UI/MatrixGrid.h"
#include "UI/Meters.h"
#include "UI/RoutingUndo.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
using namespace crowdmike::params;

namespace {
int pidx(int i, int e) { return ParamBank::index(Mod::Input, i, e); }

int findInputTrack(const ShowModel& show, int i) {
    for (int t = 0; t < kMaxRecordTracks; ++t)
        if (show.recorder.tracks[static_cast<size_t>(t)].sourceId == TapMap::forRawInput(i)) return t;
    return -1;
}
// Adds/removes a raw-input recorder track. Returns false when no free track slot is left.
bool setInputArmed(ShowModel& show, int i, bool arm) {
    const int existing = findInputTrack(show, i);
    if (!arm) {
        if (existing >= 0) show.recorder.tracks[static_cast<size_t>(existing)] = RecTrackSetting {};
        return true;
    }
    if (existing >= 0) { show.recorder.tracks[static_cast<size_t>(existing)].armed = true; return true; }
    for (auto& tr : show.recorder.tracks)
        if (tr.sourceId < 0) {
            tr = RecTrackSetting {};
            tr.sourceId = TapMap::forRawInput(i);
            tr.numChannels = 1;
            tr.armed = true;
            tr.name = show.inputDisplayName(i) + " (raw)";
            return true;
        }
    return false;
}

std::unique_ptr<juce::Component> slider(CrowdMikeAudioProcessor& p, int idx, int textW = 56) {
    return std::make_unique<ParamSlider>(p, idx, juce::Slider::LinearHorizontal, textW);
}

// ================================================================= one input row
class InputRow final : public kit::Card {
public:
    InputRow(EditorContext& c, int input) : ctx(c), proc(c.proc), i(input),
        link(proc, pidx(i, InputP::Link), juce::String::fromUTF8("LINK \xe2\x96\xb8")),
        pol(proc, pidx(i, InputP::Pol), juce::String::fromUTF8("\xc3\x98")),
        mute(proc, pidx(i, InputP::Mute), "M"), solo(proc, pidx(i, InputP::Solo), "S"),
        hpOn(proc, pidx(i, InputP::HpOn), "HPF"), lpOn(proc, pidx(i, InputP::LpOn), "LPF"),
        notchOn(proc, pidx(i, InputP::NotchOn), "NOTCH"),
        hpSlope(proc, pidx(i, InputP::HpSlope)), lpSlope(proc, pidx(i, InputP::LpSlope)),
        dsOn(proc, pidx(i, InputP::DeEssOn), "DS"), compOn(proc, pidx(i, InputP::InCompOn), "COMP"),
        trim("Trim", slider(proc, pidx(i, InputP::Trim), 58), 30),
        hpHz("Hz", slider(proc, pidx(i, InputP::HpHz), 52), 18), lpHz("Hz", slider(proc, pidx(i, InputP::LpHz), 52), 18),
        notchHz("Hz", slider(proc, pidx(i, InputP::NotchHz), 52), 18), notchQ("Q", slider(proc, pidx(i, InputP::NotchQ), 40), 14),
        notchDepth("Dp", slider(proc, pidx(i, InputP::NotchDepth), 50), 18),
        align("Align", slider(proc, pidx(i, InputP::AlignMs), 70), 34),
        grMeter(18.0f) {
        setComponentID("input-row-" + juce::String(i));
        setTitle("Input " + juce::String(i + 1));
        meter.source = [this] {
            if (!live) return 0.0f;
            auto& m = proc.getMeters();
            float pk = m.inPeak[static_cast<size_t>(i)].getAndReset();
            if (m.inClip[static_cast<size_t>(i)].exchange(false)) pk = juce::jmax(pk, 1.0f);
            return pk;
        };
        meter.setTooltip("Input level (peak, hold). A red bar at the top = clip latched: click the meter to clear.");
        grMeter.source = [this] { return live ? std::abs(proc.getMeters().inGrDb[static_cast<size_t>(i)].load()) : 0.0f; };
        addAndMakeVisible(meter);

        kit::styleLabel(indexLabel, 13.0f, true);
        indexLabel.setInterceptsMouseClicks(false, false);
        kit::styleLabel(nameLabel, 13.0f, true);
        nameLabel.setEditable(false, true, false);
        nameLabel.setColour(juce::Label::backgroundColourId, palette().panelRaised);
        nameLabel.setTooltip("Channel name: double-click to rename (stable ID " + ShowModel::inputId(i) + ")");
        nameLabel.setTitle("Input name");
        nameLabel.onTextChange = [this] { proc.getShow().input(i).name = nameLabel.getText().trim(); ctx.showModelChanged(); };
        kit::styleLabel(zoneLabel, 12.0f, false, palette().textMuted);
        zoneLabel.setEditable(false, true, false);
        zoneLabel.setColour(juce::Label::backgroundColourId, palette().panelRaised);
        zoneLabel.setTooltip("Location / zone (used by the feedback-risk estimate): double-click to edit");
        zoneLabel.setTitle("Zone");
        zoneLabel.onTextChange = [this] { proc.getShow().input(i).zone = zoneLabel.getText().trim(); ctx.showModelChanged(); };

        sourceBtn.setTooltip("Hardware / host source of this input");
        sourceBtn.onClick = [this] { showSourceMenu(); };
        link.setTooltip("Mono / stereo source mode: link this input with input " + juce::String(i + 2) + " as a stereo pair");
        pol.setTooltip(juce::String::fromUTF8("Polarity \xc3\x98 (exact 180-degree inversion)"));
        mute.setTooltip("Mute input"); solo.setTooltip("Solo input");
        rec.setClickingTogglesState(false);
        rec.setComponentID("input-rec-" + juce::String(i));
        rec.setTooltip("Record arm: adds / removes a raw (pre-processing) recorder track for this input");
        rec.onClick = [this] {
            auto& show = proc.getShow();
            const bool arm = findInputTrack(show, i) < 0;
            if (!setInputArmed(show, i, arm)) { ctx.say("No free recorder track for " + show.inputDisplayName(i), true); return; }
            ctx.showModelChanged();
            refreshRec();
        };

        groups.setComponentID("input-groups-" + juce::String(i));
        groups.setTitle("Group assignment for input " + juce::String(i + 1));
        groups.setShowHeaders(false, false);
        groups.setCellSize(24, 26);
        groups.numRows = [] { return 1; };
        groups.numCols = [] { return kMaxGroups; };
        groups.rowName = [this](int) { return proc.getShow().inputDisplayName(i); };
        groups.colName = [this](int g) { return proc.getShow().groupDisplayName(g); };
        groups.cellLabel = [](int, int g) { return juce::String(g + 1); };
        groups.getGain = [this](int, int g) { return proc.getRouting().getAssign(i, g); };
        groups.setLive = [this](int, int g, float v) { proc.getRouting().setAssign(i, g, v); };
        groups.commit = [this](int, int g, float o, float n) { performRouteEdit(proc.getUndoManager(), proc.getRouting(), RouteMatrix::Assign, i, g, o, n, "Group assignment"); };
        groups.maxGain = 2.0f;

        dsSummary.setTooltip("De-esser: frequency and live gain reduction (edit in the inspector)");
        compSummary.setTooltip("Compressor: threshold / ratio and live gain reduction (edit in the inspector)");
        for (auto* l : { &dsSummary, &compSummary }) { kit::styleLabel(*l, 11.5f, false, palette().text); l->setInterceptsMouseClicks(false, false); }
        grMeter.setTitle("Compressor gain reduction");
        presetBtn.setTooltip("Compressor / dynamics presets for this input");
        presetBtn.onClick = [this] {
            kit::PresetTarget t;
            t.module = Mod::Input; t.instance = i; t.factoryTarget = "input"; t.blockPrefixes = { "comp" };
            t.folderName = "input-comp"; t.title = "Compressor presets";
            t.extraSections = { { "Dynamics presets", { "gate", "deess", "trans" } } };
            kit::showPresetMenu(proc, &presetBtn, t);
        };

        hpOn.setTooltip("High-pass filter on/off"); lpOn.setTooltip("Low-pass filter on/off");
        notchOn.setTooltip("Parametric notch on/off (feedback control)");
        dsOn.setTooltip("De-esser on/off"); compOn.setTooltip("Compressor on/off");
        align.setTooltip("Alignment delay (time-aligns microphones; not reinforcement delay). Units follow the page switch.");

        for (juce::Component* comp : std::initializer_list<juce::Component*> { &indexLabel, &nameLabel, &zoneLabel, &sourceBtn, &link, &trim, &pol, &mute, &solo, &rec, &groups,
                                                                             &hpOn, &hpHz, &hpSlope, &lpOn, &lpHz, &lpSlope, &notchOn, &notchHz, &notchQ, &notchDepth, &align,
                                                                             &dsOn, &dsSummary, &compOn, &compSummary, &grMeter, &presetBtn })
            addAndMakeVisible(comp);
        refreshNames();
        refreshRec();
        setAlignUnit(0);
    }

    void setLive(bool l) { live = l; }
    void setSelected(bool s) { selected = s; setHighlighted(s); refreshNames(); }
    void setAlignUnit(int unit) {
        auto* s = align.as<ParamSlider>();
        s->textFromValueFunction = [this, unit](double ms) { return kit::alignToText(ms, unit, proc.getEngine().getSampleRate()); };
        s->valueFromTextFunction = [this, unit](const juce::String& t) { return kit::alignFromText(t, unit, proc.getEngine().getSampleRate()); };
        s->updateText();
    }
    void refreshNames() {
        const auto& show = proc.getShow();
        indexLabel.setText((selected ? juce::String::fromUTF8("\xe2\x96\xb6") : juce::String()) + juce::String(i + 1), juce::dontSendNotification);
        if (!nameLabel.isBeingEdited()) nameLabel.setText(show.inputDisplayName(i), juce::dontSendNotification);
        if (!zoneLabel.isBeingEdited()) zoneLabel.setText(show.input(i).zone.isNotEmpty() ? show.input(i).zone : juce::String("(zone)"), juce::dontSendNotification);
        // linked follower: show it as the right half of the pair
        const bool follower = i > 0 && kit::isOn(proc, pidx(i - 1, InputP::Link));
        link.setEnabled(i + 1 < proc.getActiveInputChannelCount() && !follower);
        link.setButtonText(follower ? juce::String::fromUTF8("R of pair") : juce::String::fromUTF8("LINK \xe2\x96\xb8"));
        refreshSource();
    }
    void refreshSource() {
        juce::String text = "Host ch " + juce::String(i + 1);
        if (auto* hs = proc.getHostServices()) {
            text = "(unassigned)";
            for (const auto& b : hs->getInputBindings())
                if (b.engineChannel == i) text = b.physicalName.isNotEmpty() ? b.physicalName + (b.resolved ? "" : " (missing)") : juce::String("(unassigned)");
        }
        sourceBtn.setButtonText(text);
    }
    void refreshRec() {
        const bool armed = findInputTrack(proc.getShow(), i) >= 0;
        rec.setToggleState(armed, juce::dontSendNotification);
        rec.setButtonText(armed ? juce::String::fromUTF8("\xe2\x97\x8f REC") : juce::String::fromUTF8("\xe2\x97\x8b REC"));
        rec.setColour(juce::TextButton::buttonOnColourId, palette().rec.darker(0.3f));
    }
    void tick() {
        const auto v = [this](int e) { return kit::actual(proc, pidx(i, e)); };
        const float dsGr = std::abs(proc.getEngine().inputChannelForMeters(i).getDeEssGrDb());
        dsSummary.setText(kit::fmtHz(v(InputP::DeEssFreq)) + "Hz" + (v(InputP::DeEssOn) >= 0.5f ? "  GR " + juce::String(dsGr, 1) : juce::String("  off")),
                          juce::dontSendNotification);
        const float gr = std::abs(proc.getMeters().inGrDb[static_cast<size_t>(i)].load());
        compSummary.setText(juce::String(v(InputP::InCompThr), 0) + "dB " + juce::String(v(InputP::InCompRatio), 1) + ":1"
                                + (v(InputP::InCompOn) >= 0.5f ? "  GR " + juce::String(gr, 1) : juce::String("  off")),
                            juce::dontSendNotification);
        groups.repaint();
    }

    int layoutHeight(int width) { return flowItems(width, false) + px(10); }
    void resized() override { flowItems(getWidth(), true); }
    void mouseDown(const juce::MouseEvent&) override { ctx.select({ Selection::Kind::Input, i }); }

    LevelMeter meter;
    MatrixGrid groups;
    juce::TextButton rec { "REC" }, presetBtn { juce::String::fromUTF8("\xe2\x96\xbe") };

private:
    int flowItems(int width, bool apply) {
        auto r = juce::Rectangle<int>(0, 0, width, 10000).reduced(px(5));
        if (apply) meter.setBounds(r.getX(), r.getY(), px(10), getHeight() - px(10));
        r.removeFromLeft(px(16));
        const std::vector<kit::FlowItem> items {
            { &indexLabel, 30 }, { &nameLabel, 130 }, { &zoneLabel, 84 }, { &sourceBtn, 120 }, { &link, 82 }, { &trim, 176 },
            { &pol, 34 }, { &mute, 32 }, { &solo, 32 }, { &rec, 58 }, { &groups, 8 * 24 + 1 },
            { &hpOn, 52, true }, { &hpHz, 136 }, { &hpSlope, 94 }, { &lpOn, 52 }, { &lpHz, 136 }, { &lpSlope, 94 },
            { &notchOn, 70 }, { &notchHz, 130 }, { &notchQ, 104 }, { &notchDepth, 124 }, { &align, 176 },
            { &dsOn, 44 }, { &dsSummary, 112 }, { &compOn, 62 }, { &compSummary, 132 }, { &grMeter, 36 }, { &presetBtn, 28 } };
        return kit::flow(items, r, px(26), px(4), apply);
    }

    void showSourceMenu() {
        auto* hs = proc.getHostServices();
        if (hs == nullptr) { ctx.say("Plug-in: input " + juce::String(i + 1) + " is host channel " + juce::String(i + 1) + " (set in the host)"); return; }
        juce::PopupMenu m;
        m.addSectionHeader("Physical source for " + proc.getShow().inputDisplayName(i));
        const auto names = hs->getInputChannelNames();
        m.addItem(1, "(unassigned)");
        for (int k = 0; k < names.size(); ++k) m.addItem(10 + k, names[k]);
        juce::Component::SafePointer<InputRow> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sourceBtn), [safe, names](int r) {
            if (safe == nullptr || r <= 0) return;
            if (auto* h = safe->proc.getHostServices()) h->setInputBinding(safe->i, r == 1 ? juce::String() : names[r - 10]);
            safe->refreshSource();
        });
    }

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    const int i;
    bool live = false, selected = false;
    juce::Label indexLabel, nameLabel, zoneLabel, dsSummary, compSummary;
    juce::TextButton sourceBtn;
    ParamToggle link, pol, mute, solo, hpOn, lpOn, notchOn, dsOn, compOn;
    ParamChoice hpSlope, lpSlope;
    kit::Captioned trim, hpHz, lpHz, notchHz, notchQ, notchDepth, align;
    GainReductionMeter grMeter;
};

// ================================================================= advanced section for the selected input
class AdvancedPanel final : public kit::Card {
public:
    explicit AdvancedPanel(EditorContext& c) : kit::Card("Advanced"), ctx(c), proc(c.proc), filter(c.proc), transfer(c.proc) {
        setComponentID("inputs-advanced");
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &filter, &spectrum, &history, &correlation, &transfer, &fbText })
            addAndMakeVisible(comp);
        kit::styleLabel(historyCaption, 11.5f, true, palette().accent);
        historyCaption.setText("WAVEFORM HISTORY", juce::dontSendNotification);
        addAndMakeVisible(historyCaption);
        kit::styleLabel(fbText, 11.5f, false, palette().text, juce::Justification::topLeft);
        fbText.setTooltip("Feedback candidates detected on this input (advisory). Apply adds a notch at the suggested frequency.");
        for (int k = 0; k < 3; ++k) {
            auto* b = applyBtns.add(new juce::TextButton("Apply notch"));
            b->setTooltip("Apply the suggested notch to this input's notch filter (operator action, undoable)");
            b->onClick = [this, k] {
                if (k >= numCand) return;
                const auto& c = cands[static_cast<size_t>(k)];
                proc.getUndoManager().beginNewTransaction("Apply notch suggestion");
                proc.applyNotchSuggestion(input, c.frequencyHz, c.suggestedQ, c.suggestedDepthDb);
                ctx.say("Notch applied at " + kit::fmtHz(c.frequencyHz) + "Hz on " + proc.getShow().inputDisplayName(input));
            };
            addChildComponent(b);
        }
        history.snapshot = [this](float* mn, float* mx, float* rms, int n) {
            if (live) proc.getEngine().inputHistory(input).snapshot(mn, mx, rms, n);
        };
        spectrum.source = [this](float* out, int n) { proc.getEngine().feedback().getSpectrumDb(out, n); };
        correlation.setTooltip("Correlation of the two linked inputs' level envelopes (an estimate from the level history; "
                               "true phase correlation needs a per-pair meter). Negative values suggest opposite polarity or poor summing.");
    }
    void setInput(int i) {
        input = i;
        filter.setInput(i);
        transfer.setCompBlock(pidx(i, InputP::InCompOn));
        setHeading("Advanced: " + proc.getShow().inputDisplayName(i));
        if (live) proc.setFeedbackTap(1 + i);
        spectrum.caption = "tap: input " + juce::String(i + 1);
    }
    void setLive(bool l) {
        if (l == live) return;
        live = l;
        if (l) { savedTap = proc.getEngine().getFeedbackTap(); proc.setFeedbackTap(1 + input); }
        else proc.setFeedbackTap(savedTap);
    }
    void tick() {
        if (!live) return;
        filter.refresh();
        transfer.setGainReduction(std::abs(proc.getMeters().inGrDb[static_cast<size_t>(input)].load()));
        if (++counter % 3 != 0) return;
        transfer.refresh();
        proc.getEngine().feedback().analyse();
        numCand = proc.getEngine().feedback().getCandidates(cands.data(), static_cast<int>(cands.size()));
        spectrum.markersHz.clear();
        juce::String t;
        if (numCand == 0) t = "No persistent feedback candidates on this input.";
        for (int k = 0; k < numCand && k < 3; ++k) {
            const auto& c = cands[static_cast<size_t>(k)];
            spectrum.markersHz.push_back(c.frequencyHz);
            t << juce::String::fromUTF8("\xe2\x96\xb2 ") << kit::fmtHz(c.frequencyHz) << "Hz  sev " << juce::String(c.severity, 2) << "  "
              << juce::String(c.persistenceSec, 1) << " s  -> notch Q " << juce::String(c.suggestedQ, 0) << " " << juce::String(c.suggestedDepthDb, 1) << " dB\n";
        }
        fbText.setText(t, juce::dontSendNotification);
        for (int k = 0; k < applyBtns.size(); ++k) applyBtns[k]->setVisible(k < numCand);
        spectrum.refresh();
        updateCorrelation();
        resized();
    }
    void resized() override {
        auto r = getContentBounds();
        const int colW = r.getWidth() / 3, rowH = r.getHeight() / 2;
        auto top = r.removeFromTop(rowH);
        filter.setBounds(top.removeFromLeft(colW).reduced(px(3)));
        spectrum.setBounds(top.removeFromLeft(colW).reduced(px(3)));
        auto h = top.reduced(px(3));
        historyCaption.setBounds(h.removeFromTop(px(16)));
        history.setBounds(h);
        transfer.setBounds(r.removeFromLeft(colW).reduced(px(3)));
        correlation.setBounds(r.removeFromLeft(colW).reduced(px(3)));
        auto fb = r.reduced(px(3));
        auto btnCol = fb.removeFromRight(px(96));
        fbText.setBounds(fb);
        for (int k = 0; k < applyBtns.size(); ++k) applyBtns[k]->setBounds(btnCol.getX(), fb.getY() + k * px(17) - px(1), btnCol.getWidth(), px(17));
    }
    int getInput() const noexcept { return input; }
    FilterResponseView filter;

private:
    void updateCorrelation() {
        const int n = proc.getActiveInputChannelCount();
        int a = -1;
        if (kit::isOn(proc, pidx(input, InputP::Link)) && input + 1 < n) a = input;
        else if (input > 0 && kit::isOn(proc, pidx(input - 1, InputP::Link))) a = input - 1;
        if (a < 0) { correlation.set(false, 0.0f, "Mono source. Enable LINK to treat this input and the next as a stereo pair."); return; }
        constexpr int kN = 128;
        float mnA[kN], mxA[kN], rA[kN], mnB[kN], mxB[kN], rB[kN];
        proc.getEngine().inputHistory(a).snapshot(mnA, mxA, rA, kN);
        proc.getEngine().inputHistory(a + 1).snapshot(mnB, mxB, rB, kN);
        const float corr = sequenceCorrelation(rA, rB, kN);
        const bool pa = kit::isOn(proc, pidx(a, InputP::Pol)), pb = kit::isOn(proc, pidx(a + 1, InputP::Pol));
        correlation.set(true, corr, "Pair " + juce::String(a + 1) + "/" + juce::String(a + 2) + juce::String::fromUTF8(":  \xc3\x98 ")
                                        + (pa ? "L inverted" : "L normal") + ", " + (pb ? "R inverted" : "R normal"));
    }
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    int input = 0, savedTap = 0, counter = 0, numCand = 0;
    bool live = false;
    SpectrumView spectrum;
    HistoryStrip history;
    juce::Label historyCaption, fbText;
    PairCorrelationView correlation;
    TransferCurveView transfer;
    juce::OwnedArray<juce::TextButton> applyBtns;
    std::array<dsp::FeedbackCandidate, 8> cands {};
};

// ================================================================= page
class InputsPage final : public Page, private Tickable {
public:
    explicit InputsPage(EditorContext& c) : Page(c), advanced(c) {
        kit::styleLabel(title, 16.0f, true, palette().accent);
        addAndMakeVisible(title);
        kit::styleLabel(hint, 11.5f, false, palette().textMuted);
        hint.setText("Click a row to inspect. Double-click names to rename. Group cells: click on/off, drag for level.", juce::dontSendNotification);
        addAndMakeVisible(hint);
        unitBox.addItemList({ "Align: ms", "Align: samples", "Align: metres" }, 1);
        unitBox.setSelectedId(1, juce::dontSendNotification);
        unitBox.setTooltip("Display / entry unit for alignment delay (metres use 343 m/s at the engine sample rate)");
        unitBox.setTitle("Alignment unit");
        unitBox.onChange = [this] { for (auto* r : rows) r->setAlignUnit(unitBox.getSelectedId() - 1); };
        addAndMakeVisible(unitBox);
        advancedBtn.setClickingTogglesState(true);
        advancedBtn.setComponentID("inputs-advanced-toggle");
        advancedBtn.setTooltip("Show the advanced analysis section for the selected input (filter graph, spectrum, waveform, correlation, dynamics, feedback)");
        advancedBtn.onClick = [this] { updateAdvanced(); resized(); };
        addAndMakeVisible(advancedBtn);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        addChildComponent(advanced);
        rebuildRows();
    }
    ~InputsPage() override { advanced.setLive(false); }

    void pageShown() override {
        live = true;
        if (proc.getActiveInputChannelCount() != rows.size()) rebuildRows();
        for (auto* r : rows) { r->setLive(true); r->refreshNames(); r->refreshRec(); }
        syncSelection();
        updateAdvanced();
    }
    void pageHidden() override {
        live = false;
        for (auto* r : rows) r->setLive(false);
        advanced.setLive(false);
    }
    void tick() override {
        if (!live) return;
        if (proc.getActiveInputChannelCount() != rows.size()) { rebuildRows(); syncSelection(); }
        for (auto* r : rows) r->tick();
        advanced.tick();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(px(8));
        auto top = r.removeFromTop(px(28));
        title.setBounds(top.removeFromLeft(px(170)));
        advancedBtn.setBounds(top.removeFromRight(px(150)).reduced(0, px(1)));
        top.removeFromRight(px(6));
        unitBox.setBounds(top.removeFromRight(px(150)).reduced(0, px(1)));
        hint.setBounds(top);
        r.removeFromTop(px(4));
        if (advanced.isVisible()) {
            advanced.setBounds(r.removeFromBottom(juce::jmax(px(220), r.getHeight() * 45 / 100)));
            r.removeFromBottom(px(6));
        }
        viewport.setBounds(r);
        layoutRows();
    }

private:
    void selectionChanged() override { syncSelection(); }
    void showModelChanged() override { for (auto* r : rows) { r->refreshNames(); r->refreshRec(); } if (advanced.isVisible()) advanced.setInput(selected); }

    int selectedInput() const {
        const auto s = ctx.getSelection();
        return s.kind == Selection::Kind::Input && s.index < rows.size() ? s.index : juce::jmin(lastSelected, juce::jmax(0, rows.size() - 1));
    }
    void syncSelection() {
        selected = selectedInput();
        lastSelected = selected;
        for (int k = 0; k < rows.size(); ++k) rows[k]->setSelected(k == selected);
        if (advanced.isVisible()) advanced.setInput(selected);
    }
    void updateAdvanced() {
        const bool open = advancedBtn.getToggleState();
        advancedBtn.setButtonText(open ? juce::String::fromUTF8("\xe2\x96\xbe Advanced") : juce::String::fromUTF8("\xe2\x96\xb8 Advanced"));
        advanced.setVisible(open);
        advanced.setLive(open && live);
        if (open) advanced.setInput(selected);
    }
    void rebuildRows() {
        rows.clear();
        const int n = juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount());
        for (int i = 0; i < n; ++i) {
            auto* row = rows.add(new InputRow(ctx, i));
            row->setLive(live);
            row->setAlignUnit(unitBox.getSelectedId() - 1);
            content.addAndMakeVisible(row);
        }
        title.setText("Inputs (" + juce::String(n) + " active)", juce::dontSendNotification);
        layoutRows();
    }
    void layoutRows() {
        const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 2;
        if (w <= 0) return;
        int y = 0;
        for (auto* row : rows) {
            const int h = row->layoutHeight(w);
            row->setBounds(0, y, w, h);
            y += h + px(4);
        }
        content.setSize(w, juce::jmax(y, 10));
    }

    juce::Label title, hint;
    juce::ComboBox unitBox;
    juce::TextButton advancedBtn { "Advanced" };
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<InputRow> rows;
    AdvancedPanel advanced;
    bool live = false;
    int selected = 0, lastSelected = 0;
};
}
std::unique_ptr<Page> makeInputsPage(EditorContext& c) { return std::make_unique<InputsPage>(c); }
}
