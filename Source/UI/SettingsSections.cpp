#include "SettingsSections.h"
#include "UI/LiveStrips.h"
#include "UI/ShowFiles.h"

namespace crowdmike::ui {
namespace {
const juce::String kCross = juce::String::fromUTF8("\xe2\x9c\x95 ");
const juce::String kDot = juce::String::fromUTF8("\xe2\x97\x8f ");
const juce::String kRing = juce::String::fromUTF8("\xe2\x97\x8b ");
void muted(juce::Label& l, float h = 12.0f) { styleLiveLabel(l, h, false, palette().textMuted); }
void sub(juce::Label& l, const juce::String& t) { styleLiveLabel(l, 12.0f, true, palette().textMuted); l.setText(t.toUpperCase(), juce::dontSendNotification); }
}

// ================================================================= base
SettingsSection::SettingsSection(EditorContext& c, const juce::String& t) : ctx(c), proc(c.proc), heading(t) { setTitle(t); }
int SettingsSection::rowH() const { return juce::roundToInt(26.0f * scale()); }
void SettingsSection::paint(juce::Graphics& g) {
    const auto& p = palette();
    g.setColour(p.panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 5.0f);
    g.setColour(p.panelEdge);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 5.0f, 1.0f);
    g.setColour(p.accent);
    g.setFont(font(14.0f, true));
    g.drawText(heading.toUpperCase(), getLocalBounds().reduced(10, 0).removeFromTop(kHeader), juce::Justification::centredLeft);
}

// ================================================================= Audio
AudioSettingsSection::AudioSettingsSection(EditorContext& c) : SettingsSection(c, "Audio I/O") {
    styleLiveLabel(status, 12.5f);
    status.setFont(monoFont(12.0f));
    status.setJustificationType(juce::Justification::topLeft);
    muted(note, 12.5f);
    note.setJustificationType(juce::Justification::topLeft);
    sub(inTitle, "Engine inputs <- device channels");
    sub(outTitle, "Engine outputs -> device channels");
    openDevice.setTooltip("Choose audio driver, device, sample rate and buffer size");
    openDevice.onClick = [this] { if (auto* hs = proc.getHostServices()) hs->showDeviceSettings(); };
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &status, &note, &inTitle, &outTitle, &openDevice }) addAndMakeVisible(comp);
    refresh();
}

void AudioSettingsSection::rebuildRows() {
    rows.clear();
    auto* hs = proc.getHostServices();
    if (hs == nullptr) return;
    const auto make = [this](bool input, int count) {
        for (int k = 0; k < count; ++k) {
            auto* r = rows.add(new BindingRow());
            r->isInput = input;
            r->engineChannel = k;
            styleLiveLabel(r->name, 12.0f);
            styleLiveLabel(r->state, 11.5f, true);
            const juce::String what = (input ? "Engine input " : "Engine output ") + juce::String(k + 1);
            r->choice.setTitle(what + " device channel");
            r->choice.setTooltip(what + ": physical device channel (stored per machine, not in the show)");
            r->choice.onChange = [this, r] {
                auto* h = proc.getHostServices();
                if (h == nullptr) return;
                const int id = r->choice.getSelectedId();
                if (id >= 1000) return;   // the missing device channel: nothing to do
                const auto names = r->isInput ? h->getInputChannelNames() : h->getOutputChannelNames();
                const juce::String phys = id >= 2 ? names[id - 2] : juce::String();
                if (r->isInput) h->setInputBinding(r->engineChannel, phys); else h->setOutputBinding(r->engineChannel, phys);
                refresh();
            };
            addAndMakeVisible(r->name);
            addAndMakeVisible(r->choice);
            addAndMakeVisible(r->state);
        }
    };
    make(true, numIn);
    make(false, numOut);
}

