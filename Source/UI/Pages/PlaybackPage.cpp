// Playback page (GUI_SPEC section 9, RECORDING_SPEC 9-12): 32 clip slots + deck editor, crowd bed, virtual soundcheck.
// Clip files / settings / hotkeys, bed and soundcheck references persist in ShowModel::playback through the processor
// (loadClipFile / setClipSettings / setBedSettings / loadSoundcheckSession ...). Engines are polled at ~10 Hz only.
#include "UI/Meters.h"
#include "UI/PageKit.h"
#include "UI/Pages.h"
#include "UI/PlaybackWidgets.h"
#include "UI/Ticker.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
namespace {
using Clip = rec::PlaybackEngine::ClipSettings;
using Bed = rec::PlaybackEngine::BedSettings;

void setupSlider(juce::Slider& s, double lo, double hi, double step, const juce::String& suffix, const juce::String& tip, const juce::String& id, double mid = 0.0) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 74, 20);
    s.setRange(lo, hi, step);
    if (mid > lo && mid < hi) s.setSkewFactorFromMidPoint(mid);
    s.setTextValueSuffix(suffix);
    s.setTooltip(tip);
    s.setTitle(tip.upToFirstOccurrenceOf(":", false, false));
    s.setComponentID(id);
    s.setWantsKeyboardFocus(true);
    s.setDoubleClickReturnValue(true, lo < 0.0 && hi > 0.0 ? 0.0 : lo);
}

void fillRoutes(juce::ComboBox& box, const ShowModel& show) {
    const int keep = box.getSelectedId();
    box.clear(juce::dontSendNotification);
    box.addItem("Master", 1);
    for (int g = 0; g < 8; ++g) box.addItem("Group " + juce::String(g + 1) + ": " + show.groupDisplayName(g), g + 2);
    box.addItem("Direct playback bus", 10);
    box.setSelectedId(keep > 0 ? keep : 1, juce::dontSendNotification);
}

struct LabeledRow {
    juce::Label label;
    juce::Component* control = nullptr;
};

