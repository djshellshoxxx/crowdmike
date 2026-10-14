// Settings page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class SettingsPage final : public Page {
public:
    explicit SettingsPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Settings", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeSettingsPage(EditorContext& c) { return std::make_unique<SettingsPage>(c); }
}
