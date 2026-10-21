#include "LineCheckPanel.h"
#include "Engine/LineCheckApply.h"
#include "Routing/WiringTemplates.h"

// Defined in UI/PageKit.cpp (PageKit.h is not included: its styleLabel overload would be ambiguous with EditKit.h).
namespace crowdmike::ui::kit {
void setupButton(juce::Button&, const juce::String& tooltip, const juce::String& componentId);
void confirmAsync(juce::Component* associated, const juce::String& title, const juce::String& message, const juce::String& okText,
                  std::function<void()> onConfirm);
}

namespace crowdmike::ui {
using namespace crowdmike::params;

namespace {
int iidx(int i, int e) { return ParamBank::index(Mod::Input, i, e); }
juce::String db1(float v) { return v <= -119.0f ? juce::String("-inf") : juce::String(v, 1); }
juce::String signedDb(float v) { return (v > 0.05f ? "+" : "") + juce::String(v, 1); }
}

// ----------------------------------------------------------------- one result row
struct LineCheckPanel::Row final : public juce::Component {
    Row(LineCheckPanel& o, const linecheck::Result& r, const juce::String& inputName, float trimNow) : owner(o), result(r) {
        setComponentID("linecheck-row-" + juce::String(r.input));
        setTitle("Line check result for " + inputName);
        kit::styleLabel(name, 12.5f, true);
        name.setText(juce::String(r.input + 1) + "  " + inputName, juce::dontSendNotification);
        using V = linecheck::Verdict;
        StatusBadge::Level level = StatusBadge::Level::Ok;
        if (r.verdict == V::Dead || r.verdict == V::Clipping) level = StatusBadge::Level::Danger;
        else if (r.verdict == V::Hot || r.verdict == V::Low) level = StatusBadge::Level::Warn;
        juce::String glyph = r.verdict == V::Ok ? juce::String::fromUTF8("\xe2\x9c\x93 ") : juce::String::fromUTF8("\xe2\x96\xb2 ");
        badge.set(glyph + linecheck::verdictName(r.verdict), level);
        badge.setTitle(juce::String("Verdict: ") + linecheck::verdictName(r.verdict));
        kit::styleLabel(levels, 11.5f, false);
        levels.setFont(monoFont(11.5f));
        levels.setText(r.valid ? "prog " + db1(r.programRmsDb) + "  pk " + db1(r.peakDb) + "  floor " + db1(r.noiseFloorDb) + " dBFS" : juce::String("no data"),
                       juce::dontSendNotification);
        levels.setTooltip("Programme level (loudest 30 % of the 10 s), peak and noise-floor estimate, all pre-trim");
        kit::styleLabel(flags, 11.5f, false, linecheck::describeFlags(r).empty() ? palette().textMuted : palette().warn);
        flags.setText(linecheck::describeFlags(r).empty() ? juce::String("-") : juce::String(linecheck::describeFlags(r)), juce::dontSendNotification);
        flags.setTooltip(flags.getText());
        kit::styleLabel(trim, 11.5f, false);
        trim.setFont(monoFont(11.5f));
        trim.setText(r.hasTrim ? "Trim " + signedDb(trimNow) + " > " + signedDb(r.proposedTrimDb) + (r.trimNoChange ? " (ok)" : "") : juce::String("Trim -"),
                     juce::dontSendNotification);
        kit::styleLabel(hpf, 11.5f, false);
        hpf.setText(r.suggestHpOn ? "HPF " + juce::String(juce::roundToInt(r.suggestHpHz)) + " Hz" + (r.hpNoChange ? " (ok)" : "") : juce::String("HPF -"),
                    juce::dontSendNotification);
        const bool anyChange = (r.hasTrim && !r.trimNoChange) || (r.suggestHpOn && !r.hpNoChange);
        kit::setupButton(applyBtn, "Apply this input's proposal (trim and high-pass); undoable", "linecheck-apply-" + juce::String(r.input));
        applyBtn.setButtonText("Apply");
        applyBtn.setEnabled(anyChange);
        applyBtn.onClick = [this] { owner.applyInput(result.input); };
        kit::setupButton(flipBtn, "Flip this input's polarity (undoable)", "linecheck-flip-" + juce::String(r.input));
        flipBtn.setButtonText(juce::String::fromUTF8("Flip \xc3\x98"));
        flipBtn.onClick = [this] { owner.applyInput(result.input, true); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &name, &badge, &levels, &flags, &trim, &hpf, &applyBtn })
            addAndMakeVisible(c);
        addChildComponent(flipBtn);
        flipBtn.setVisible(r.polarityMismatch);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(palette().panelRaised);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(px(3), px(1));
        const int w = r.getWidth();
        name.setBounds(r.removeFromLeft(juce::jlimit(px(70), px(150), w / 8)));
        badge.setBounds(r.removeFromLeft(px(92)).reduced(0, px(2)));
        const int fixedRight = px(58) + (flipBtn.isVisible() ? px(58) : 0) + px(6);
        applyBtn.setBounds(r.removeFromRight(px(54)).reduced(0, px(1)));
        if (flipBtn.isVisible()) flipBtn.setBounds(r.removeFromRight(px(58)).reduced(0, px(1)));
        (void) fixedRight;
        const int rest = r.getWidth();
        levels.setBounds(r.removeFromLeft(juce::jmax(px(150), rest * 32 / 100)));
        trim.setBounds(r.removeFromLeft(juce::jmax(px(120), rest * 22 / 100)));
        hpf.setBounds(r.removeFromLeft(juce::jmax(px(80), rest * 14 / 100)));
        flags.setBounds(r);
    }
    LineCheckPanel& owner;
    linecheck::Result result;
    juce::Label name, levels, flags, trim, hpf;
    StatusBadge badge;
    juce::TextButton applyBtn, flipBtn;
};