// ================================================================= clip deck (selected slot)
class DeckPanel final : public juce::Component {
public:
    DeckPanel(EditorContext& c) : ctx(c), proc(c.proc) {
        title.setFont(font(15.0f, true));
        addAndMakeVisible(title);
        kit::styleLabel(info, 11.5f, false, true);
        addAndMakeVisible(info);
        nameEdit.setTitle("Clip name");
        nameEdit.setTooltip("Display name of the clip");
        nameEdit.setComponentID("play.name");
        nameEdit.setJustification(juce::Justification::centredLeft);
        nameEdit.onReturnKey = nameEdit.onFocusLost = [this] { auto s = settings(); if (s.name != nameEdit.getText()) { s.name = nameEdit.getText().substring(0, 48); commit(s); } };
        addAndMakeVisible(nameEdit);
        load.setButtonText("Load...");
        kit::setupButton(load, "Load a WAV / AIFF / FLAC file into this slot", "play.load");
        load.onClick = [this] { chooseFile(); };
        unload.setButtonText("Unload");
        kit::setupButton(unload, "Remove the clip from this slot", "play.unload");
        unload.onClick = [this] { proc.unloadClipFile(slot); ctx.say("Clip " + juce::String(slot + 1) + " unloaded"); if (onChanged) onChanged(); };
        trigger.setButtonText(juce::String::fromUTF8("\xe2\x96\xb6 Trigger"));
        kit::setupButton(trigger, "Start (or restart) the clip", "play.trigger");
        trigger.onClick = [this] { if (!proc.triggerClip(slot)) ctx.say("Trigger failed (queue full)", true); };
        stop.setButtonText(juce::String::fromUTF8("\xe2\x96\xa0 Stop"));
        kit::setupButton(stop, "Stop the clip (with its fade-out)", "play.stop");
        stop.onClick = [this] { proc.getPlayback().stop(slot); };
        wave.setComponentID("play.waveform");
        wave.onTrimChanged = [this](double s0, double e0, bool) { auto s = settings(); s.startFrac = s0; s.endFrac = e0; commit(s); };
        addAndMakeVisible(wave);

        mode.addItemList({ "One-shot", "Loop" }, 1);
        mode.setTitle("Playback mode");
        mode.setTooltip("One-shot plays the region once; Loop repeats it until stopped");
        mode.setComponentID("play.mode");
        mode.onChange = [this] { auto s = settings(); s.mode = mode.getSelectedId() == 2 ? Clip::Loop : Clip::OneShot; commit(s); };
        route.setTitle("Clip route");
        route.setTooltip("Destination bus of the clip");
        route.setComponentID("play.route");
        route.onChange = [this] { auto s = settings(); s.route = juce::jlimit(0, 9, route.getSelectedId() - 1); commit(s); };
        kit::setupToggle(reverse, "Reverse", "Play the trimmed region backwards", palette().accentDim);
        reverse.setComponentID("play.reverse");
        reverse.onClick = [this] { auto s = settings(); s.reverse = reverse.getToggleState(); kit::refreshToggleText(reverse, "Reverse"); commit(s); };
        kit::setupToggle(sync, "Sync", "Tempo sync: triggers wait for the next grid line of the host / engine tempo (immediate when no tempo is available)", palette().accentDim);
        sync.setComponentID("play.sync");
        sync.onClick = [this] { auto s = settings(); s.tempoSync = sync.getToggleState(); kit::refreshToggleText(sync, "Sync"); commit(s); };
        for (int q = 0; q < rec::PlaybackEngine::kNumQuantise; ++q) {
            static const char* const names[] = { "1/16 note", "1/8 note", "1 beat", "2 beats", "1 bar", "2 bars" };
            syncGrid.addItem(names[q], q + 1);
        }
        syncGrid.setTitle("Sync grid");
        syncGrid.setTooltip("Tempo sync grid: a trigger starts on the next multiple of this length (bar = 4 beats)");
        syncGrid.setComponentID("play.syncGrid");
        syncGrid.onChange = [this] { auto s = settings(); s.syncQuantise = syncGrid.getSelectedId() - 1; commit(s); };
        setupSlider(syncBeats, 0.0, 64.0, 0.25, " beats", "Loop beats: with Loop + Sync the loop restarts every this many beats, locked to the tempo grid (0 = natural clip length)", "play.syncBeats");
        syncBeats.onValueChange = [this] { auto s = settings(); s.syncLoopBeats = syncBeats.getValue(); commit(s); };
        setupSlider(gain, -60.0, 12.0, 0.1, " dB", "Gain: clip level", "play.gain", -12.0);
        gain.onValueChange = [this] { auto s = settings(); s.gainDb = static_cast<float>(gain.getValue()); commit(s); };
        setupSlider(fadeIn, 0.0, 10000.0, 1.0, " ms", "Fade in: ramp from each (re)trigger", "play.fadeIn", 500.0);
        fadeIn.onValueChange = [this] { auto s = settings(); s.fadeInMs = static_cast<float>(fadeIn.getValue()); commit(s); };
        setupSlider(fadeOut, 0.0, 10000.0, 1.0, " ms", "Fade out: before a one-shot end and on stop", "play.fadeOut", 500.0);
        fadeOut.onValueChange = [this] { auto s = settings(); s.fadeOutMs = static_cast<float>(fadeOut.getValue()); commit(s); };

        hotkeyButton.setButtonText("Assign key");
        kit::setupButton(hotkeyButton, "Press, then press the key that should trigger this clip (Esc cancels). "
                                       "Function keys and Ctrl shortcuts keep their global meaning; E/V are also performance keys.", "play.hotkey");
        hotkeyButton.onClick = [this] { capturingKey = true; hotkeyButton.setButtonText("Press a key..."); grabKeyboardFocus(); };
        clearKey.setButtonText("Clear key");
        kit::setupButton(clearKey, "Remove the keyboard hotkey of this clip", "play.clearKey");
        clearKey.onClick = [this] { proc.setClipHotkey(slot, {}); refresh(); };
        learnTrig.setButtonText("MIDI trigger");
        kit::setupButton(learnTrig, "MIDI learn: the next note / CC triggers this clip", "play.learnTrigger");
        learnTrig.onClick = [this] { proc.getHub().startLearn({ BindTarget::ClipTrigger, slot, BindMode::Trigger }); ctx.say("MIDI learn: move a control to trigger clip " + juce::String(slot + 1)); };
        learnStop.setButtonText("MIDI stop");
        kit::setupButton(learnStop, "MIDI learn: the next note / CC stops this clip", "play.learnStop");
        learnStop.onClick = [this] { proc.getHub().startLearn({ BindTarget::ClipStop, slot, BindMode::Trigger }); ctx.say("MIDI learn: move a control to stop clip " + juce::String(slot + 1)); };
        clearMidi.setButtonText("Clear MIDI");
        kit::setupButton(clearMidi, "Remove MIDI trigger / stop bindings of this clip", "play.clearMidi");
        clearMidi.onClick = [this] { proc.getHub().removeBindingsFor(BindTarget::ClipTrigger, slot); proc.getHub().removeBindingsFor(BindTarget::ClipStop, slot); refresh(); };
        kit::styleLabel(assignInfo, 11.5f, false, true);
        for (auto* l : { &lMode, &lGain, &lFadeIn, &lFadeOut, &lAssign, &lSync }) { kit::styleLabel(*l, 12.0f, false, true); addAndMakeVisible(*l); }
        lSync.setText("Tempo sync", juce::dontSendNotification);
        lMode.setText("Mode / route", juce::dontSendNotification);
        lGain.setText("Levels / gain", juce::dontSendNotification);
        lFadeIn.setText("Fade in", juce::dontSendNotification);
        lFadeOut.setText("Fade out", juce::dontSendNotification);
        lAssign.setText("Assign", juce::dontSendNotification);
        for (auto* c2 : std::initializer_list<juce::Component*> { &load, &unload, &trigger, &stop, &mode, &route, &reverse, &sync, &syncGrid, &syncBeats, &gain, &fadeIn, &fadeOut,
                                                                  &hotkeyButton, &clearKey, &learnTrig, &learnStop, &clearMidi, &assignInfo })
            addAndMakeVisible(c2);
        setWantsKeyboardFocus(true);
    }

    std::function<void()> onChanged;

    void setSlot(int s) { slot = s; capturingKey = false; hotkeyButton.setButtonText("Assign key"); wave.clearEnvelope(); loadedShown = false; refresh(); }
    int getSlot() const noexcept { return slot; }

