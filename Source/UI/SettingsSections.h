#pragma once
// Panels of the Settings page (GUI_SPEC section 12, IO_SPEC section 1/8, CONTROL_SPEC sections 5/6):
// audio device + channel bindings, MIDI inputs / bindings / learn, OSC, UI preferences, show files, shortcuts.
// Each panel reports its preferred height for a width so the page can stack them in one or two columns.
#include <functional>
#include <memory>
#include "UI/EditorContext.h"
#include "UI/Meters.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
class SettingsSection : public juce::Component {
public:
    SettingsSection(EditorContext& c, const juce::String& title);
    void paint(juce::Graphics&) override;
    virtual int preferredHeight(int width) const = 0;
    virtual void refresh() {}
    std::function<void()> onLayoutChanged;     // set by the page: preferred height changed
protected:
    static constexpr int kHeader = 28;
    int rowH() const;
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::String heading;
};

class AudioSettingsSection final : public SettingsSection {
public:
    explicit AudioSettingsSection(EditorContext&);
    int preferredHeight(int) const override;
    void resized() override;
    void refresh() override;          // re-reads device status and bindings (rebuilds rows when the shape changed)
    struct BindingRow {
        juce::Label name, state;
        juce::ComboBox choice;
        bool isInput = true;
        int engineChannel = 0;
    };
    int getNumRows() const noexcept { return rows.size(); }
    BindingRow& row(int i) { return *rows[i]; }
    juce::Label& noteLabel() { return note; }
private:
    void rebuildRows();
    void updateRow(BindingRow&, const DeviceBinding&, const juce::StringArray& names);
    juce::Label status, note, inTitle, outTitle;
    juce::TextButton openDevice { "Open device settings..." };
    juce::OwnedArray<BindingRow> rows;
    int numIn = 0, numOut = 0;
};

class MidiSettingsSection final : public SettingsSection, private Tickable {
public:
    explicit MidiSettingsSection(EditorContext&);
    int preferredHeight(int) const override;
    void resized() override;
    void refresh() override;
    void tick() override;
    int getNumBindingRows() const noexcept { return bindingRows.size(); }
    juce::TextButton& removeButton(int i) { return bindingRows[i]->remove; }
    juce::TextButton& cancelLearnButton() { return cancelLearn; }
    StatusBadge& learnBadge() { return learn; }
    static juce::String describeBinding(CrowdMikeAudioProcessor&, const ControlBinding&);
private:
    struct BindingRow { juce::Label text; juce::TextButton remove { "Remove" }; };
    juce::Label inputsTitle, inputsNote, bindingsTitle, lastMidi, emptyNote;
    juce::OwnedArray<juce::ToggleButton> inputToggles;
    juce::OwnedArray<BindingRow> bindingRows;
    StatusBadge learn;
    juce::TextButton cancelLearn { "Cancel learn" };
    juce::String bindingSignature;
    int tickCount = 0;
};

class OscSettingsSection final : public SettingsSection, private Tickable {
public:
    explicit OscSettingsSection(EditorContext&);
    int preferredHeight(int) const override;
    void resized() override;
    void refresh() override;
    void tick() override { if (++tickCount % 10 == 0 && isVisible()) updateStatus(); }
    void apply();                      // writes the widgets into ShowModel::osc and (re)starts the server
    juce::ToggleButton& enableToggle() { return enable; }
    juce::ToggleButton& localhostToggle() { return localhost; }
    juce::TextEditor& portEditor() { return port; }
    static juce::String cheatSheet();
private:
    void updateStatus();
    juce::ToggleButton enable { "Enable OSC control" }, localhost { "Localhost only" };
    juce::Label portLabel, bindLabel, status, sheetTitle, sheet;
    juce::TextEditor port, bindAddress;
    int tickCount = 0;
};

class UiSettingsSection final : public SettingsSection {
public:
    explicit UiSettingsSection(EditorContext&);
    int preferredHeight(int) const override;
    void resized() override;
    void refresh() override;
    juce::ToggleButton& highContrastToggle() { return highContrast; }
    juce::ToggleButton& compactToggle() { return compact; }
    juce::ToggleButton& tooltipsToggle() { return tooltips; }
    juce::ComboBox& scaleBox() { return scaleChoice; }
    juce::Slider& rampSlider() { return ramp; }
    static constexpr float kScales[] = { 0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f };
private:
    juce::ToggleButton highContrast { "High-contrast colours" }, compact { "Compact mode (hide inspector)" }, tooltips { "Show tooltips" };
    juce::Label scaleLabel, rampLabel;
    juce::ComboBox scaleChoice;
    juce::Slider ramp;
};

// Beat Sync (docs/BEAT_SYNC_SPEC.md section 10): tempo source, manual BPM, nudge, bar length, auto input, trigger quantise,
// tap + resync buttons and a live read-out. Every control is bound to a tempo.* parameter (so it is MIDI/OSC/macro bindable).
class TempoSettingsSection final : public SettingsSection {
public:
    explicit TempoSettingsSection(EditorContext&);
    ~TempoSettingsSection() override;
    int preferredHeight(int) const override;
    void resized() override;
    void refresh() override;
    juce::TextButton& resyncButton() { return resync; }
    juce::Label& readout() { return info; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::Label info;
    juce::TextButton resync { "Resync downbeat" };
};

class ShowFilesSection final : public SettingsSection {
public:
    explicit ShowFilesSection(EditorContext&);
    int preferredHeight(int) const override;
    void resized() override;
    juce::TextButton& resetButton() { return reset; }
private:
    juce::TextButton save { "Save show..." }, load { "Load show..." }, reset { "Reset to defaults..." };
    juce::Label info;
    std::unique_ptr<juce::FileChooser> chooser;
};

class ShortcutsSection final : public SettingsSection {
public:
    explicit ShortcutsSection(EditorContext&);
    int preferredHeight(int) const override;
    void resized() override;
    static juce::StringArray shortcuts();
private:
    juce::Label text;
};
}
