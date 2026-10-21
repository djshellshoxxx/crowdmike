// Settings page (GUI_SPEC section 12, IO_SPEC, CONTROL_SPEC): audio device + channel bindings, MIDI, OSC, interface
// preferences, show files and the keyboard cheat-sheet. Panels live in UI/SettingsSections.*; this file only stacks them
// in one or two scrollable columns.
#include "UI/Pages.h"
#include "UI/SettingsSections.h"

namespace crowdmike::ui {
namespace {
class SettingsPage final : public Page {
public:
    explicit SettingsPage(EditorContext& c)
        : Page(c), audio(c), midi(c), osc(c), uiPrefs(c), tempoSec(c), globalSec(c), files(c), keys(c) {
        for (auto* s : sections()) {
            s->onLayoutChanged = [this] { layoutContent(); };
            content.addAndMakeVisible(s);
        }
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setTitle("Settings");
        addAndMakeVisible(viewport);
    }
    ~SettingsPage() override { viewport.setViewedComponent(nullptr, false); }

    void pageShown() override { for (auto* s : sections()) s->refresh(); layoutContent(); }
    void resized() override { viewport.setBounds(getLocalBounds()); layoutContent(); }

    // test hooks
    AudioSettingsSection& audioSection() { return audio; }
    MidiSettingsSection& midiSection() { return midi; }
    OscSettingsSection& oscSection() { return osc; }
    UiSettingsSection& uiSection() { return uiPrefs; }
    TempoSettingsSection& tempoSection() { return tempoSec; }
    GlobalSettingsSection& globalSection() { return globalSec; }

private:
    void showModelChanged() override { uiPrefs.refresh(); }

    std::vector<SettingsSection*> sections() { return { &audio, &midi, &osc, &uiPrefs, &tempoSec, &globalSec, &files, &keys }; }   // every section must be in this list: it parents them

    void layoutContent() {
        const int w = viewport.getWidth() - viewport.getScrollBarThickness();
        if (w <= 0) return;
        const int gap = 8;
        const bool twoCols = w >= 1000;
        const int colW = twoCols ? (w - 3 * gap) / 2 : w - 2 * gap;
        std::vector<SettingsSection*> left { &audio, &midi }, right { &osc, &uiPrefs, &tempoSec, &globalSec, &files, &keys };
        if (!twoCols) { left = sections(); right.clear(); }
        const auto stack = [&](const std::vector<SettingsSection*>& list, int x) {
            int y = gap;
            for (auto* s : list) {
                const int h = s->preferredHeight(colW);
                s->setBounds(x, y, colW, h);
                y += h + gap;
            }
            return y;
        };
        const int hl = stack(left, gap);
        const int hr = stack(right, gap * 2 + colW);
        content.setSize(w, juce::jmax(hl, hr));
    }

    AudioSettingsSection audio;
    MidiSettingsSection midi;
    OscSettingsSection osc;
    UiSettingsSection uiPrefs;
    TempoSettingsSection tempoSec;
    GlobalSettingsSection globalSec;
    ShowFilesSection files;
    ShortcutsSection keys;
    juce::Component content;
    juce::Viewport viewport;
};
}
std::unique_ptr<Page> makeSettingsPage(EditorContext& c) { return std::make_unique<SettingsPage>(c); }
}
