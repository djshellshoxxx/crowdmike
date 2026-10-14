#pragma once
// Small shared building blocks for the Inputs / Groups / FX / Matrix pages: parameter helpers (actual-unit access,
// reset-to-default as one undo step), a titled card, a caption+control wrapper, a proportional flow layout and the
// module preset menu (factory presets + user presets via async FileChooser). Namespace `ui::kit` avoids clashes.
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "UI/Theme.h"

namespace crowdmike::ui::kit {
void styleLabel(juce::Label&, float height, bool bold = false, juce::Colour colour = palette().text,
                juce::Justification just = juce::Justification::centredLeft);

// Actual-unit value of a parameter (dB, Hz, ms, 0/1, choice index).
float actual(CrowdMikeAudioProcessor&, int globalIndex);
inline bool isOn(CrowdMikeAudioProcessor& p, int globalIndex) { return actual(p, globalIndex) >= 0.5f; }
// Sets a parameter in actual units inside a host gesture (message thread).
void setActual(CrowdMikeAudioProcessor&, int globalIndex, float value);
// Sets every listed parameter to its default as ONE undo transaction. Returns the number of parameters changed.
int resetToDefaults(CrowdMikeAudioProcessor&, const std::vector<int>& globalIndices, const juce::String& transactionName);

juce::String fmtDb(float db, int decimals = 1);
juce::String fmtHz(float hz);
juce::String fmtGainDb(float linearGain);      // "-inf", "0.0", "+3.0"

// Alignment-delay display units (Inputs page): 0 = ms, 1 = samples, 2 = metres (343 m/s). The parameter stays in ms.
constexpr float kSpeedOfSoundMps = 343.0f;
juce::String alignToText(double ms, int unit, double sampleRate);
double alignFromText(const juce::String&, int unit, double sampleRate);   // returns ms
const char* alignUnitName(int unit);

// Panel with a title strip. Children are laid out by the owner inside getContentBounds().
class Card : public juce::Component {
public:
    explicit Card(const juce::String& title = {}) : title(title) {}
    void setHeading(const juce::String& t) { title = t; repaint(); }
    void setSubtitle(const juce::String& s) { subtitle = s; repaint(); }
    void setHighlighted(bool h) { if (h != highlighted) { highlighted = h; repaint(); } }
    void paint(juce::Graphics&) override;
    juce::Rectangle<int> getContentBounds() const;
    int titleHeight() const;
protected:
    juce::String title, subtitle;
    bool highlighted = false;
};

// A short caption to the left of a control (used for dense rows).
class Captioned final : public juce::Component {
public:
    Captioned(const juce::String& caption, std::unique_ptr<juce::Component> control, int captionWidth = 40);
    void resized() override;
    juce::Component& control() { return *ctl; }
    void setTooltip(const juce::String& t) { if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(ctl.get())) tc->setTooltip(t); }
    template <typename T> T* as() { return dynamic_cast<T*>(ctl.get()); }
private:
    juce::Label caption;
    std::unique_ptr<juce::Component> ctl;
    int captionW;
};

// Flow layout: places items left-to-right, wrapping to a new line when the next item would not fit.
struct FlowItem {
    juce::Component* comp = nullptr;
    int width = 60;            // unscaled preferred width (multiplied by ui::scale())
    bool lineBreak = false;    // force this item to start a new line
    bool fill = false;         // stretch to the end of the line
};
// Returns the height used. When `apply` is false nothing is moved (measurement only).
int flow(const std::vector<FlowItem>& items, juce::Rectangle<int> area, int lineHeight, int gap, bool apply);

// Preset menu for one module block (factory presets for `factoryTarget`/`blockPrefixes`, user presets, file save/load).
struct PresetTarget {
    Mod module = Mod::Input;
    int instance = 0;
    juce::String factoryTarget;                // "input", "group", "master", "delay", ...
    juce::StringArray blockPrefixes;           // parameter-suffix prefixes captured/applied ("comp"); empty = whole module
    juce::String folderName;                   // sub-folder of PresetManager::userPresetFolder()
    juce::String title;                        // shown as the menu header
    std::vector<std::pair<juce::String, juce::StringArray>> extraSections;   // e.g. {"Dynamics presets", {"gate","deess"}}
};
void showPresetMenu(CrowdMikeAudioProcessor&, juce::Component* anchor, const PresetTarget&);
// Capture of the listed blocks (or the whole module) of one instance, as a PresetManager tree.
juce::ValueTree captureBlocks(CrowdMikeAudioProcessor&, const PresetTarget&);
juce::File userFolderFor(const PresetTarget&);

// Opens a callout with a parameter list; picking one opens the shared learn / macro menu for it.
void showAssignMenu(CrowdMikeAudioProcessor&, juce::Component* anchor, const std::vector<int>& globalIndices);
}