void AudioSettingsSection::updateRow(BindingRow& r, const DeviceBinding& b, const juce::StringArray& names) {
    auto& show = proc.getShow();
    r.name.setText(r.isInput ? juce::String(r.engineChannel + 1) + ": " + show.inputDisplayName(r.engineChannel)
                             : juce::String(r.engineChannel + 1) + ": " + show.outputDisplayName(r.engineChannel), juce::dontSendNotification);
    r.choice.clear(juce::dontSendNotification);
    r.choice.addItem("(unassigned)", 1);
    for (int k = 0; k < names.size(); ++k) r.choice.addItem(names[k], k + 2);
    const int idx = b.physicalName.isNotEmpty() ? names.indexOf(b.physicalName) : -1;
    const bool unresolved = b.physicalName.isNotEmpty() && (!b.resolved || idx < 0);
    if (unresolved) { r.choice.addItem(b.physicalName + " (missing)", 1000); r.choice.setSelectedId(1000, juce::dontSendNotification); }
    else r.choice.setSelectedId(idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
    if (unresolved) { r.state.setText(kCross + "UNRESOLVED - disconnected (safe)", juce::dontSendNotification); r.state.setColour(juce::Label::textColourId, palette().danger); }
    else if (b.physicalName.isEmpty()) { r.state.setText(kRing + "unassigned (silent)", juce::dontSendNotification); r.state.setColour(juce::Label::textColourId, palette().textMuted); }
    else { r.state.setText(kDot + "linked", juce::dontSendNotification); r.state.setColour(juce::Label::textColourId, palette().ok); }
}

void AudioSettingsSection::refresh() {
    auto* hs = proc.getHostServices();
    const bool hosted = hs == nullptr;
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &status, &inTitle, &outTitle, &openDevice }) comp->setVisible(!hosted);
    note.setVisible(hosted);
    if (hosted) {
        note.setText("Host-controlled I/O: the DAW/host owns the audio device and channel layout.\n"
                     "Active layout: " + juce::String(proc.getActiveInputChannelCount()) + " inputs / "
                     + juce::String(proc.getActiveOutputChannelCount()) + " outputs. Change the track/bus layout in the host.",
                     juce::dontSendNotification);
        if (!rows.isEmpty()) { rows.clear(); numIn = numOut = 0; if (onLayoutChanged) onLayoutChanged(); }
        return;
    }
    const auto st = hs->getDeviceStatus();
    juce::String s;
    s << (st.deviceLost ? kCross + "DEVICE LOST - audio paused until it returns" : (st.deviceOpen ? kDot + "Device open" : kCross + "No device open")) << "\n"
      << (st.typeName.isNotEmpty() ? st.typeName + " / " : juce::String()) << (st.deviceName.isNotEmpty() ? st.deviceName : juce::String("(none)")) << "\n"
      << juce::String(st.sampleRate / 1000.0, 1) << " kHz, buffer " << st.bufferSize << ", latency in " << st.inputLatencySamples
      << " / out " << st.outputLatencySamples << " smp, xruns " << st.xruns << "\n"
      << "Device channels: " << st.numDeviceInputs << " in / " << st.numDeviceOutputs << " out";
    if (st.error.isNotEmpty()) s << "\nError: " << st.error;
    status.setText(s, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, st.deviceLost || !st.deviceOpen ? palette().danger : palette().text);
    const auto inB = hs->getInputBindings();
    const auto outB = hs->getOutputBindings();
    if (static_cast<int>(inB.size()) != numIn || static_cast<int>(outB.size()) != numOut) {
        numIn = static_cast<int>(inB.size());
        numOut = static_cast<int>(outB.size());
        rebuildRows();
        if (onLayoutChanged) onLayoutChanged();
    }
    const auto inNames = hs->getInputChannelNames();
    const auto outNames = hs->getOutputChannelNames();
    for (auto* r : rows) {
        const auto& list = r->isInput ? inB : outB;
        if (r->engineChannel < static_cast<int>(list.size())) updateRow(*r, list[static_cast<size_t>(r->engineChannel)], r->isInput ? inNames : outNames);
    }
    resized();
}