// ----------------------------------------------------------------- panel
LineCheckPanel::LineCheckPanel(EditorContext& c) : kit::Card("Line Check"), ctx(c), proc(c.proc) {
    setComponentID("linecheck-panel");
    setTitle("Line check");
    setSubtitle("Talk, clap or let the crowd make noise for 10 seconds; nothing changes until you press Apply");
    kit::setupButton(startBtn, "Listen to all active inputs for 10 seconds (talk, clap or crowd noise). Inputs keep working.", "linecheck-start");
    kit::setupButton(stopBtn, "Stop listening: Stop analyses what was heard (needs 2 s), shift-click cancels", "linecheck-stop");
    kit::setupButton(applyAllBtn, "Apply the proposed trims and high-pass settings of all inputs as one undo step", "linecheck-apply-all");
    kit::setupButton(undoBtn, "Undo the last Line Check apply", "linecheck-undo");
    kit::setupButton(quickBtn, "Pick a wiring template for the channel count and set sane high-pass defaults", "linecheck-quick");
    startBtn.onClick = [this] { if (currentState == State::Listening) stopListening(false); else startListening(); };
    stopBtn.onClick = [this] { stopListening(true); };
    applyAllBtn.onClick = [this] { applyAll(); };
    undoBtn.onClick = [this] { undoLastApply(); };
    quickBtn.onClick = [this] {
        const int ins = proc.getActiveInputChannelCount(), outs = proc.getActiveOutputChannelCount();
        const auto id = static_cast<WiringTemplates::Id>(linecheck::chooseTemplate(ins, outs));
        kit::confirmAsync(this, "Quick start", juce::String("Apply the wiring template \"") + WiringTemplates::name(id)
                                                   + "\" for " + juce::String(ins) + " inputs / " + juce::String(outs) + " outputs?\n\nThis replaces the routing matrices (parameters can be undone).",
                          "Apply", [this] { quickStart(); });
    };
    kit::styleLabel(status, 12.0f, false, palette().text);
    status.setComponentID("linecheck-status");
    kit::styleLabel(targetLabel, 11.5f, false, palette().textMuted);
    targetLabel.setText("Target", juce::dontSendNotification);
    target.setRange(-30.0, -10.0, 0.5);
    target.setValue(settings.targetRmsDb, juce::dontSendNotification);
    target.setTextValueSuffix(" dBFS");
    target.setTextBoxStyle(juce::Slider::TextBoxRight, false, px(70), px(20));
    target.setTooltip("Target programme RMS level for the proposed trims (default -20 dBFS)");
    target.setTitle("Line check target level");
    target.setComponentID("linecheck-target");
    target.onValueChange = [this] { settings.targetRmsDb = static_cast<float>(target.getValue()); };
    viewport.setViewedComponent(&rowHolder, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setTitle("Line check results");
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &startBtn, &stopBtn, &applyAllBtn, &undoBtn, &quickBtn, &status, &targetLabel, &target, &bar, &viewport })
        addAndMakeVisible(comp);
    status.setText("Ready. Press Start and make some noise on every input.", juce::dontSendNotification);
    refreshButtons();
}

LineCheckPanel::~LineCheckPanel() {
    proc.getEngine().lineProbe().stop();
    viewport.setViewedComponent(nullptr, false);
}

int LineCheckPanel::preferredHeight(int nRows) noexcept { return px(24) + px(34) + px(8) + juce::jlimit(2, 8, nRows) * px(28) + px(14); }

