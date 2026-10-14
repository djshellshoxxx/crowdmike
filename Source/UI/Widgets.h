#pragma once
// Parameter-bound controls shared by every page (GUI_SPEC section 12): fine-adjust with Shift, reset with Ctrl/Cmd-click,
// numeric entry on double-click, right-click learn/assign menu, keyboard adjustable, host-gesture aware.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "PluginProcessor.h"
#include "UI/Theme.h"

namespace crowdmike::ui {
void showParamContextMenu(CrowdMikeAudioProcessor&, int globalParamIndex, juce::Component* anchor);

class ParamSlider : public juce::Slider {
public:
    ParamSlider(CrowdMikeAudioProcessor&, int globalIndex, juce::Slider::SliderStyle = juce::Slider::LinearHorizontal, int textBoxWidth = 62);
    int getParamIndex() const noexcept { return index; }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    CrowdMikeAudioProcessor& proc;
    int index;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

class ParamToggle : public juce::ToggleButton {
public:
    ParamToggle(CrowdMikeAudioProcessor&, int globalIndex, const juce::String& text);
    int getParamIndex() const noexcept { return index; }
    void mouseDown(const juce::MouseEvent&) override;
private:
    CrowdMikeAudioProcessor& proc;
    int index;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

class ParamChoice : public juce::ComboBox {
public:
    ParamChoice(CrowdMikeAudioProcessor&, int globalIndex);
    int getParamIndex() const noexcept { return index; }
    void mouseDown(const juce::MouseEvent&) override;
private:
    CrowdMikeAudioProcessor& proc;
    int index;
    std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
};

// Momentary button bound to a bool command parameter (echo throw, reverse trigger, tail kill...): 1 while held.
class MomentaryButton : public juce::TextButton {
public:
    MomentaryButton(CrowdMikeAudioProcessor&, int globalIndex, const juce::String& text);
    void buttonStateChanged() override;
private:
    CrowdMikeAudioProcessor& proc;
    int index;
};

// A label + control cell. Auto-picks control type from the parameter kind.
class ParamCell : public juce::Component {
public:
    ParamCell(CrowdMikeAudioProcessor&, int globalIndex, const juce::String& labelOverride = {}, bool rotary = false);
    void resized() override;
    void paint(juce::Graphics&) override {}
    juce::Component& control() { return *ctl; }
    int getParamIndex() const noexcept { return index; }
private:
    int index;
    juce::Label label;
    std::unique_ptr<juce::Component> ctl;
    bool toggleKind = false;
};

// Grid of ParamCells for a list of global parameter indices (inspector / FX pages are generated with this).
class ParamPanel : public juce::Component {
public:
    explicit ParamPanel(CrowdMikeAudioProcessor& p) : proc(p) {}
    void setParams(const std::vector<int>& indices, const juce::String& title = {}, bool rotary = false);
    void setCellSize(int w, int h) { cellW = w; cellH = h; resized(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    int getPreferredHeight(int width) const;
    int getNumCells() const noexcept { return cells.size(); }
private:
    CrowdMikeAudioProcessor& proc;
    juce::String title;
    juce::OwnedArray<ParamCell> cells;
    int cellW = 200, cellH = 44;
    bool useRotary = false;
};

// Utility: all global indices of one module instance, optionally filtered by suffix prefix.
std::vector<int> paramIndices(Mod module, int instance, const juce::String& suffixPrefix = {});
std::vector<int> paramRange(Mod module, int instance, int firstEnum, int count);
}