    void refresh() {
        fillRoutes(route, proc.getShow());
        const auto s = settings();
        const auto inf = proc.getPlayback().getClipInfo(slot);
        title.setText("Clip " + juce::String(slot + 1), juce::dontSendNotification);
        if (!nameEdit.hasKeyboardFocus(true)) nameEdit.setText(s.name, false);
        nameEdit.setTextToShowWhenEmpty(inf.name.isNotEmpty() ? inf.name : juce::String("(name)"), palette().textMuted);
        mode.setSelectedId(s.mode == Clip::Loop ? 2 : 1, juce::dontSendNotification);
        route.setSelectedId(s.route + 1, juce::dontSendNotification);
        reverse.setToggleState(s.reverse, juce::dontSendNotification);
        sync.setToggleState(s.tempoSync, juce::dontSendNotification);
        syncGrid.setSelectedId(s.syncQuantise + 1, juce::dontSendNotification);
        syncBeats.setValue(s.syncLoopBeats, juce::dontSendNotification);
        syncGrid.setEnabled(s.tempoSync);
        syncBeats.setEnabled(s.tempoSync);
        kit::refreshToggleText(reverse, "Reverse");
        kit::refreshToggleText(sync, "Sync");
        gain.setValue(s.gainDb, juce::dontSendNotification);
        fadeIn.setValue(s.fadeInMs, juce::dontSendNotification);
        fadeOut.setValue(s.fadeOutMs, juce::dontSendNotification);
        wave.setTrim(s.startFrac, s.endFrac);
        const auto key = proc.getClipHotkey(slot);
        assignInfo.setText("Key: " + (key.isNotEmpty() ? key : juce::String("-")) + "    MIDI trigger: " + kit::bindingSummary(proc.getShow(), BindTarget::ClipTrigger, slot)
                               + "    MIDI stop: " + kit::bindingSummary(proc.getShow(), BindTarget::ClipStop, slot),
                           juce::dontSendNotification);
        missing = proc.isClipMissing(slot);
        poll();
    }

    void poll() {
        const auto inf = proc.getPlayback().getClipInfo(slot);
        const auto path = proc.getClipPath(slot);
        juce::String text;
        if (missing) text = juce::String::fromUTF8("\xe2\x9c\x95 MISSING: ") + path;
        else if (inf.loading) text = "Loading " + path;
        else if (inf.error.isNotEmpty()) text = juce::String::fromUTF8("\xe2\x9c\x95 ERROR: ") + inf.error;
        else if (inf.loaded) text = kit::formatClock(inf.durationSeconds, true) + "  " + juce::String(inf.numChannels) + " ch  " + juce::String(inf.sourceSampleRate / 1000.0, 1) + " kHz"
                                    + (inf.streaming ? "  STREAMED" : "") + (inf.syncPending ? "  WAITING FOR BEAT" : "") + "  " + path;
        else text = "Empty slot: Load a WAV, AIFF or FLAC file";
        info.setText(text, juce::dontSendNotification);
        info.setTooltip(text);
        const auto streamStats = proc.getPlayback().getStreamStats();
        if (streamStats.underrunEvents != lastUnderruns) {
            lastUnderruns = streamStats.underrunEvents;
            underrunUntilMs = juce::Time::getMillisecondCounter() + 6000;
            ctx.say("Playback stream underrun: the disk could not keep up (silence inserted)", true);
        }
        const bool underrunNow = juce::Time::getMillisecondCounter() < underrunUntilMs;
        if (underrunNow && inf.streaming) { text << "  UNDERRUN"; info.setText(text, juce::dontSendNotification); }
        info.setColour(juce::Label::textColourId, missing || inf.error.isNotEmpty() || (underrunNow && inf.streaming) ? palette().danger : palette().textMuted);
        if (inf.loaded != loadedShown) {
            loadedShown = inf.loaded;
            std::vector<float> mn(rec::PlaybackEngine::kWaveformPoints), mx(rec::PlaybackEngine::kWaveformPoints);
            if (inf.loaded && proc.getPlayback().getWaveform(slot, mn.data(), mx.data(), static_cast<int>(mn.size()))) wave.setEnvelope(mn.data(), mx.data(), static_cast<int>(mn.size()));
            else wave.clearEnvelope();
        }
        wave.setMessage(missing ? "File missing: load it again or relink" : (inf.loading ? "Loading..." : "No clip loaded"));
        wave.setPlayPosition(inf.playPosition01, inf.playing, settings().reverse);
        trigger.setEnabled(inf.loaded);
    }

    bool keyPressed(const juce::KeyPress& k) override {
        if (!capturingKey) return false;
        capturingKey = false;
        hotkeyButton.setButtonText("Assign key");
        if (k.getKeyCode() == juce::KeyPress::escapeKey) return true;
        const auto mods = k.getModifiers();
        const bool reserved = (k.getKeyCode() >= juce::KeyPress::F1Key && k.getKeyCode() <= juce::KeyPress::F12Key) || mods.isCommandDown() || mods.isCtrlDown();
        if (reserved) { ctx.say("That key is reserved for a global shortcut", true); return true; }
        proc.setClipHotkey(slot, k.getTextDescription());
        ctx.say("Clip " + juce::String(slot + 1) + " hotkey: " + k.getTextDescription());
        refresh();
        if (onChanged) onChanged();
        return true;
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 8);
        const int rowH = juce::jlimit(24, 30, r.getHeight() / 9);
        auto top = r.removeFromTop(rowH);
        title.setBounds(top.removeFromLeft(80));
        kit::layoutRow(top, { { &nameEdit, 2.0f }, { &load, -80.0f }, { &unload, -72.0f }, { &trigger, -96.0f }, { &stop, -80.0f } });
        r.removeFromTop(4);
        info.setBounds(r.removeFromTop(16));
        r.removeFromTop(4);
        const int waveH = juce::jlimit(32, 260, r.getHeight() - (rowH + 6) * 4 - 24);
        wave.setBounds(r.removeFromTop(waveH));
        r.removeFromTop(8);
        auto line = [&](juce::Label& l) { auto row = r.removeFromTop(rowH); r.removeFromTop(6); l.setBounds(row.removeFromLeft(90)); return row; };
        kit::layoutRow(line(lMode), { { &mode, 1.0f }, { &route, 1.6f }, { &reverse, -96.0f }, { &sync, -80.0f } });
        kit::layoutRow(line(lSync), { { &syncGrid, 1.0f }, { &syncBeats, 2.0f } });
        {
            auto row = line(lGain);
            const int third = (row.getWidth() - 24) / 3;
            gain.setBounds(row.removeFromLeft(third));
            row.removeFromLeft(12);
            auto fi = row.removeFromLeft(third);
            lFadeIn.setBounds(fi.removeFromLeft(56));
            fadeIn.setBounds(fi);
            row.removeFromLeft(12);
            lFadeOut.setBounds(row.removeFromLeft(62));
            fadeOut.setBounds(row);
        }
        kit::layoutRow(line(lAssign), { { &hotkeyButton, 1.0f }, { &clearKey, 1.0f }, { &learnTrig, 1.0f }, { &learnStop, 1.0f }, { &clearMidi, 1.0f } });
        assignInfo.setBounds(r.removeFromTop(16).withTrimmedLeft(90));
    }

