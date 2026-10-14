#pragma once
// Searchable parameter picker (macro destinations, locked parameters): module filter + text search + result list.
// Shown in a CallOutBox (asynchronous, never blocks). Enter or double-click picks the highlighted parameter.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "PluginProcessor.h"

namespace crowdmike::ui {
juce::String moduleDisplayName(Mod) noexcept;
Mod moduleOfIndex(int globalIndex) noexcept;
// Global indices whose module matches `module` (Mod::Count = all, macros excluded) and whose display name contains
// every whitespace-separated word of `query` (case-insensitive).
std::vector<int> searchParams(CrowdMikeAudioProcessor&, const juce::String& query, Mod module = Mod::Count, int maxResults = 400);
juce::String paramDisplayName(CrowdMikeAudioProcessor&, int globalIndex);   // "Input 3 Gain" style (host name)

class ParamPickerPanel final : public juce::Component, private juce::ListBoxModel {
public:
    ParamPickerPanel(CrowdMikeAudioProcessor&, std::function<void(int globalIndex)> onPick);
    void resized() override;
    void paint(juce::Graphics&) override;
    static void launch(CrowdMikeAudioProcessor&, juce::Component& anchor, std::function<void(int)> onPick);
private:
    int getNumRows() override { return static_cast<int>(results.size()); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { pick(row); }
    void returnKeyPressed(int row) override { pick(row); }
    void refresh();
    void pick(int row);
    CrowdMikeAudioProcessor& proc;
    std::function<void(int)> onPick;
    juce::TextEditor search;
    juce::ComboBox module;
    juce::ListBox list;
    juce::Label count;
    std::vector<int> results;
};
}