int AudioSettingsSection::preferredHeight(int w) const {
    if (proc.getHostServices() == nullptr) return kHeader + 3 * rowH() + 10;
    const bool side = w >= 760;
    const int tables = side ? juce::jmax(numIn, numOut) * rowH() + rowH() : (numIn + numOut) * rowH() + 2 * rowH();
    return kHeader + juce::roundToInt(5 * 15.0f * scale()) + rowH() / 2 + 8 + tables + 12;
}

void AudioSettingsSection::resized() {
    auto r = getLocalBounds().reduced(10, 0);
    r.removeFromTop(kHeader);
    const int rh = rowH();
    if (proc.getHostServices() == nullptr) { note.setBounds(r.removeFromTop(3 * rh)); return; }
    auto top = r.removeFromTop(juce::roundToInt(5 * 15.0f * scale()));
    openDevice.setBounds(top.removeFromRight(juce::jmin(220, top.getWidth() / 3)).removeFromTop(rh + 4));
    status.setBounds(top);
    r.removeFromTop(rh / 2);
    const bool side = getWidth() >= 760;
    auto inArea = side ? r.removeFromLeft(r.getWidth() / 2 - 6) : r.removeFromTop(rh * (numIn + 1));
    if (side) r.removeFromLeft(12);
    const auto layoutTable = [&](juce::Rectangle<int> a, juce::Label& title, bool input) {
        title.setBounds(a.removeFromTop(rh));
        for (auto* row : rows) {
            if (row->isInput != input) continue;
            auto line = a.removeFromTop(rh).reduced(0, 2);
            row->name.setBounds(line.removeFromLeft(line.getWidth() * 28 / 100));
            row->state.setBounds(line.removeFromRight(line.getWidth() * 45 / 100).withTrimmedLeft(6));
            row->choice.setBounds(line);
        }
    };
    layoutTable(inArea, inTitle, true);
    layoutTable(r, outTitle, false);
}

// ================================================================= MIDI
MidiSettingsSection::MidiSettingsSection(EditorContext& c) : SettingsSection(c, "MIDI") {
    sub(inputsTitle, "MIDI inputs");
    muted(inputsNote);
    sub(bindingsTitle, "MIDI bindings (learned)");
    styleLiveLabel(lastMidi, 12.0f);
    lastMidi.setFont(monoFont(12.0f));
    lastMidi.setTooltip("Most recent MIDI message received (any input)");
    muted(emptyNote);
    emptyNote.setText("No bindings. Right-click any control and choose MIDI Learn.", juce::dontSendNotification);
    cancelLearn.setTooltip("Stop waiting for a MIDI message (MIDI learn)");
    cancelLearn.onClick = [this] { proc.getHub().cancelLearn(); tick(); };
    learn.setTitle("MIDI learn state");
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &inputsTitle, &inputsNote, &bindingsTitle, &lastMidi, &emptyNote, &learn, &cancelLearn })
        addAndMakeVisible(comp);
    refresh();
}

juce::String MidiSettingsSection::describeBinding(CrowdMikeAudioProcessor& p, const ControlBinding& b) {
    juce::String src = b.type == MidiMsgType::CC ? "CC " + juce::String(b.number)
                     : (b.type == MidiMsgType::NoteOnOff ? "Note " + juce::String(b.number) : juce::String("PitchBend"));
    src << (b.channel == 0 ? " (omni)" : " ch" + juce::String(b.channel));
    static const char* targets[] = { "", "Scene select", "Next scene", "Previous scene", "Record start", "Record stop", "Record toggle",
                                     "Record marker", "Split file", "Clip trigger", "Clip stop", "Stop all clips", "Panic", "Macro" };
    static const char* modes[] = { "absolute", "toggle", "momentary", "trigger" };
    juce::String dst;
    const int t = static_cast<int>(b.target);
    if (b.target == BindTarget::Param) {
        auto* param = p.getBank().parameterAt(b.targetIndex);
        dst = param != nullptr ? param->getName(40) : "param #" + juce::String(b.targetIndex);
    } else if (t > 0 && t < 14) {
        dst = targets[t];
        if (b.target == BindTarget::SceneSelect || b.target == BindTarget::ClipTrigger || b.target == BindTarget::ClipStop || b.target == BindTarget::MacroValue)
            dst << " " << (b.targetIndex + 1);
    }
    return src + juce::String::fromUTF8(" \xe2\x86\x92 ") + dst + " [" + modes[juce::jlimit(0, 3, static_cast<int>(b.mode))] + "]";
}

