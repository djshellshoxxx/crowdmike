#pragma once
// Macro editor (CONTROL_SPEC section 4): 8 macros, name, live value (bound to the macro parameter) and up to 16
// destinations each with min / max / curve / invert / bipolar. Edits go to ShowModel::macros and are published with
// applyShowChanges().
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "UI/EditorContext.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
class MacroEditor final : public juce::Component {
public:
    explicit MacroEditor(EditorContext&);
    ~MacroEditor() override;
    void selectMacro(int m);
    int getSelectedMacro() const noexcept { return macro; }
    void refresh();                 // rebuild destination rows from the ShowModel
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    struct DestRow;
    void addDestination(int globalIndex);
    void removeDestination(int d);
    void commit(bool notifyListeners);
    void layoutRows();

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    int macro = 0;
    juce::OwnedArray<juce::TextButton> macroButtons;
    juce::TextEditor name;
    std::unique_ptr<ParamSlider> value;
    juce::TextButton addDest;
    juce::Label header, emptyNote;
    juce::OwnedArray<juce::Label> colLabels;
    juce::Viewport view;
    juce::Component holder;
    juce::OwnedArray<DestRow> rows;
};
}