private:
    Clip settings() const { return proc.getPlayback().getSettings(slot); }
    void commit(const Clip& s) { proc.setClipSettings(slot, s); if (onChanged) onChanged(); }

    void chooseFile() {
        chooser = std::make_unique<juce::FileChooser>("Load clip " + juce::String(slot + 1), juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<DeckPanel> safe(this);
        const int target = slot;
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, target](const juce::FileChooser& fc) {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            if (!safe->proc.loadClipFile(target, f)) safe->ctx.say("Cannot load " + f.getFileName(), true);
            else safe->ctx.say("Loading " + f.getFileName() + " into clip " + juce::String(target + 1));
            if (safe->slot == target) { safe->loadedShown = false; safe->refresh(); }
            if (safe->onChanged) safe->onChanged();
        });
    }

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    int slot = 0;
    bool capturingKey = false, loadedShown = false, missing = false;
    juce::uint64 lastUnderruns = 0;
    juce::uint32 underrunUntilMs = 0;
    juce::Label title, info, assignInfo, lMode, lGain, lFadeIn, lFadeOut, lAssign, lSync;
    juce::ComboBox syncGrid;
    juce::Slider syncBeats;
    juce::TextEditor nameEdit;
    juce::TextButton load, unload, trigger, stop, reverse, sync, hotkeyButton, clearKey, learnTrig, learnStop, clearMidi;
    ClipWaveformView wave { true };
    juce::ComboBox mode, route;
    juce::Slider gain, fadeIn, fadeOut;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ================================================================= crowd bed
class BedPanel final : public juce::Component {
public:
    explicit BedPanel(EditorContext& c) : ctx(c), proc(c.proc) {
        kit::setupToggle(enabled, "Bed enabled", "Continuous crowd bed built from random regions of the source clip", palette().ok);
        enabled.setComponentID("bed.enabled");
        enabled.onClick = [this] { commit(); };
        source.setTitle("Bed source clip");
        source.setTooltip("Clip slot used as the bed source");
        source.setComponentID("bed.slot");
        source.onChange = [this] { commit(); };
        route.setTitle("Bed route");
        route.setTooltip("Destination bus of the bed");
        route.setComponentID("bed.route");
        route.onChange = [this] { commit(); };
        setupSlider(minLoop, 1.0, 600.0, 0.5, " s", "Loop min: shortest random region", "bed.minLoop", 30.0);
        setupSlider(maxLoop, 1.0, 600.0, 0.5, " s", "Loop max: longest random region", "bed.maxLoop", 60.0);
        setupSlider(crossfade, 0.1, 30.0, 0.1, " s", "Crossfade: equal-power crossfade between regions", "bed.crossfade", 4.0);
        setupSlider(variation, 0.0, 12.0, 0.1, " dB", "Level variation: slow random drift +/-", "bed.variation");
        setupSlider(lowpass, 0.0, 20000.0, 1.0, " Hz", "Low-pass: one-pole filter, 0 = off", "bed.lowpass", 4000.0);
        setupSlider(gain, -60.0, 12.0, 0.1, " dB", "Bed gain", "bed.gain", -12.0);
        for (auto* s : { &minLoop, &maxLoop, &crossfade, &variation, &lowpass, &gain }) { s->onValueChange = [this] { commit(); }; addAndMakeVisible(*s); }
        const char* names[] = { "Source / route", "Loop min", "Loop max", "Crossfade", "Variation", "Low-pass", "Gain" };
        for (auto* n : names) { auto* l = labels.add(new juce::Label({}, n)); kit::styleLabel(*l, 12.0f, false, true); addAndMakeVisible(l); }
        kit::styleLabel(note, 11.5f, false, true);
        note.setText("The bed loops random regions of the source clip with crossfades and slow level drift. It ducks out on Panic and fades back in. Long clips are streamed from disk (regions of at least 3 s).", juce::dontSendNotification);
        for (auto* c2 : std::initializer_list<juce::Component*> { &enabled, &source, &route, &note }) addAndMakeVisible(c2);
    }

