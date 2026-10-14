// Inputs page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class InputsPage final : public Page {
public:
    explicit InputsPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Inputs", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeInputsPage(EditorContext& c) { return std::make_unique<InputsPage>(c); }
}