void MidiSettingsSection::refresh() {
    auto* hs = proc.getHostServices();
    inputToggles.clear();
    if (hs != nullptr) {
        const auto names = hs->getMidiInputNames();
        for (const auto& n : names) {
            auto* t = inputToggles.add(new juce::ToggleButton(n));
            t->setToggleState(hs->isMidiInputEnabled(n), juce::dontSendNotification);
            t->setTooltip("Receive MIDI from " + n);
            t->onClick = [this, t, n] { if (auto* h = proc.getHostServices()) h->setMidiInputEnabled(n, t->getToggleState()); };
            addAndMakeVisible(t);
        }
        inputsNote.setText(names.isEmpty() ? "No MIDI input devices found." : juce::String(), juce::dontSendNotification);
    } else {
        inputsNote.setText("Plug-in: MIDI arrives on the host track routed to CrowdMike.", juce::dontSendNotification);
    }
    bindingRows.clear();
    auto& binds = proc.getShow().bindings;
    for (size_t k = 0; k < binds.size(); ++k) {
        auto* row = bindingRows.add(new BindingRow());
        styleLiveLabel(row->text, 12.0f);
        row->text.setText(describeBinding(proc, binds[k]), juce::dontSendNotification);
        row->remove.setTooltip("Remove this MIDI binding");
        row->remove.setTitle("Remove binding " + juce::String(static_cast<int>(k) + 1));
        row->remove.onClick = [this, k] {
            auto& b = proc.getShow().bindings;
            if (k < b.size()) b.erase(b.begin() + static_cast<std::ptrdiff_t>(k));
            ctx.showModelChanged();
            refresh();
        };
        addAndMakeVisible(row->text);
        addAndMakeVisible(row->remove);
    }
    emptyNote.setVisible(binds.empty());
    bindingSignature = juce::String(static_cast<int>(binds.size()));
    for (const auto& b : binds) bindingSignature << ":" << b.number << "/" << b.targetIndex;
    tick();
    if (onLayoutChanged) onLayoutChanged();
    resized();
}

void MidiSettingsSection::tick() {
    auto& hub = proc.getHub();
    const bool learning = hub.isLearning();
    learn.set(learning ? "MIDI LEARN ACTIVE - move a control" : "MIDI learn idle", learning ? StatusBadge::Level::Warn : StatusBadge::Level::Off);
    cancelLearn.setEnabled(learning);
    const int num = hub.lastMidiNumber.load();
    lastMidi.setText(num < 0 ? "Last MIDI: (none received)"
                             : "Last MIDI: #" + juce::String(num) + " = " + juce::String(hub.lastMidiValue.load()) + "  ch " + juce::String(hub.lastMidiChannel.load()),
                     juce::dontSendNotification);
    if (++tickCount % 10 == 0 && isVisible()) {   // pick up bindings learned elsewhere
        juce::String sig(static_cast<int>(proc.getShow().bindings.size()));
        for (const auto& b : proc.getShow().bindings) sig << ":" << b.number << "/" << b.targetIndex;
        if (sig != bindingSignature) refresh();
    }
}

int MidiSettingsSection::preferredHeight(int) const {
    const int rh = rowH();
    return kHeader + rh * (2 + juce::jmax(1, inputToggles.size())) + rh * 2 + rh * juce::jmax(1, bindingRows.size()) + rh + 16;
}