linecheck::ApplyOptions LineCheckPanel::optionsFor(bool polarity) const {
    linecheck::ApplyOptions o;
    o.polarity = polarity;
    return o;
}

std::vector<linecheck::InputContext> LineCheckPanel::contexts() const {
    std::vector<linecheck::InputContext> v;
    const int n = juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount());
    for (int i = 0; i < n; ++i) {
        linecheck::InputContext c;
        c.active = true;
        c.polarityInverted = kit::isOn(proc, iidx(i, InputP::Pol));
        c.trimDb = kit::actual(proc, iidx(i, InputP::Trim));
        c.hpOn = kit::isOn(proc, iidx(i, InputP::HpOn));
        c.hpHz = kit::actual(proc, iidx(i, InputP::HpHz));
        v.push_back(c);
    }
    return v;
}

void LineCheckPanel::setActive(bool a) {
    active = a;
    if (!a && currentState == State::Listening) stopListening(false);
}

void LineCheckPanel::startListening() {
    const int n = juce::jlimit(1, kMaxInputs, proc.getActiveInputChannelCount());
    collector.begin(proc.getEngine().lineProbe(), n);
    currentState = State::Listening;
    applied = false;
    listeningTicks = 0;
    lastResults.clear();
    rebuildRows();
    startBtn.setButtonText("Cancel");
    status.setText("Listening... talk, clap or let the crowd make noise on every input.", juce::dontSendNotification);
    refreshButtons();
    resized();
}

void LineCheckPanel::stopListening(bool analyse) {
    proc.getEngine().lineProbe().stop();
    startBtn.setButtonText("Start 10 s listen");
    if (currentState != State::Listening) return;
    collector.pull(proc.getEngine().lineProbe());
    if (analyse && collector.framesCollected() >= linecheck::kMinFramesForEarlyStop) { analyseNow(); return; }
    currentState = lastResults.empty() ? State::Idle : State::Results;
    status.setText(analyse ? "Not enough audio heard yet (need 2 s). Try again." : "Cancelled.", juce::dontSendNotification);
    refreshButtons();
    resized();
}

void LineCheckPanel::tick() {
    if (currentState != State::Listening) return;
    auto& probe = proc.getEngine().lineProbe();
    collector.pull(probe);
    progressValue = collector.progress();
    ++listeningTicks;
    const int sec = collector.framesCollected() / 10;
    status.setText("Listening... " + juce::String(sec) + " / " + juce::String(linecheck::kListenSeconds) + " s"
                       + (probe.dropped() > 0 ? "  (some audio was skipped)" : juce::String()),
                   juce::dontSendNotification);
    if (collector.isComplete()) analyseNow();
}

void LineCheckPanel::analyseNow() {
    proc.getEngine().lineProbe().stop();
    lastResults = linecheck::Analyser::analyse(collector.frames(), contexts(), settings);
    currentState = State::Results;
    startBtn.setButtonText("Run again");
    int ok = 0, problems = 0;
    for (const auto& r : lastResults) {
        if (r.verdict == linecheck::Verdict::Ok && !r.dc && !r.hum && !r.polarityMismatch) ++ok; else ++problems;
    }
    status.setText(juce::String(ok) + " input(s) OK, " + juce::String(problems) + " need attention. Review the proposals, then Apply.", juce::dontSendNotification);
    progressValue = 1.0;
    rebuildRows();
    refreshButtons();
    resized();
}

void LineCheckPanel::rebuildRows() {
    rows.clear();
    for (const auto& r : lastResults)
        rowHolder.addAndMakeVisible(rows.add(new Row(*this, r, proc.getShow().inputDisplayName(r.input), kit::actual(proc, iidx(r.input, InputP::Trim)))));
}

void LineCheckPanel::refreshButtons() {
    const bool listening = currentState == State::Listening;
    bool anyChange = false;
    for (const auto& r : lastResults) anyChange = anyChange || (r.hasTrim && !r.trimNoChange) || (r.suggestHpOn && !r.hpNoChange);
    stopBtn.setVisible(listening);
    stopBtn.setEnabled(listening);
    applyAllBtn.setEnabled(currentState == State::Results && anyChange);
    undoBtn.setEnabled(applied);
    quickBtn.setEnabled(!listening);
    bar.setVisible(listening);
}

