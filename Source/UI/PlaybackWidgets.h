#pragma once
// Playback page building blocks: a clip tile of the 32-slot grid.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PluginProcessor.h"
#include "UI/ClipWaveformView.h"

namespace crowdmike::ui {
juce::StringArray clipRouteNames();     // "Master", "Group 1 (name)" ... "Direct playback bus"

class ClipTile final : public juce::Component, public juce::SettableTooltipClient {
public:
    ClipTile(CrowdMikeAudioProcessor&, int slot);
    // Pulls ClipInfo/settings (call at a modest rate). `missing` is cached by the page (file-system check).
    void refresh(const rec::PlaybackEngine::ClipInfo&, bool missing);
    void reloadThumbnail();
    void setSelected(bool);
    juce::String stateText() const { return state; }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { if (onSelect) onSelect(slot); repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    std::function<void(int)> onSelect, onToggle;
    std::function<void(int, int)> onMove;       // slot, delta (arrow keys)

private:
    CrowdMikeAudioProcessor& proc;
    int slot;
    bool selected = false, loaded = false, playing = false, missingFile = false;
    juce::String name, state, hotkey, duration;
    ClipWaveformView thumb { false };
};
}