void MidiSettingsSection::resized() {
    auto r = getLocalBounds().reduced(10, 0);
    r.removeFromTop(kHeader);
    const int rh = rowH();
    auto lr = r.removeFromTop(rh).reduced(0, 2);
    cancelLearn.setBounds(lr.removeFromRight(120));
    lr.removeFromRight(6);
    learn.setBounds(lr.removeFromRight(juce::jmin(260, lr.getWidth() / 2)));
    lastMidi.setBounds(lr);
    inputsTitle.setBounds(r.removeFromTop(rh));
    if (inputToggles.isEmpty()) inputsNote.setBounds(r.removeFromTop(rh));
    else { inputsNote.setBounds({}); for (auto* t : inputToggles) t->setBounds(r.removeFromTop(rh).reduced(0, 2).removeFromLeft(juce::jmin(360, r.getWidth()))); }
    r.removeFromTop(rh / 2);
    bindingsTitle.setBounds(r.removeFromTop(rh));
    if (bindingRows.isEmpty()) emptyNote.setBounds(r.removeFromTop(rh));
    for (auto* row : bindingRows) {
        auto line = r.removeFromTop(rh).reduced(0, 2);
        row->remove.setBounds(line.removeFromRight(80));
        row->text.setBounds(line);
    }
}

// ================================================================= OSC
OscSettingsSection::OscSettingsSection(EditorContext& c) : SettingsSection(c, "OSC") {
    enable.setTooltip("Accept OSC control messages (standalone app)");
    localhost.setTooltip("Only accept OSC from this computer (127.0.0.1)");
    enable.onClick = [this] { apply(); };
    localhost.onClick = [this] { apply(); };
    muted(portLabel); portLabel.setText("UDP port", juce::dontSendNotification);
    muted(bindLabel); bindLabel.setText("Bind address", juce::dontSendNotification);
    port.setInputRestrictions(5, "0123456789");
    port.setTitle("OSC UDP port");
    port.setTooltip("UDP port 1024..65535 (Enter to apply)");
    port.onReturnKey = [this] { apply(); };
    port.onFocusLost = [this] { apply(); };
    bindAddress.setTitle("OSC bind address");
    bindAddress.setTooltip("Interface address to bind (empty = all interfaces, or localhost when Localhost only is on)");
    bindAddress.setTextToShowWhenEmpty("all interfaces", palette().textMuted);
    bindAddress.onReturnKey = [this] { apply(); };
    bindAddress.onFocusLost = [this] { apply(); };
    styleLiveLabel(status, 12.0f, true);
    sub(sheetTitle, "Address cheat-sheet");
    styleLiveLabel(sheet, 11.5f);
    sheet.setFont(monoFont(11.5f));
    sheet.setJustificationType(juce::Justification::topLeft);
    sheet.setText(cheatSheet(), juce::dontSendNotification);
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &enable, &localhost, &portLabel, &bindLabel, &port, &bindAddress, &status, &sheetTitle, &sheet })
        addAndMakeVisible(comp);
    refresh();
}

juce::String OscSettingsSection::cheatSheet() {
    return "/crowdmike/master/level <dB>        /crowdmike/input/1/mute <0|1>\n"
           "/crowdmike/group/2/trim <dB>        /crowdmike/output/3/delay <ms>\n"
           "/crowdmike/fx/reverse/trigger       /crowdmike/fx/delay/feedback <0..0.95>\n"
           "/crowdmike/scene/3/select           /crowdmike/scene/next | prev\n"
           "/crowdmike/record/start|stop|toggle|marker|split\n"
           "/crowdmike/clip/4/trigger|stop      /crowdmike/clips/stop\n"
           "/crowdmike/macro/2 <0..1>           /crowdmike/panic [0|1]\n"
           "/crowdmike/param/<param.id> <value> /crowdmike/paramnorm/<param.id> <0..1>";
}

