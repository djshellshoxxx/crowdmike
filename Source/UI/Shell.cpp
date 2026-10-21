#include "Shell.h"
#include "Core/PresetManager.h"

namespace crowdmike::ui {
using namespace crowdmike;
using namespace crowdmike::params;

namespace {
juce::String formatMs(int samples, double sr) {
    return sr > 0.0 ? juce::String(samples * 1000.0 / sr, 1) + " ms" : juce::String("-");
}
void styleLabel(juce::Label& l, float h, bool bold = false, juce::Colour c = palette().text, juce::Justification j = juce::Justification::centredLeft) {
    l.setFont(font(h, bold));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(j);
    l.setMinimumHorizontalScale(0.6f);
}
}

// ================================================================= StatusBar
StatusBar::StatusBar(EditorContext& c)
    : ctx(c), proc(c.proc),
      panic(c.proc, ParamBank::index(Mod::Global, 0, GlobalP::Panic), "PANIC MUTE"),
      bypass(c.proc, ParamBank::index(Mod::Global, 0, GlobalP::Bypass), "BYPASS"),
      tempoWidget(c) {
    title.setText("CROWDMIKE", juce::dontSendNotification);
    styleLabel(title, 17.0f, true, palette().accent);
    for (auto* l : { &sceneLabel, &rateLabel, &latencyLabel, &cpuLabel, &messageLabel }) styleLabel(*l, 12.5f);
    styleLabel(messageLabel, 12.5f, false, palette().warn);
    for (auto* l : { &title, &sceneLabel, &rateLabel, &latencyLabel, &cpuLabel, &messageLabel }) addAndMakeVisible(*l);
    for (auto* b : { &prevScene, &nextScene }) { addAndMakeVisible(*b); }
    prevScene.setTooltip("Previous scene (Ctrl+Left)");
    nextScene.setTooltip("Next scene (Ctrl+Right)");
    prevScene.onClick = [this] { proc.getScenes().previous(proc.getShow().ui.recallScope); };
    nextScene.onClick = [this] { proc.getScenes().next(proc.getShow().ui.recallScope); };
    for (auto* b : { &deviceBadge, &recBadge, &xrunBadge, &modeBadge }) addAndMakeVisible(*b);
    panic.setColour(juce::ToggleButton::textColourId, palette().text);
    panic.setTooltip("Mute all crowd reinforcement outputs immediately. Recording continues. (F12)");
    bypass.setTooltip("True bypass: host input straight to output");
    addAndMakeVisible(panic);
    addAndMakeVisible(bypass);
    addAndMakeVisible(tempoWidget);
    c.notify = [this](const juce::String& m, bool w) { showMessage(m, w); };
    c.addListener(this);
    tick();
}
StatusBar::~StatusBar() { ctx.removeListener(this); ctx.notify = nullptr; }

void StatusBar::showMessage(const juce::String& m, bool warning) {
    messageLabel.setText(m, juce::dontSendNotification);
    messageLabel.setColour(juce::Label::textColourId, warning ? palette().warn : palette().text);
    messageTicks = 150;
}

void StatusBar::paint(juce::Graphics& g) {
    g.fillAll(palette().panel);
    g.setColour(palette().panelEdge);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void StatusBar::resized() {
    auto full = getLocalBounds();
    // Row 2 (right): sample rate / block, latency, CPU. They used to share row 1, where at the minimum window width (1100 px) the
    // badges left them no room at all (GUI_SPEC section 2 requires them to be visible).
    auto tempoRow = full.removeFromBottom(kTempoRowHeight).reduced(8, 1);
    cpuLabel.setBounds(tempoRow.removeFromRight(78));
    latencyLabel.setBounds(tempoRow.removeFromRight(100));
    rateLabel.setBounds(tempoRow.removeFromRight(130));
    tempoRow.removeFromRight(8);
    tempoWidget.setBounds(tempoRow.removeFromLeft(TempoWidget::kPreferredWidth)); tempoRow.removeFromLeft(8);
    messageLabel.setBounds(tempoRow);
    auto r = full.reduced(8, 5);
    // Right block first (fixed widths), then the scene name takes whatever is left (never overlapping the badges).
    panic.setBounds(r.removeFromRight(140)); r.removeFromRight(6);
    bypass.setBounds(r.removeFromRight(86)); r.removeFromRight(6);
    recBadge.setBounds(r.removeFromRight(108)); r.removeFromRight(6);
    modeBadge.setBounds(r.removeFromRight(104)); r.removeFromRight(6);
    xrunBadge.setBounds(r.removeFromRight(100)); r.removeFromRight(6);
    deviceBadge.setBounds(r.removeFromRight(juce::jlimit(120, 190, r.getWidth() - 330))); r.removeFromRight(6);
    title.setBounds(r.removeFromLeft(118));
    prevScene.setBounds(r.removeFromLeft(26)); r.removeFromLeft(2);
    const int sceneW = juce::jlimit(60, 220, r.getWidth() - 2 - 26 - 10);
    sceneLabel.setBounds(r.removeFromLeft(sceneW)); r.removeFromLeft(2);
    nextScene.setBounds(r.removeFromLeft(26));
}

void StatusBar::tick() {
    auto& m = proc.getMeters();
    const double sr = m.sampleRate.load();
    sceneLabel.setText("Scene: " + (proc.getScenes().current() >= 0 ? proc.getCurrentSceneName() : juce::String("(none)")), juce::dontSendNotification);
    rateLabel.setText(juce::String(sr / 1000.0, 1) + " kHz / " + juce::String(m.blockSize.load()) + " smp", juce::dontSendNotification);
    int hostLat = 0;
    if (auto* hs = proc.getHostServices()) { const auto st = hs->getDeviceStatus(); hostLat = st.inputLatencySamples + st.outputLatencySamples; }
    latencyLabel.setText("Lat " + formatMs(m.latencySamples.load() + m.blockSize.load() + hostLat, sr), juce::dontSendNotification);
    cpuLabel.setText("CPU " + juce::String(juce::roundToInt(m.cpuLoad.load() * 100.0f)) + "%", juce::dontSendNotification);

    if (auto* hs = proc.getHostServices()) {
        const auto st = hs->getDeviceStatus();
        if (st.deviceLost || !st.deviceOpen) deviceBadge.set("NO DEVICE", StatusBadge::Level::Danger);
        else deviceBadge.set(st.deviceName.substring(0, 22), StatusBadge::Level::Ok);
        xrunBadge.set("XRUN " + juce::String(st.xruns), st.xruns > 0 ? StatusBadge::Level::Warn : StatusBadge::Level::Off);
    } else {
        deviceBadge.set("HOST I/O", StatusBadge::Level::Ok);
        const auto over = static_cast<int>(m.overruns.load());
        xrunBadge.set("OVERRUN " + juce::String(over), over > 0 ? StatusBadge::Level::Warn : StatusBadge::Level::Off);
    }
    const auto rs = proc.getRecorder().getStatus();
    if (rs.errors > 0) recBadge.set("REC ERROR", StatusBadge::Level::Danger);
    else if (rs.recording) {
        const int secs = static_cast<int>(rs.elapsedSeconds);
        recBadge.set(juce::String::formatted("REC %02d:%02d", secs / 60, secs % 60) + (rs.dropouts > 0 ? "!" : ""), rs.dropouts > 0 ? StatusBadge::Level::Warn : StatusBadge::Level::Danger);
    } else recBadge.set("REC IDLE", StatusBadge::Level::Off);

    if (m.panicActive.load()) modeBadge.set("PANIC", StatusBadge::Level::Danger);
    else if (m.bypassActive.load()) modeBadge.set("BYPASSED", StatusBadge::Level::Warn);
    else if (m.rehearsalActive.load()) modeBadge.set("REHEARSAL", StatusBadge::Level::Warn);
    else if (m.soundcheckActive.load()) modeBadge.set("SOUNDCHECK", StatusBadge::Level::Warn);
    else modeBadge.set("LIVE", StatusBadge::Level::Ok);

    if (messageTicks > 0 && --messageTicks == 0) messageLabel.setText({}, juce::dontSendNotification);
}

// ================================================================= NavRail
NavRail::NavRail(EditorContext& c) : ctx(c) {
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        auto* b = buttons.add(new juce::TextButton(juce::String(i + 1) + "  " + pageName(static_cast<PageId>(i))));
        b->setClickingTogglesState(false);
        b->setTooltip(juce::String(pageName(static_cast<PageId>(i))) + " page (F" + juce::String(i + 1) + ")");
        b->setTitle(pageName(static_cast<PageId>(i)));
        b->onClick = [this, i] { if (ctx.navigate) ctx.navigate(static_cast<PageId>(i)); };
        addAndMakeVisible(b);
    }
}
void NavRail::setCurrent(PageId id) { for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState(i == static_cast<int>(id), juce::dontSendNotification); }
void NavRail::resized() {
    auto r = getLocalBounds().reduced(6);
    const int h = juce::jmin(46, r.getHeight() / buttons.size());
    for (auto* b : buttons) b->setBounds(r.removeFromTop(h).reduced(0, 2));
}

// ================================================================= PerformanceStrip
PerformanceStrip::PerformanceStrip(EditorContext& c)
    : ctx(c), proc(c.proc),
      masterLevel(c.proc, ParamBank::index(Mod::Master, 0, MasterP::Level), juce::Slider::LinearHorizontal, 64),
      masterMute(c.proc, ParamBank::index(Mod::Master, 0, MasterP::Mute), "MASTER MUTE"),
      freeze(c.proc, ParamBank::index(Mod::Global, 0, GlobalP::Freeze), "FREEZE / HOLD"),
      echoThrow(c.proc, ParamBank::index(Mod::FxEcho, 0, EchoP::Trigger), "ECHO THROW"),
      reverseTrigger(c.proc, ParamBank::index(Mod::FxReverse, 0, ReverseP::Trigger), "REVERSE"),
      tailKill(c.proc, ParamBank::index(Mod::FxEcho, 0, EchoP::TailKill), "TAIL KILL") {
    masterLabel.setText("CROWD LEVEL", juce::dontSendNotification);
    styleLabel(masterLabel, 11.5f, true, palette().textMuted);
    addAndMakeVisible(masterLabel);
    addAndMakeVisible(masterLevel);
    addAndMakeVisible(masterMute);
    addAndMakeVisible(freeze);
    for (int m = 0; m < kNumMacros; ++m) {
        auto* s = macros.add(new ParamSlider(proc, ParamBank::index(Mod::Macro, m, 0), juce::Slider::LinearHorizontal, 40));
        s->setNumDecimalPlacesToDisplay(2);
        auto* l = macroLabels.add(new juce::Label());
        l->setText(proc.getShow().macroNames[static_cast<size_t>(m)], juce::dontSendNotification);
        styleLabel(*l, 11.0f, false, palette().textMuted);
        addAndMakeVisible(s);
        addAndMakeVisible(l);
    }
    for (auto* b : { static_cast<juce::Button*>(&echoThrow), static_cast<juce::Button*>(&reverseTrigger), static_cast<juce::Button*>(&tailKill) }) addAndMakeVisible(*b);
    // TAIL KILL is the panic for every tail: it drives the echo AND the stadium reverb tail-kill parameters (key K does the same).
    tailKill.setTooltip("Tail kill (momentary): fades out echo throws and the stadium reverb tail while held (K)");
    tailKill.onStateChange = [this] {
        if (auto* p = proc.getBank().parameterAt(ParamBank::index(Mod::FxReverb, 0, ReverbP::TailKill))) {
            const bool down = tailKill.isDown();
            if (down != (p->getValue() >= 0.5f)) {
                if (down) p->beginChangeGesture();
                p->setValueNotifyingHost(down ? 1.0f : 0.0f);
                if (!down) p->endChangeGesture();
            }
        }
    };
    marker.setTooltip("Drop a recording marker (Ctrl+M)");
    marker.onClick = [this] { proc.addMarker(5); ctx.say("Marker added"); };
    record.setClickingTogglesState(false);
    record.setTooltip("Start / stop recording (Ctrl+R)");
    record.onClick = [this] { if (proc.isRecording()) proc.stopRecording(); else if (!proc.startRecording()) ctx.say("Cannot start recording: " + proc.getRecorder().getStatus().lastError, true); };
    panicBtn.setColour(juce::TextButton::buttonColourId, palette().danger.darker(0.4f));
    panicBtn.setTooltip("Panic (F12): mute reinforcement outputs");
    panicBtn.onClick = [this] {
        auto* p = proc.getParameters().getParameter(ParamBank::paramId(Mod::Global, 0, GlobalP::Panic));
        proc.getHub().setPanic(p == nullptr || p->getValue() < 0.5f);
    };
    for (auto* b : { &marker, &record, &panicBtn }) addAndMakeVisible(*b);
    marker.setTitle("Add marker");
    record.setTitle("Start recording");
    record.setButtonText(juce::String::fromUTF8("\xe2\x97\x8f REC"));
    panicBtn.setTitle("Panic");
    c.addListener(this);
    tick();
}
PerformanceStrip::~PerformanceStrip() { ctx.removeListener(this); }
void PerformanceStrip::tick() {
    const bool recording = proc.isRecording();
    if (record.getToggleState() != recording) {
        record.setToggleState(recording, juce::dontSendNotification);
        record.setButtonText(recording ? juce::String::fromUTF8("\xe2\x96\xa0 STOP") : juce::String::fromUTF8("\xe2\x97\x8f REC"));
        record.setTitle(recording ? "Stop recording" : "Start recording");
    }
}
void PerformanceStrip::showModelChanged() { for (int m = 0; m < macroLabels.size(); ++m) macroLabels[m]->setText(proc.getShow().macroNames[static_cast<size_t>(m)], juce::dontSendNotification); }

void PerformanceStrip::resized() {
    auto r = getLocalBounds().reduced(8, 6);
    auto left = r.removeFromLeft(260);
    masterLabel.setBounds(left.removeFromTop(16));
    masterLevel.setBounds(left.removeFromTop(26));
    masterMute.setBounds(left.removeFromTop(26).removeFromLeft(130));
    r.removeFromLeft(8);
    auto right = r.removeFromRight(330);
    auto top = right.removeFromTop(right.getHeight() / 2).reduced(0, 2);
    echoThrow.setBounds(top.removeFromLeft(104)); top.removeFromLeft(4);
    reverseTrigger.setBounds(top.removeFromLeft(104)); top.removeFromLeft(4);
    tailKill.setBounds(top);
    auto bottom = right.reduced(0, 2);
    freeze.setBounds(bottom.removeFromLeft(104)); bottom.removeFromLeft(4);
    marker.setBounds(bottom.removeFromLeft(70)); bottom.removeFromLeft(4);
    record.setBounds(bottom.removeFromLeft(60)); bottom.removeFromLeft(4);
    panicBtn.setBounds(bottom);
    r.removeFromRight(8);
    const int cols = 4, w = r.getWidth() / cols, h = r.getHeight() / 2;
    for (int m = 0; m < macros.size(); ++m) {
        auto cell = juce::Rectangle<int>(r.getX() + (m % cols) * w, r.getY() + (m / cols) * h, w, h).reduced(4, 0);
        macroLabels[m]->setBounds(cell.removeFromTop(14));
        macros[m]->setBounds(cell);
    }
}

// ================================================================= Inspector
struct Inspector::Content final : public juce::Component {
    explicit Content(CrowdMikeAudioProcessor& p) : proc(p) {}
    CrowdMikeAudioProcessor& proc;
    juce::OwnedArray<ParamPanel> panels;
    juce::Label hint;
    int width = 280;
    void add(const juce::String& title, const std::vector<int>& idx, bool rotary = false) {
        auto* panel = panels.add(new ParamPanel(proc));
        panel->setCellSize(width - 12, 40);
        panel->setParams(idx, title, rotary);
        addAndMakeVisible(panel);
    }
    int layoutFor(int w) {
        width = w;
        int y = 0;
        for (auto* p : panels) {
            p->setCellSize(w - 12, 40);
            const int h = p->getPreferredHeight(w);
            p->setBounds(0, y, w, h);
            y += h + 6;
        }
        if (hint.isVisible()) hint.setBounds(8, 8, w - 16, 80);
        return juce::jmax(y, 100);
    }
};

Inspector::Inspector(EditorContext& c) : ctx(c), proc(c.proc), content(std::make_unique<Content>(c.proc)) {
    header.setFont(font(15.0f, true));
    header.setColour(juce::Label::textColourId, palette().accent);
    addAndMakeVisible(header);
    addAndMakeVisible(presetButton);
    presetButton.setTooltip("Load a preset into the selected item");
    presetButton.onClick = [this] {
        const auto sel = ctx.getSelection();
        Mod mod = Mod::Input; juce::String target;
        switch (sel.kind) {
            case Selection::Kind::Input: mod = Mod::Input; target = "input"; break;
            case Selection::Kind::Group: mod = Mod::Group; target = "group"; break;
            case Selection::Kind::Master: mod = Mod::Master; target = "master"; break;
            default: return;
        }
        juce::PopupMenu menu;
        const auto list = PresetManager::factoryPresetsFor(target);
        for (size_t i = 0; i < list.size(); ++i) menu.addItem(static_cast<int>(i) + 1, list[i]->name);
        const int inst = sel.index;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetButton), [this, list, mod, inst](int r) {
            if (r > 0) { proc.getUndoManager().beginNewTransaction("Apply preset"); PresetManager::applyFactory(proc.getParameters(), *list[static_cast<size_t>(r - 1)], mod, inst); }
        });
    };
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    c.addListener(this);
    rebuild();
}
Inspector::~Inspector() { ctx.removeListener(this); viewport.setViewedComponent(nullptr, false); }