    void refresh() {
        const auto b = proc.getPlayback().getBed();
        const int keep = source.getSelectedId();
        source.clear(juce::dontSendNotification);
        for (int s = 0; s < rec::PlaybackEngine::kMaxClips; ++s) {
            const auto set = proc.getPlayback().getSettings(s);
            source.addItem("Clip " + juce::String(s + 1) + (set.name.isNotEmpty() ? ": " + set.name : juce::String()), s + 1);
        }
        juce::ignoreUnused(keep);
        source.setSelectedId(b.slot + 1, juce::dontSendNotification);
        fillRoutes(route, proc.getShow());
        route.setSelectedId(b.route + 1, juce::dontSendNotification);
        enabled.setToggleState(b.enabled, juce::dontSendNotification);
        kit::refreshToggleText(enabled, "Bed enabled");
        minLoop.setValue(b.minLoopSec, juce::dontSendNotification);
        maxLoop.setValue(b.maxLoopSec, juce::dontSendNotification);
        crossfade.setValue(b.crossfadeSec, juce::dontSendNotification);
        variation.setValue(b.levelVariationDb, juce::dontSendNotification);
        lowpass.setValue(b.lowpassHz, juce::dontSendNotification);
        gain.setValue(b.gainDb, juce::dontSendNotification);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 8);
        const int rowH = juce::jlimit(24, 30, r.getHeight() / 9);
        enabled.setBounds(r.removeFromTop(rowH).removeFromLeft(180));
        r.removeFromTop(6);
        auto twoCol = r.getWidth() > 760;
        juce::Component* ctl[] = { nullptr, &minLoop, &maxLoop, &crossfade, &variation, &lowpass, &gain };
        auto colA = twoCol ? r.removeFromLeft(r.getWidth() / 2 - 8) : r;
        if (twoCol) r.removeFromLeft(16);
        auto colB = twoCol ? r : colA;
        for (int i = 0; i < 7; ++i) {
            auto& col = (!twoCol || i < 4) ? colA : colB;
            auto row = col.removeFromTop(rowH);
            col.removeFromTop(6);
            labels[i]->setBounds(row.removeFromLeft(100));
            if (i == 0) kit::layoutRow(row, { { &source, 1.0f }, { &route, 1.0f } });
            else ctl[i]->setBounds(row);
        }
        note.setBounds(colB.removeFromTop(36));
    }

private:
    void commit() {
        Bed b = proc.getPlayback().getBed();
        b.enabled = enabled.getToggleState();
        kit::refreshToggleText(enabled, "Bed enabled");
        b.slot = juce::jmax(0, source.getSelectedId() - 1);
        b.route = juce::jlimit(0, 9, route.getSelectedId() - 1);
        b.minLoopSec = minLoop.getValue();
        b.maxLoopSec = juce::jmax(maxLoop.getValue(), b.minLoopSec);
        b.crossfadeSec = crossfade.getValue();
        b.levelVariationDb = static_cast<float>(variation.getValue());
        b.lowpassHz = static_cast<float>(lowpass.getValue());
        b.gainDb = static_cast<float>(gain.getValue());
        proc.setBedSettings(b);
        if (b.enabled && !proc.getPlayback().getClipInfo(b.slot).loaded) ctx.say("Crowd bed: clip " + juce::String(b.slot + 1) + " is empty", true);
    }

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::TextButton enabled;
    juce::ComboBox source, route;
    juce::Slider minLoop, maxLoop, crossfade, variation, lowpass, gain;
    juce::OwnedArray<juce::Label> labels;
    juce::Label note;
};

// ================================================================= virtual soundcheck
class SoundcheckPanel final : public juce::Component, private juce::ListBoxModel {
public:
    explicit SoundcheckPanel(EditorContext& c)
        : ctx(c), proc(c.proc), sourceSwitch(c.proc, ParamBank::index(Mod::Global, 0, params::GlobalP::Soundcheck), "Recorded source (soundcheck)") {
        loadButton.setButtonText("Load session...");
        kit::setupButton(loadButton, "Load a CrowdMike session JSON written by the recorder", "sc.load");
        loadButton.onClick = [this] { chooseSession(); };
        kit::styleLabel(sessionLabel, 12.0f, false, true);
        lockButton.setComponentID("sc.lock");
        kit::setupButton(lockButton, "Locked: recorded inputs can never reach the engine. Unlocking asks for confirmation.");
        lockButton.onClick = [this] { toggleLock(); };
        armButton.setComponentID("sc.arm");
        kit::setupButton(armButton, "Arm: replace the live inputs with the recorded ones (fades in over 2.5 s). Asks for confirmation.");
        armButton.onClick = [this] { toggleArm(); };
        sourceSwitch.setTooltip("Live / recorded source switch (global 'soundcheck' parameter): recorded inputs are used only when this is on AND the player is unlocked and armed");
        sourceSwitch.setComponentID("sc.source");
        play.setButtonText(juce::String::fromUTF8("\xe2\x96\xb6 Play"));
        pause.setButtonText("|| Pause");
        stopB.setButtonText(juce::String::fromUTF8("\xe2\x96\xa0 Stop"));
        kit::setupButton(play, "Play the recorded session", "sc.play");
        kit::setupButton(pause, "Pause", "sc.pause");
        kit::setupButton(stopB, "Stop (fades out, rewinds to the loop start)", "sc.stop");
        play.onClick = [this] { proc.getSoundcheck().play(); };
        pause.onClick = [this] { proc.getSoundcheck().pause(); };
        stopB.onClick = [this] { proc.getSoundcheck().stop(); };
        setupSlider(seek, 0.0, 1.0, 0.01, " s", "Position: drag to seek", "sc.seek");
        seek.onDragStart = [this] { seeking = true; };
        seek.onDragEnd = [this] { seeking = false; proc.getSoundcheck().seekSeconds(seek.getValue()); };
        seek.onValueChange = [this] { if (!seeking) proc.getSoundcheck().seekSeconds(seek.getValue()); };
        setupSlider(loopStart, 0.0, 1.0, 0.1, " s", "Loop start", "sc.loopStart");
        setupSlider(loopEnd, 0.0, 1.0, 0.1, " s", "Loop end", "sc.loopEnd");
        kit::setupToggle(loopOn, "Loop", "Loop the range between loop start and loop end", palette().accentDim);
        loopOn.setComponentID("sc.loop");
        loopStart.onValueChange = loopEnd.onValueChange = [this] { commitLoop(); };
        loopOn.onClick = [this] { kit::refreshToggleText(loopOn, "Loop"); commitLoop(); };
        mapping.setModel(this);
        mapping.setRowHeight(20);
        mapping.setTitle("Input mapping of the loaded session");
        kit::styleLabel(position, 13.0f, true);
        kit::styleLabel(mapHeader, 11.0f, true, true);
        mapHeader.setText("INPUT   <-   RECORDED TRACK (channel)   FILE", juce::dontSendNotification);
        for (auto* c2 : std::initializer_list<juce::Component*> { &loadButton, &sessionLabel, &lockButton, &armButton, &stateBadge, &sourceSwitch, &play, &pause, &stopB,
                                                                  &seek, &position, &loopStart, &loopEnd, &loopOn, &mapping, &mapHeader })
            addAndMakeVisible(c2);
    }