void OscSettingsSection::refresh() {
    const auto& o = proc.getShow().osc;
    enable.setToggleState(o.enabled, juce::dontSendNotification);
    localhost.setToggleState(o.localhostOnly, juce::dontSendNotification);
    port.setText(juce::String(o.port), false);
    bindAddress.setText(o.bindAddress, false);
    bindAddress.setEnabled(!o.localhostOnly);
    updateStatus();
}

void OscSettingsSection::apply() {
    auto& o = proc.getShow().osc;
    const int newPort = juce::jlimit(1024, 65535, port.getText().getIntValue() > 0 ? port.getText().getIntValue() : o.port);
    const bool changed = o.enabled != enable.getToggleState() || o.localhostOnly != localhost.getToggleState()
                      || o.port != newPort || o.bindAddress != bindAddress.getText().trim();
    if (!changed) { port.setText(juce::String(o.port), false); return; }
    o.enabled = enable.getToggleState();
    o.localhostOnly = localhost.getToggleState();
    o.port = newPort;
    o.bindAddress = bindAddress.getText().trim();
    proc.getOsc().stop();                // restart with the new settings (applyShowChanges starts it when enabled)
    ctx.showModelChanged();
    refresh();
}

void OscSettingsSection::updateStatus() {
    auto& osc = proc.getOsc();
    const auto& o = proc.getShow().osc;
    juce::String s;
    StatusBadge::Level lvl = StatusBadge::Level::Off;
    if (proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone) s = kRing + "OSC is available in the standalone app";
    else if (osc.isRunning()) { s = kDot + "Listening on " + (o.localhostOnly ? juce::String("127.0.0.1") : (o.bindAddress.isNotEmpty() ? o.bindAddress : juce::String("all interfaces"))) + ":" + juce::String(o.port); lvl = StatusBadge::Level::Ok; }
    else if (osc.lastError.isNotEmpty() && o.enabled) { s = kCross + "Error: " + osc.lastError; lvl = StatusBadge::Level::Danger; }
    else s = kRing + "Stopped";
    s << "   messages received: " << osc.messagesReceived;
    status.setText(s, juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, lvl == StatusBadge::Level::Danger ? palette().danger : (lvl == StatusBadge::Level::Ok ? palette().ok : palette().textMuted));
}

int OscSettingsSection::preferredHeight(int) const { return kHeader + rowH() * 4 + juce::roundToInt(8 * 16 * scale()) + 20; }

void OscSettingsSection::resized() {
    auto r = getLocalBounds().reduced(10, 0);
    r.removeFromTop(kHeader);
    const int rh = rowH();
    auto l1 = r.removeFromTop(rh).reduced(0, 2);
    enable.setBounds(l1.removeFromLeft(juce::jmin(200, l1.getWidth() / 2)));
    l1.removeFromLeft(8);
    localhost.setBounds(l1.removeFromLeft(juce::jmin(160, l1.getWidth())));
    auto l2 = r.removeFromTop(rh).reduced(0, 2);
    portLabel.setBounds(l2.removeFromLeft(70));
    port.setBounds(l2.removeFromLeft(80));
    l2.removeFromLeft(12);
    bindLabel.setBounds(l2.removeFromLeft(96));
    bindAddress.setBounds(l2.removeFromLeft(juce::jmin(200, l2.getWidth())));
    status.setBounds(r.removeFromTop(rh));
    sheetTitle.setBounds(r.removeFromTop(rh));
    sheet.setBounds(r.reduced(0, 2));
}

