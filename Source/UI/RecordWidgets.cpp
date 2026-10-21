#include "RecordWidgets.h"
#include "Engine/TapMap.h"
#include "UI/PageKit.h"

namespace crowdmike::ui {
juce::String tapDisplayName(const ShowModel& show, int id) {
    using namespace TapMap;
    if (!isValid(id)) return "None";
    if (id < Proc) return "Raw: " + show.inputDisplayName(id - Raw);
    if (id < Group) return "Processed: " + show.inputDisplayName(id - Proc);
    if (id < Fx) return "Group: " + show.groupDisplayName(id - Group);
    if (id < Master || id >= FxExtra) return "FX: " + defaultName(id);
    if (id == Master) return "Master";
    if (id == Playback) return "Playback bus";
    const int pair = id - OutPair;
    return "Outputs: " + show.outputDisplayName(pair * 2) + " / " + show.outputDisplayName(pair * 2 + 1);
}

void fillTapCombo(juce::ComboBox& box, const ShowModel& show, int ins, int outs) {
    using namespace TapMap;
    const int keep = box.getSelectedId();
    box.clear(juce::dontSendNotification);
    box.addItem("None (unused)", 1);
    const auto add = [&](int id, bool active) { box.addItem(tapDisplayName(show, id) + (active ? "" : "  (inactive)"), id + 2); };
    box.addSectionHeading("Raw inputs (pre-processing)");
    for (int i = 0; i < kMaxInputs; ++i) add(forRawInput(i), i < ins);
    box.addSectionHeading("Processed inputs");
    for (int i = 0; i < kMaxInputs; ++i) add(forProcessedInput(i), i < ins);
    box.addSectionHeading("Groups (stereo)");
    for (int g = 0; g < kMaxGroups; ++g) add(forGroup(g), true);
    box.addSectionHeading("FX returns (stereo)");
    for (int f = 0; f < kNumFx; ++f) add(forFx(f), true);
    box.addSectionHeading("Buses (stereo)");
    add(Master, true);
    add(Playback, true);
    box.addSectionHeading("Output pairs (stereo)");
    for (int p = 0; p < kMaxOutputs / 2; ++p) add(forOutputPair(p), p * 2 < outs);
    box.setSelectedId(keep, juce::dontSendNotification);
}

// ---------------------------------------------------------------- RecordTrackRow
RecordTrackRow::RecordTrackRow(CrowdMikeAudioProcessor& p, int t) : proc(p), track(t) {
    const juce::String n(t + 1);
    select.setButtonText(n);
    kit::setupButton(select, "Select track " + n + " for the rolling waveform", "rec.track" + n + ".select");
    source.setTitle("Track " + n + " source");
    source.setTooltip("Recorder source of track " + n + " (channel count follows the source: inputs mono, buses stereo)");
    source.setComponentID("rec.track" + n + ".source");
    source.onChange = [this] {
        auto& s = setting();
        const int id = source.getSelectedId() - 2;
        s.sourceId = TapMap::isValid(id) ? id : -1;
        s.numChannels = TapMap::isValid(id) ? TapMap::channelsFor(id) : 1;
        if (s.sourceId < 0) s.armed = false;
        updateStates();
        if (onEdited) onEdited();
    };
    name.setTitle("Track " + n + " file name");
    name.setTooltip("Optional name used for the {source} token (empty = source name)");
    name.setComponentID("rec.track" + n + ".name");
        name.setJustification(juce::Justification::centredLeft);
    name.onReturnKey = name.onFocusLost = [this] {
        if (name.getText() == setting().name) return;
        setting().name = name.getText().substring(0, 48);
        if (onEdited) onEdited();
    };
    const auto& pal = palette();
    kit::setupToggle(arm, "ARM", "Arm track " + n + " for recording", pal.rec);
    kit::setupToggle(mute, "MUTE", "Mute track " + n + " (kept configured but not recorded)", pal.mute);
    kit::setupToggle(monitor, "MON", "Monitor track " + n + " (follow it in the rolling waveform)", pal.accentDim);
    arm.setComponentID("rec.track" + n + ".arm");
    mute.setComponentID("rec.track" + n + ".mute");
    monitor.setComponentID("rec.track" + n + ".monitor");
    arm.onClick = [this] {
        auto& s = setting();
        s.armed = arm.getToggleState() && s.sourceId >= 0;
        updateStates();
        if (onEdited) onEdited();
    };
    mute.onClick = [this] { setting().muted = mute.getToggleState(); updateStates(); if (onEdited) onEdited(); };
    monitor.onClick = [this] {
        setting().monitor = monitor.getToggleState();
        updateStates();
        if (monitor.getToggleState() && onSelect) onSelect(track);
        if (onEdited) onEdited();
    };
    kit::styleLabel(channels, 11.5f, false, true, juce::Justification::centred);
    kit::styleLabel(status, 11.5f, true, false, juce::Justification::centred);
    meter.setTooltip("Track " + n + " level (written data). Click to clear the clip latch.");
    for (auto* c : std::initializer_list<juce::Component*> { &select, &source, &name, &channels, &arm, &mute, &monitor, &meter, &status })
        addAndMakeVisible(c);
}

RecTrackSetting& RecordTrackRow::setting() { return proc.getShow().recorder.tracks[static_cast<size_t>(track)]; }

void RecordTrackRow::refreshFromModel(int ins, int outs) {
    fillTapCombo(source, proc.getShow(), ins, outs);
    updateStates();
}

void RecordTrackRow::updateStates() {
    const auto& s = setting();
    source.setSelectedId(TapMap::isValid(s.sourceId) ? s.sourceId + 2 : 1, juce::dontSendNotification);
    if (!name.hasKeyboardFocus(true)) name.setText(s.name, false);
    name.setTextToShowWhenEmpty(TapMap::isValid(s.sourceId) ? TapMap::defaultName(s.sourceId) : juce::String("-"), palette().textMuted);
    const bool used = TapMap::isValid(s.sourceId);
    channels.setText(used ? (TapMap::channelsFor(s.sourceId) == 2 ? "ST" : "MONO") : "", juce::dontSendNotification);
    arm.setToggleState(s.armed && used, juce::dontSendNotification);
    mute.setToggleState(s.muted, juce::dontSendNotification);
    monitor.setToggleState(s.monitor, juce::dontSendNotification);
    kit::refreshToggleText(arm, "ARM");
    kit::refreshToggleText(mute, "MUTE");
    kit::refreshToggleText(monitor, "MON");
    arm.setEnabled(used);
    mute.setEnabled(used);
    monitor.setEnabled(used);
    name.setEnabled(used);
    repaint();
}

void RecordTrackRow::setSelected(bool s) {
    if (s == selected) return;
    selected = s;
    select.setButtonText((s ? juce::String::fromUTF8("\xe2\x96\xb6") : juce::String()) + juce::String(track + 1));
    repaint();
}

void RecordTrackRow::setTrackStatus(bool error, int dropouts) {
    juce::String t = error ? "ERR" : (dropouts > 0 ? juce::String(dropouts) + " DROP" : juce::String());
    if (t != status.getText()) {
        status.setText(t, juce::dontSendNotification);
        status.setColour(juce::Label::textColourId, error ? palette().danger : palette().warn);
        status.setTooltip(error ? "Disk / file error on this track" : (dropouts > 0 ? "Blocks dropped because the writer could not keep up" : ""));
    }
}

void RecordTrackRow::paint(juce::Graphics& g) {
    const auto& p = palette();
    if (selected) { g.setColour(p.accentDim.withAlpha(0.35f)); g.fillRect(getLocalBounds()); }
    else if (track % 2 == 1) { g.setColour(p.panelRaised.withAlpha(0.35f)); g.fillRect(getLocalBounds()); }
    if (setting().armed && TapMap::isValid(setting().sourceId) && !setting().muted) {
        g.setColour(p.rec);
        g.fillRect(0, 2, 3, getHeight() - 4);
    }
}

void RecordTrackRow::resized() {
    auto r = getLocalBounds().reduced(4, 2).withTrimmedLeft(2);
    const int w = r.getWidth();
    kit::layoutRow(r, { { &select, -34.0f }, { &source, 3.2f }, { &name, w > 640 ? 1.8f : 0.0f }, { &channels, -38.0f },
                        { &arm, -62.0f }, { &mute, -66.0f }, { &monitor, -60.0f }, { &meter, 2.0f }, { &status, -52.0f } }, 4);
    name.setVisible(w > 640);
}

// ---------------------------------------------------------------- RollingWaveform
void RollingWaveform::push(float peak) {
    history[static_cast<size_t>(writePos)] = std::isfinite(peak) ? juce::jlimit(0.0f, 1.5f, peak) : 0.0f;
    writePos = (writePos + 1) % kPoints;
    repaint();
}

void RollingWaveform::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat();
    g.setColour(p.background.darker(0.25f));
    g.fillRoundedRectangle(b, 3.0f);
    const float mid = b.getCentreY(), half = b.getHeight() * 0.44f;
    g.setColour(p.panelEdge);
    g.drawHorizontalLine(static_cast<int>(mid), b.getX(), b.getRight());
    const float step = b.getWidth() / static_cast<float>(kPoints);
    for (int i = 0; i < kPoints; ++i) {
        const float v = history[static_cast<size_t>((writePos + i) % kPoints)];
        if (v <= 0.0f) continue;
        // dB-ish scale so quiet crowd material is still visible
        const float db = juce::Decibels::gainToDecibels(v, -60.0f);
        const float h = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f) * half;
        g.setColour(v >= 0.99f ? p.danger : (db > -12.0f ? p.warn : p.accent));
        g.fillRect(b.getX() + static_cast<float>(i) * step, mid - h, juce::jmax(1.0f, step), h * 2.0f);
    }
    g.setColour(p.textMuted);
    g.setFont(font(11.0f));
    g.drawText(caption, getLocalBounds().reduced(6, 3), juce::Justification::topLeft, true);
}
}