    void refresh() {
        maps = proc.getSoundcheck().getInputMapping();
        mapping.updateContent();
        mapping.repaint();
        const auto path = proc.getSoundcheckSessionPath();
        missing = proc.isSoundcheckSessionMissing();
        sessionText = path.isEmpty() ? juce::String("No session loaded") : (missing ? juce::String::fromUTF8("\xe2\x9c\x95 MISSING: ") + path : path);
        const auto& pb = proc.getShow().playback.getChildWithName("Soundcheck");
        if (pb.hasProperty("loopStart")) {
            loopStart.setValue(static_cast<double>(pb["loopStart"]), juce::dontSendNotification);
            loopEnd.setValue(static_cast<double>(pb["loopEnd"]), juce::dontSendNotification);
            loopOn.setToggleState(static_cast<bool>(pb["loopOn"]), juce::dontSendNotification);
        }
        kit::refreshToggleText(loopOn, "Loop");
        poll();
    }

    void poll() {
        auto& sc = proc.getSoundcheck();
        const auto ls = sc.getLoadState();
        if (ls.error != lastError) { lastError = ls.error; if (ls.error.isNotEmpty()) ctx.say("Soundcheck: " + ls.error, true); }
        if (ls.loaded != wasLoaded) { wasLoaded = ls.loaded; maps = sc.getInputMapping(); mapping.updateContent(); mapping.repaint(); }
        juce::String text = sessionText;
        if (ls.loading) text = "Loading... " + text;
        else if (ls.loaded) text = juce::String(ls.numInputs) + " inputs, " + kit::formatClock(ls.durationSeconds) + "  " + text;
        if (ls.error.isNotEmpty()) text << "  (" << ls.error << ")";
        sessionLabel.setText(text, juce::dontSendNotification);
        sessionLabel.setTooltip(text);
        sessionLabel.setColour(juce::Label::textColourId, missing || (ls.error.isNotEmpty() && !ls.loaded) ? palette().danger : palette().textMuted);
        const double dur = juce::jmax(0.1, sc.getDurationSeconds());
        if (std::abs(seek.getMaximum() - dur) > 0.01) {
            seek.setRange(0.0, dur, 0.01);
            loopStart.setRange(0.0, dur, 0.1);
            loopEnd.setRange(0.0, dur, 0.1);
        }
        if (!seeking) seek.setValue(sc.getPositionSeconds(), juce::dontSendNotification);
        position.setText(kit::formatClock(sc.getPositionSeconds(), true) + " / " + kit::formatClock(sc.getDurationSeconds(), true) + (sc.isPlaying() ? "  PLAYING" : "  PAUSED"),
                         juce::dontSendNotification);
        const bool locked = sc.isLocked(), armed = sc.isArmed();
        lockButton.setButtonText(locked ? "LOCKED (click to unlock)" : "UNLOCKED (click to lock)");
        armButton.setButtonText(armed ? "ARMED (click to disarm)" : "Arm recorded inputs...");
        armButton.setEnabled(!locked || armed);
        armButton.setColour(juce::TextButton::buttonColourId, armed ? palette().warn.withAlpha(0.6f) : palette().panelRaised);
        const bool replacing = proc.getMeters().soundcheckActive.load() || sc.isReplacingInputs();
        const bool fading = armed && juce::Time::getMillisecondCounterHiRes() - armTimeMs < 2600.0;
        if (replacing && fading) stateBadge.set("RECORDED INPUTS FADING IN", StatusBadge::Level::Warn);
        else if (replacing) stateBadge.set("RECORDED INPUTS ACTIVE", StatusBadge::Level::Danger);
        else if (armed) stateBadge.set("ARMED: source switch LIVE", StatusBadge::Level::Warn);
        else if (!locked) stateBadge.set("UNLOCKED, NOT ARMED", StatusBadge::Level::Warn);
        else stateBadge.set("LIVE INPUTS (LOCKED)", StatusBadge::Level::Ok);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 8);
        const int rowH = juce::jlimit(24, 30, r.getHeight() / 9);
        auto row = r.removeFromTop(rowH);
        loadButton.setBounds(row.removeFromLeft(130));
        row.removeFromLeft(8);
        sessionLabel.setBounds(row);
        r.removeFromTop(6);
        row = r.removeFromTop(rowH);
        kit::layoutRow(row, { { &lockButton, 1.0f }, { &armButton, 1.0f }, { &sourceSwitch, 1.2f }, { &stateBadge, 1.3f } });
        r.removeFromTop(6);
        row = r.removeFromTop(rowH);
        kit::layoutRow(row, { { &play, -84.0f }, { &pause, -84.0f }, { &stopB, -84.0f }, { &seek, 3.0f }, { &position, -190.0f } });
        r.removeFromTop(6);
        row = r.removeFromTop(rowH);
        kit::layoutRow(row, { { &loopOn, -84.0f }, { &loopStart, 1.0f }, { &loopEnd, 1.0f } });
        r.removeFromTop(6);
        mapHeader.setBounds(r.removeFromTop(16));
        mapping.setBounds(r);
    }