// ================================================================= UI
UiSettingsSection::UiSettingsSection(EditorContext& c) : SettingsSection(c, "Interface") {
    highContrast.setTooltip("Black background, maximum-contrast colours for bright or very dark venues");
    compact.setTooltip("Hide the inspector and tighten margins (Ctrl+I toggles the inspector)");
    tooltips.setTooltip("Show help tooltips when hovering controls");
    const auto bindToggle = [this](juce::ToggleButton& b, bool UiSettings::* field) {
        b.onClick = [this, &b, field] { proc.getShow().ui.*field = b.getToggleState(); ctx.showModelChanged(); };
        addAndMakeVisible(b);
    };
    bindToggle(highContrast, &UiSettings::highContrast);
    bindToggle(compact, &UiSettings::compact);
    bindToggle(tooltips, &UiSettings::tooltips);
    muted(scaleLabel); scaleLabel.setText("UI scale", juce::dontSendNotification);
    muted(rampLabel); rampLabel.setText("Scene recall ramp", juce::dontSendNotification);
    for (int k = 0; k < static_cast<int>(std::size(kScales)); ++k) scaleChoice.addItem(juce::String(juce::roundToInt(kScales[k] * 100.0f)) + " %", k + 1);
    scaleChoice.setTitle("UI scale");
    scaleChoice.setTooltip("Scale of the whole interface (75 % .. 200 %)");
    scaleChoice.onChange = [this] {
        const int id = scaleChoice.getSelectedId();
        if (id < 1) return;
        proc.getShow().ui.scale = kScales[id - 1];
        ctx.showModelChanged();
    };
    ramp.setSliderStyle(juce::Slider::LinearHorizontal);
    ramp.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    ramp.setRange(10.0, 500.0, 1.0);
    ramp.setTextValueSuffix(" ms");
    ramp.setTitle("Scene recall ramp");
    ramp.setTooltip("Default crossfade time when a scene is recalled (10..500 ms)");
    ramp.onValueChange = [this] { proc.getShow().ui.recallRampMs = static_cast<float>(ramp.getValue()); };
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &scaleLabel, &rampLabel, &scaleChoice, &ramp }) addAndMakeVisible(comp);
    refresh();
}

void UiSettingsSection::refresh() {
    const auto& u = proc.getShow().ui;
    highContrast.setToggleState(u.highContrast, juce::dontSendNotification);
    compact.setToggleState(u.compact, juce::dontSendNotification);
    tooltips.setToggleState(u.tooltips, juce::dontSendNotification);
    int best = 3;
    for (int k = 0; k < static_cast<int>(std::size(kScales)); ++k)
        if (std::abs(kScales[k] - u.scale) < std::abs(kScales[best - 1] - u.scale)) best = k + 1;
    scaleChoice.setSelectedId(best, juce::dontSendNotification);
    ramp.setValue(u.recallRampMs, juce::dontSendNotification);
}

int UiSettingsSection::preferredHeight(int) const { return kHeader + rowH() * 5 + 14; }

void UiSettingsSection::resized() {
    auto r = getLocalBounds().reduced(10, 0);
    r.removeFromTop(kHeader);
    const int rh = rowH();
    for (auto* b : { &highContrast, &compact, &tooltips }) b->setBounds(r.removeFromTop(rh).reduced(0, 2).removeFromLeft(juce::jmin(300, r.getWidth())));
    auto l = r.removeFromTop(rh).reduced(0, 2);
    scaleLabel.setBounds(l.removeFromLeft(130));
    scaleChoice.setBounds(l.removeFromLeft(110));
    auto l2 = r.removeFromTop(rh).reduced(0, 2);
    rampLabel.setBounds(l2.removeFromLeft(130));
    ramp.setBounds(l2.removeFromLeft(juce::jmin(320, l2.getWidth())));
}

