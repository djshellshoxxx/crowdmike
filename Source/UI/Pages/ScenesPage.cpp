// Scenes page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class ScenesPage final : public Page {
public:
    explicit ScenesPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Scenes", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeScenesPage(EditorContext& c) { return std::make_unique<ScenesPage>(c); }
}
