// Diagnostics page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class DiagnosticsPage final : public Page {
public:
    explicit DiagnosticsPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Diagnostics", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeDiagnosticsPage(EditorContext& c) { return std::make_unique<DiagnosticsPage>(c); }
}