void Inspector::rebuild() {
    auto fresh = std::make_unique<Content>(proc);
    const auto sel = ctx.getSelection();
    const auto& show = proc.getShow();
    const int i = sel.index;
    using K = Selection::Kind;
    juce::String title = "Inspector";
    presetButton.setVisible(sel.kind == K::Input || sel.kind == K::Group || sel.kind == K::Master);
    switch (sel.kind) {
        case K::Input:
            title = show.inputDisplayName(i);
            fresh->add("Channel", paramRange(Mod::Input, i, InputP::Trim, 6));
            fresh->add("Filters", paramRange(Mod::Input, i, InputP::HpOn, 10));
            fresh->add("EQ (4 band)", paramRange(Mod::Input, i, InputP::Eq1On, 20));
            fresh->add("Phase / Align", paramRange(Mod::Input, i, InputP::PhaseStages, 3));
            fresh->add("Gate / Expander", paramRange(Mod::Input, i, InputP::GateOn, GateP::Count));
            fresh->add("De-esser", paramRange(Mod::Input, i, InputP::DeEssOn, DeEssP::Count));
            fresh->add("Compressor", paramRange(Mod::Input, i, InputP::InCompOn, CompP::Count));
            fresh->add("Transient", paramRange(Mod::Input, i, InputP::TransOn, TransP::Count));
            fresh->add("Riding / FX sends", paramRange(Mod::Input, i, InputP::Riding, InputP::Count - InputP::Riding));
            break;
        case K::Group:
            title = show.groupDisplayName(i);
            fresh->add("Fader", paramRange(Mod::Group, i, GroupP::Trim, 6));
            fresh->add("Filters", paramRange(Mod::Group, i, GroupP::HpOn, 4));
            fresh->add("EQ (4 band)", paramRange(Mod::Group, i, GroupP::Eq1On, 20));
            fresh->add("Compressor", paramRange(Mod::Group, i, GroupP::GrpCompOn, CompP::Count));
            fresh->add("Limiter", paramRange(Mod::Group, i, GroupP::GrpLimOn, LimP::Count));
            fresh->add("Width / Sends", paramRange(Mod::Group, i, GroupP::Width, GroupP::Count - GroupP::Width));
            break;
        case K::Output:
            title = show.outputDisplayName(i);
            fresh->add("Output", paramIndices(Mod::Output, i));
            break;
        case K::Master:
            title = "Master crowd bus";
            fresh->add("Master", paramRange(Mod::Master, 0, MasterP::Level, 3));
            fresh->add("EQ (8 band)", paramRange(Mod::Master, 0, MasterP::Eq1On, 40));
            fresh->add("Glue compressor", paramRange(Mod::Master, 0, MasterP::MstCompOn, CompP::Count));
            fresh->add("Limiter", paramRange(Mod::Master, 0, MasterP::MstLimOn, LimP::Count));
            fresh->add("Width", paramRange(Mod::Master, 0, MasterP::WidthOn, WidthP::Count));
            break;
        case K::Fx:
            switch (i) {
                case 0: title = "Digital delay"; fresh->add("Delay", paramIndices(Mod::FxDelay, 0)); break;
                case 1: title = "Echo throw"; fresh->add("Echo", paramIndices(Mod::FxEcho, 0)); break;
                case 2: title = "Tape delay"; fresh->add("Tape", paramIndices(Mod::FxTape, 0)); break;
                case 3: title = "Reverse chunk"; fresh->add("Reverse", paramIndices(Mod::FxReverse, 0)); break;
                case 4: title = "Noise enhancer"; fresh->add("Noise", paramIndices(Mod::FxNoise, 0)); break;
                case 5: title = "Stadium reverb"; fresh->add("Reverb", paramIndices(Mod::FxReverb, 0)); break;
                default: title = "Crowd multiplier"; fresh->add("Multiplier", paramIndices(Mod::FxMulti, 0)); break;
            }
            break;
        default:
            fresh->hint.setText("Select an input, group, output or effect to edit its parameters here.", juce::dontSendNotification);
            fresh->hint.setFont(font(13.0f));
            fresh->hint.setColour(juce::Label::textColourId, palette().textMuted);
            fresh->hint.setJustificationType(juce::Justification::topLeft);
            fresh->addAndMakeVisible(fresh->hint);
            break;
    }
    header.setText(title, juce::dontSendNotification);
    viewport.setViewedComponent(nullptr, false);
    content = std::move(fresh);
    viewport.setViewedComponent(content.get(), false);
    resized();
}

void Inspector::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).reduced(8, 3);
    if (presetButton.isVisible()) presetButton.setBounds(top.removeFromRight(70));
    header.setBounds(top);
    viewport.setBounds(r);
    const int w = r.getWidth() - viewport.getScrollBarThickness();
    content->setSize(w, content->layoutFor(w));
}
}