private:
    int getNumRows() override { return static_cast<int>(maps.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override {
        if (row < 0 || row >= static_cast<int>(maps.size())) return;
        const auto& m = maps[static_cast<size_t>(row)];
        if (selected) g.fillAll(palette().accentDim);
        g.setFont(font(12.0f));
        g.setColour(palette().text);
        g.drawText(proc.getShow().inputDisplayName(m.inputIndex), 4, 0, 120, h, juce::Justification::centredLeft, true);
        g.drawText(m.name + " (ch " + juce::String(m.channel + 1) + ")", 130, 0, juce::jmax(60, w / 3), h, juce::Justification::centredLeft, true);
        g.setColour(palette().textMuted);
        g.drawText(m.file.getFileName(), 140 + w / 3, 0, w - (144 + w / 3), h, juce::Justification::centredLeft, true);
    }

    void chooseSession() {
        chooser = std::make_unique<juce::FileChooser>("Load recorded session", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("CrowdMike"), "*.json");
        juce::Component::SafePointer<SoundcheckPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) {
            if (safe == nullptr) return;
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            if (!safe->proc.loadSoundcheckSession(f)) safe->ctx.say("Soundcheck session not found: " + f.getFullPathName(), true);
            else safe->ctx.say("Loading soundcheck session " + f.getFileName());
            safe->refresh();
        });
    }

    void toggleLock() {
        auto& sc = proc.getSoundcheck();
        if (!sc.isLocked()) { sc.setLocked(true); ctx.say("Virtual soundcheck locked: live inputs restored"); poll(); return; }
        juce::Component::SafePointer<SoundcheckPanel> safe(this);
        kit::confirmAsync(this, "Unlock virtual soundcheck?",
                          "Unlocking allows recorded tracks to replace the LIVE inputs once armed. Only do this when the room is not live.", "Unlock",
                          [safe] { if (safe != nullptr) { safe->proc.getSoundcheck().setLocked(false); safe->ctx.say("Virtual soundcheck unlocked", true); safe->poll(); } });
    }

    void toggleArm() {
        auto& sc = proc.getSoundcheck();
        if (sc.isArmed()) { sc.disarm(); ctx.say("Recorded inputs disarmed (fading out)"); poll(); return; }
        juce::Component::SafePointer<SoundcheckPanel> safe(this);
        kit::confirmAsync(this, "Replace live inputs?",
                          "The recorded inputs will REPLACE the live inputs (fade-in over 2.5 s) while the source switch is on.", "Arm",
                          [safe] {
                              if (safe == nullptr) return;
                              if (!safe->proc.getSoundcheck().arm()) { safe->ctx.say("Cannot arm: unlock first and load a session", true); return; }
                              safe->armTimeMs = juce::Time::getMillisecondCounterHiRes();
                              safe->ctx.say("RECORDED INPUTS ARMED: fading in", true);
                              safe->poll();
                          });
    }

    void commitLoop() {
        const double a = juce::jmin(loopStart.getValue(), loopEnd.getValue()), b = juce::jmax(loopStart.getValue(), loopEnd.getValue());
        proc.setSoundcheckLoop(a, b, loopOn.getToggleState());
    }

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::TextButton loadButton, lockButton, armButton, play, pause, stopB, loopOn;
    juce::Label sessionLabel, position, mapHeader;
    StatusBadge stateBadge;
    ParamToggle sourceSwitch;
    juce::Slider seek, loopStart, loopEnd;
    juce::ListBox mapping;
    std::vector<rec::SoundcheckPlayer::InputMapping> maps;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String sessionText, lastError;
    bool seeking = false, wasLoaded = false, missing = false;
    double armTimeMs = -1.0e9;
};

// ================================================================= page
class PlaybackPage final : public Page, private Tickable {
public:
    explicit PlaybackPage(EditorContext& c)
        : Page(c), deck(c), bed(c), soundcheck(c),
          level(c.proc, ParamBank::index(Mod::Global, 0, params::GlobalP::PlaybackLevel)) {
        for (int s = 0; s < rec::PlaybackEngine::kMaxClips; ++s) {
            auto* t = tiles.add(new ClipTile(proc, s));
            t->onSelect = [this](int slot) { selectSlot(slot); };
            t->onToggle = [this](int slot) { toggleClip(slot); };
            t->onMove = [this](int slot, int d) { const int n = juce::jlimit(0, rec::PlaybackEngine::kMaxClips - 1, slot + d); tiles[n]->grabKeyboardFocus(); selectSlot(n); };
            addAndMakeVisible(t);
        }
        stopAll.setButtonText(juce::String::fromUTF8("\xe2\x96\xa0 Stop all"));
        kit::setupButton(stopAll, "Stop every clip (with fades)", "play.stopAll");
        stopAll.onClick = [this] { proc.getPlayback().stopAll(); ctx.say("All clips stopped"); };
        level.setTooltip("Playback level: master gain of every clip and the bed");
        kit::styleLabel(levelLabel, 12.0f, false, true);
        levelLabel.setText("Playback level", juce::dontSendNotification);
        tabs.addTab("Clip", palette().panel, &deck, false);
        tabs.addTab("Crowd bed", palette().panel, &bed, false);
        tabs.addTab("Virtual soundcheck", palette().panel, &soundcheck, false);
        tabs.setTabBarDepth(30);
        tabs.setOutline(0);
        deck.onChanged = [this] { pollTiles(true); };
        for (auto* c2 : std::initializer_list<juce::Component*> { &stopAll, &level, &levelLabel, &tabs, &scBadge }) addAndMakeVisible(c2);
        selectSlot(0);
    }

