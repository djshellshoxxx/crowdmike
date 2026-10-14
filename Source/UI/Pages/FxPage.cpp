// Fx page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class FxPage final : public Page {
public:
    explicit FxPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Fx", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeFxPage(EditorContext& c) { return std::make_unique<FxPage>(c); }
}
