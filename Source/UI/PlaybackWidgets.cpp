#include "PlaybackWidgets.h"
#include "UI/PageKit.h"

namespace crowdmike::ui {
juce::StringArray clipRouteNames() {
    juce::StringArray s { "Master" };
    for (int g = 1; g <= 8; ++g) s.add("Group " + juce::String(g));
    s.add("Direct playback bus");
    return s;
}

ClipTile::ClipTile(CrowdMikeAudioProcessor& p, int s) : proc(p), slot(s) {
    setWantsKeyboardFocus(true);
    setTitle("Clip " + juce::String(s + 1));
    setComponentID("play.tile" + juce::String(s + 1));
    setTooltip("Clip " + juce::String(s + 1) + ": click to select, double-click or Enter to trigger / stop, arrows to move");
    addAndMakeVisible(thumb);
    thumb.setInterceptsMouseClicks(false, false);
}

void ClipTile::refresh(const rec::PlaybackEngine::ClipInfo& info, bool missing) {
    const auto settings = proc.getPlayback().getSettings(slot);
    const auto newName = settings.name.isNotEmpty() ? settings.name : (info.name.isNotEmpty() ? info.name : juce::String());
    juce::String newState;
    if (missing) newState = "MISSING";
    else if (info.loading) newState = "LOADING";
    else if (info.error.isNotEmpty()) newState = "ERROR";
    else if (!info.loaded) newState = "EMPTY";
    else if (info.playing) newState = juce::String::fromUTF8("\xe2\x96\xb6 ") + juce::String(juce::roundToInt(info.playPosition01 * 100.0)) + "%";
    else newState = juce::String::fromUTF8("\xe2\x96\xa0 READY");
    const auto key = proc.getClipHotkey(slot);
    const auto dur = info.loaded ? kit::formatClock(info.durationSeconds * (settings.endFrac - settings.startFrac), true) : juce::String();
    if (info.loaded != loaded) { loaded = info.loaded; reloadThumbnail(); }
    thumb.setTrim(settings.startFrac, settings.endFrac);
    thumb.setPlayPosition(info.playPosition01, info.playing, settings.reverse);
    thumb.setMessage(missing ? "file missing" : (info.loading ? "loading..." : " "));
    if (newName != name || newState != state || key != hotkey || dur != duration || info.playing != playing || missing != missingFile) {
        name = newName; state = newState; hotkey = key; duration = dur; playing = info.playing; missingFile = missing;
        setDescription(name + " " + state);
        repaint();
    }
}

void ClipTile::reloadThumbnail() {
    std::vector<float> mn(256), mx(256);
    if (proc.getPlayback().getWaveform(slot, mn.data(), mx.data(), 256)) thumb.setEnvelope(mn.data(), mx.data(), 256);
    else thumb.clearEnvelope();
}

void ClipTile::setSelected(bool s) { if (s != selected) { selected = s; repaint(); } }

void ClipTile::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(playing ? p.accentDim.withAlpha(0.55f) : p.panelRaised);
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(hasKeyboardFocus(false) || selected ? p.accent : p.panelEdge);
    g.drawRoundedRectangle(b, 4.0f, selected ? 2.0f : 1.0f);
    auto r = getLocalBounds().reduced(6, 3);
    auto top = r.removeFromTop(16);
    g.setFont(font(11.0f, true));
    g.setColour(p.textMuted);
    g.drawText(juce::String(slot + 1), top.removeFromLeft(20), juce::Justification::centredLeft);
    if (hotkey.isNotEmpty()) g.drawText("[" + hotkey + "]", top.removeFromRight(juce::jmin(60, top.getWidth() / 2)), juce::Justification::centredRight, true);
    g.setColour(p.text);
    g.setFont(font(12.0f, true));
    g.drawText(name.isNotEmpty() ? name : juce::String("-"), top, juce::Justification::centredLeft, true);
    auto bottom = getLocalBounds().reduced(6, 3).removeFromBottom(15);
    g.setFont(font(11.0f, true));
    g.setColour(missingFile || state == "ERROR" ? p.danger : (playing ? p.ok : p.textMuted));
    g.drawText(state, bottom, juce::Justification::centredLeft, true);
    g.setColour(p.textMuted);
    g.drawText(duration, bottom, juce::Justification::centredRight, true);
}

void ClipTile::resized() {
    auto r = getLocalBounds().reduced(6, 3);
    r.removeFromTop(17);
    r.removeFromBottom(16);
    thumb.setBounds(r.reduced(0, 1));
    thumb.setVisible(r.getHeight() >= 8);
}

void ClipTile::mouseDown(const juce::MouseEvent&) { grabKeyboardFocus(); if (onSelect) onSelect(slot); }
void ClipTile::mouseDoubleClick(const juce::MouseEvent&) { if (onToggle) onToggle(slot); }

bool ClipTile::keyPressed(const juce::KeyPress& k) {
    const int c = k.getKeyCode();
    if (c == juce::KeyPress::returnKey) { if (onToggle) onToggle(slot); return true; }
    if (onMove) {
        if (c == juce::KeyPress::leftKey) { onMove(slot, -1); return true; }
        if (c == juce::KeyPress::rightKey) { onMove(slot, 1); return true; }
        if (c == juce::KeyPress::upKey) { onMove(slot, -8); return true; }
        if (c == juce::KeyPress::downKey) { onMove(slot, 8); return true; }
    }
    return false;
}
}