    void pageShown() override { active = true; refreshAll(); }
    void pageHidden() override { active = false; }

    void paint(juce::Graphics& g) override {
        Page::paint(g);
        kit::paintSection(g, gridArea, "Clip slots");
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10);
        auto header = r.removeFromTop(32);
        stopAll.setBounds(header.removeFromLeft(110));
        header.removeFromLeft(10);
        scBadge.setBounds(header.removeFromRight(juce::jmin(260, header.getWidth() / 3)));
        header.removeFromRight(10);
        levelLabel.setBounds(header.removeFromLeft(100));
        level.setBounds(header.removeFromLeft(juce::jmin(320, header.getWidth())));
        r.removeFromTop(8);
        const int cols = 8, rowsN = 4;
        gridArea = r.removeFromTop(juce::jlimit(kit::kSectionTitleH + 4 * 40, kit::kSectionTitleH + 4 * 110, r.getHeight() * (r.getHeight() < 600 ? 34 : 40) / 100));
        auto gc = kit::sectionContent(gridArea);
        const int tw = gc.getWidth() / cols, th = gc.getHeight() / rowsN;
        for (int s = 0; s < tiles.size(); ++s)
            tiles[s]->setBounds(gc.getX() + (s % cols) * tw, gc.getY() + (s / cols) * th, tw - 4, th - 4);
        r.removeFromTop(8);
        tabs.setBounds(r);
    }

    void tick() override {
        if (!active) return;
        if (++tickCount % 3 == 0) poll();     // ~10 Hz: getClipInfo takes the control mutex briefly
    }

    void poll() {
        pollTiles(false);
        if (tabs.getCurrentTabIndex() == 0) deck.poll();
        if (tabs.getCurrentTabIndex() == 2 || tickCount % 15 == 0) soundcheck.poll();
        const bool replacing = proc.getMeters().soundcheckActive.load() || proc.getSoundcheck().isReplacingInputs();
        if (replacing) scBadge.set("RECORDED INPUTS ACTIVE", StatusBadge::Level::Danger);
        else if (proc.getSoundcheck().isArmed()) scBadge.set("SOUNDCHECK ARMED", StatusBadge::Level::Warn);
        else scBadge.set("LIVE INPUTS", StatusBadge::Level::Ok);
    }

private:
    void showModelChanged() override { refreshAll(); }

    void refreshAll() {
        for (int s = 0; s < tiles.size(); ++s) missing[static_cast<size_t>(s)] = proc.isClipMissing(s);
        for (auto* t : tiles) t->reloadThumbnail();
        pollTiles(true);
        deck.refresh();
        bed.refresh();
        soundcheck.refresh();
        poll();
    }

    void pollTiles(bool refreshMissing) {
        for (int s = 0; s < tiles.size(); ++s) {
            if (refreshMissing) missing[static_cast<size_t>(s)] = proc.isClipMissing(s);
            const auto info = proc.getPlayback().getClipInfo(s);
            if (info.error != lastErrors[static_cast<size_t>(s)]) {
                lastErrors[static_cast<size_t>(s)] = info.error;
                if (info.error.isNotEmpty()) ctx.say("Clip " + juce::String(s + 1) + ": " + info.error, true);
            }
            tiles[s]->refresh(info, missing[static_cast<size_t>(s)]);
        }
    }

    void selectSlot(int s) {
        selected = s;
        for (int i = 0; i < tiles.size(); ++i) tiles[i]->setSelected(i == s);
        if (deck.getSlot() != s || !deckInit) { deckInit = true; deck.setSlot(s); }
        ctx.select({ Selection::Kind::Clip, s });
    }

    void toggleClip(int s) {
        auto& pb = proc.getPlayback();
        const auto info = pb.getClipInfo(s);
        if (!info.loaded) { ctx.say("Clip " + juce::String(s + 1) + (missing[static_cast<size_t>(s)] ? " file is missing" : " is empty"), true); return; }
        if (info.playing) pb.stop(s);
        else pb.trigger(s);
    }

    DeckPanel deck;
    BedPanel bed;
    SoundcheckPanel soundcheck;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::OwnedArray<ClipTile> tiles;
    juce::TextButton stopAll;
    ParamSlider level;
    juce::Label levelLabel;
    StatusBadge scBadge;
    juce::Rectangle<int> gridArea;
    std::array<bool, rec::PlaybackEngine::kMaxClips> missing {};
    std::array<juce::String, rec::PlaybackEngine::kMaxClips> lastErrors;
    bool active = false, deckInit = false;
    int selected = 0, tickCount = 0;
};
}
std::unique_ptr<Page> makePlaybackPage(EditorContext& c) { return std::make_unique<PlaybackPage>(c); }
}
