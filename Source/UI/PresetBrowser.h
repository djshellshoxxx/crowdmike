#pragma once
// Wiring templates (ROUTING_SPEC section 8) + preset browser (CONTROL_SPEC section 2): factory presets by target
// module / instance, user show + routing presets via PresetManager (asynchronous file choosers only).
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "Core/PresetManager.h"
#include "UI/EditorContext.h"

namespace crowdmike::ui {
class PresetBrowser final : public juce::Component {
public:
    explicit PresetBrowser(EditorContext&);
    ~PresetBrowser() override;
    void refresh();                         // rescans the user folder + factory list
    void resized() override;
    void paint(juce::Graphics&) override;
    // Direct actions (also used by tests; the buttons route through confirmation first where destructive).
    void applyTemplate(int templateIndex);
    bool applyFactory(int row);             // row of the current factory list
    bool loadPresetFile(const juce::File&);
    int getNumFactoryRows() const noexcept { return static_cast<int>(factory.size()); }

private:
    class TemplateModel;
    class FactoryModel;
    class UserModel;
    void rebuildFactory();
    Mod targetModule() const;
    void savePreset(bool routingOnly);
    void choosePresetFile();

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    std::unique_ptr<TemplateModel> templateModel;
    std::unique_ptr<FactoryModel> factoryModel;
    std::unique_ptr<UserModel> userModel;
    juce::ListBox templates, factoryList, userList;
    juce::Label templateDesc;
    juce::TextButton applyTemplateButton, applyFactoryButton, saveShow, saveRouting, loadFile, loadUser, refreshButton;
    juce::ComboBox target, instance;
    std::vector<const FactoryPreset*> factory;
    juce::Array<juce::File> userFiles;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> templArea, factArea, userArea;
};
}