bool LineCheckPanel::applyAll() {
    if (currentState != State::Results || lastResults.empty()) return false;
    std::vector<int> all;
    for (const auto& r : lastResults) all.push_back(r.input);
    linecheck::ApplyTarget t { proc.getUndoManager(),
        [this](const juce::String& id, float v) { proc.getHub().setParamActual(ParamBank::indexForId(id), v); },
        [this](const juce::String& id) { return kit::actual(proc, ParamBank::indexForId(id)); } };
    const int writes = linecheck::applyProposals(t, lastResults, all, optionsFor(false));
    applied = writes > 0;
    status.setText(writes > 0 ? "Applied " + juce::String(writes) + " change(s). Undo reverts them in one step." : "Nothing to change.", juce::dontSendNotification);
    ctx.say(status.getText());
    // refresh the proposals against the new live values
    lastResults = linecheck::Analyser::analyse(collector.frames(), contexts(), settings);
    rebuildRows();
    refreshButtons();
    resized();
    return writes > 0;
}

bool LineCheckPanel::applyInput(int input, bool includePolarity) {
    if (currentState != State::Results || input < 0 || input >= static_cast<int>(lastResults.size())) return false;
    linecheck::ApplyTarget t { proc.getUndoManager(),
        [this](const juce::String& id, float v) { proc.getHub().setParamActual(ParamBank::indexForId(id), v); },
        [this](const juce::String& id) { return kit::actual(proc, ParamBank::indexForId(id)); } };
    auto o = optionsFor(includePolarity);
    if (includePolarity) { o.trim = false; o.hpf = false; }
    const int writes = linecheck::applyProposals(t, lastResults, { input }, o);
    applied = applied || writes > 0;
    ctx.say(writes > 0 ? "Line check applied to " + proc.getShow().inputDisplayName(input) : "Nothing to change on " + proc.getShow().inputDisplayName(input));
    lastResults = linecheck::Analyser::analyse(collector.frames(), contexts(), settings);
    rebuildRows();
    refreshButtons();
    resized();
    return writes > 0;
}

bool LineCheckPanel::undoLastApply() {
    if (!applied) return false;
    const bool ok = proc.getUndoManager().undo();
    applied = false;
    status.setText(ok ? "Last Line Check apply undone." : "Nothing to undo.", juce::dontSendNotification);
    if (currentState == State::Results) {
        lastResults = linecheck::Analyser::analyse(collector.frames(), contexts(), settings);
        rebuildRows();
    }
    refreshButtons();
    resized();
    return ok;
}

void LineCheckPanel::quickStart() {
    const int ins = proc.getActiveInputChannelCount(), outs = proc.getActiveOutputChannelCount();
    const int id = linecheck::chooseTemplate(ins, outs);
    proc.applyWiringTemplate(id);                  // begins its own undo transaction
    linecheck::ApplyTarget t { proc.getUndoManager(),
        [this](const juce::String& pid, float v) { proc.getHub().setParamActual(ParamBank::indexForId(pid), v); },
        [this](const juce::String& pid) { return kit::actual(proc, ParamBank::indexForId(pid)); } };
    linecheck::applyQuickStartDefaults(t, ins);    // joins that transaction
    applied = true;
    status.setText(juce::String("Quick start applied: ") + WiringTemplates::name(static_cast<WiringTemplates::Id>(id)) + ". Next: run Line Check to set levels.",
                   juce::dontSendNotification);
    ctx.say(status.getText());
    ctx.showModelChanged();
    refreshButtons();
}

void LineCheckPanel::resized() {
    auto r = getContentBounds();
    auto top = r.removeFromTop(px(28));
    startBtn.setBounds(top.removeFromLeft(px(124)).reduced(0, px(2)));
    top.removeFromLeft(px(4));
    stopBtn.setBounds(top.removeFromLeft(px(54)).reduced(0, px(2)));
    top.removeFromLeft(px(10));
    targetLabel.setBounds(top.removeFromLeft(px(40)));
    target.setBounds(top.removeFromLeft(px(200)));
    top.removeFromLeft(px(10));
    quickBtn.setBounds(top.removeFromRight(px(110)).reduced(0, px(2)));
    top.removeFromRight(px(4));
    undoBtn.setBounds(top.removeFromRight(px(60)).reduced(0, px(2)));
    top.removeFromRight(px(4));
    applyAllBtn.setBounds(top.removeFromRight(px(86)).reduced(0, px(2)));
    auto statusRow = r.removeFromTop(px(22));
    if (currentState == State::Listening) bar.setBounds(statusRow.removeFromRight(juce::jmax(px(120), statusRow.getWidth() / 4)).reduced(0, px(3)));
    status.setBounds(statusRow);
    r.removeFromTop(px(4));
    viewport.setBounds(r);
    const int w = r.getWidth() - viewport.getScrollBarThickness();
    int y = 0;
    for (auto* row : rows) { row->setBounds(0, y, w, px(26)); y += px(28); }
    rowHolder.setSize(juce::jmax(10, w), juce::jmax(1, y));
}
}