// ================================================================= Show files
ShowFilesSection::ShowFilesSection(EditorContext& c) : SettingsSection(c, "Show files") {
    muted(info);
    info.setText("A .crowdshow file holds all parameters, routing, names, macros, MIDI/OSC settings and scenes. "
                 "Device channel bindings stay on this machine.", juce::dontSendNotification);
    info.setJustificationType(juce::Justification::topLeft);
    save.setTooltip("Save the complete show to a .crowdshow file");
    load.setTooltip("Load a .crowdshow file (replaces the current show)");
    reset.setTooltip("Reset every parameter, routing and show setting to factory defaults (asks first)");
    save.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Save show", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.crowdshow");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension(kShowFileExtension);
            juce::String err;
            if (saveShowFile(proc, f, err)) { info.setText("Saved " + f.getFullPathName(), juce::dontSendNotification); ctx.say("Show saved: " + f.getFileName()); }
            else ctx.say("Save failed: " + err, true);
        });
    };
    load.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load show", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.crowdshow");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            if (loadShowFile(proc, f, err)) {
                proc.getUndoManager().clearUndoHistory();
                info.setText("Loaded " + f.getFullPathName(), juce::dontSendNotification);
                ctx.showModelChanged();
                ctx.say("Show loaded: " + f.getFileName());
            } else ctx.say("Load failed: " + err, true);
        });
    };
    reset.onClick = [this] {
        juce::Component::SafePointer<ShowFilesSection> safe(this);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle("Reset show").withMessage("Reset all parameters, routing and show settings to defaults? (Parameter changes can be undone.)")
                                         .withButton("Reset").withButton("Cancel").withAssociatedComponent(this),
                                     [safe](int result) {
            if (safe == nullptr || result != 1) return;
            resetShowToDefaults(safe->proc);
            safe->ctx.showModelChanged();
            safe->ctx.say("Show reset to defaults");
        });
    };
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &save, &load, &reset, &info }) addAndMakeVisible(comp);
}

int ShowFilesSection::preferredHeight(int) const { return kHeader + rowH() * 3 + 14; }

void ShowFilesSection::resized() {
    auto r = getLocalBounds().reduced(10, 0);
    r.removeFromTop(kHeader);
    auto l = r.removeFromTop(rowH() + 4).reduced(0, 2);
    const int bw = juce::jmin(170, (l.getWidth() - 16) / 3);
    save.setBounds(l.removeFromLeft(bw)); l.removeFromLeft(8);
    load.setBounds(l.removeFromLeft(bw)); l.removeFromLeft(8);
    reset.setBounds(l.removeFromLeft(bw));
    info.setBounds(r.reduced(0, 2));
}

// ================================================================= Shortcuts
ShortcutsSection::ShortcutsSection(EditorContext& c) : SettingsSection(c, "Keyboard shortcuts") {
    styleLiveLabel(text, 11.5f);
    text.setFont(monoFont(11.5f));
    text.setJustificationType(juce::Justification::topLeft);
    text.setText(shortcuts().joinIntoString("\n"), juce::dontSendNotification);
    addAndMakeVisible(text);
}

juce::StringArray ShortcutsSection::shortcuts() {
    return { "F1..F10            Live, Inputs, Groups, FX, Matrix, Record, Playback, Scenes, Settings, Diagnostics",
             "F12                PANIC mute (toggle)",
             "Ctrl+Z / Ctrl+Y    Undo / redo (also Ctrl+Shift+Z)",
             "Ctrl+Left/Right    Previous / next scene",
             "Ctrl+1..9          Recall scene 1..9 (Shift: 10..18)",
             "Ctrl+R / Ctrl+M    Record start-stop / drop marker",
             "Ctrl+I             Show / hide the inspector",
             "Hold E / Hold V    Echo throw / reverse trigger (momentary)",
             "Tab / Shift+Tab    Move keyboard focus; arrows adjust the focused control",
             "Shift-drag         Fine adjustment",
             "Ctrl-click         Reset control to default",
             "Double-click       Type a value / rename a strip",
             "Right-click        MIDI learn, macro assign, lock against scene recall" };
}

int ShortcutsSection::preferredHeight(int) const { return kHeader + juce::roundToInt(static_cast<float>(shortcuts().size()) * 16.0f * scale()) + 16; }
void ShortcutsSection::resized() { text.setBounds(getLocalBounds().reduced(10, 0).withTrimmedTop(kHeader).withTrimmedBottom(6)); }
}
