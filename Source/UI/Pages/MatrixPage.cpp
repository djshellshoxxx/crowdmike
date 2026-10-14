// Matrix page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class MatrixPage final : public Page {
public:
    explicit MatrixPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Matrix", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeMatrixPage(EditorContext& c) { return std::make_unique<MatrixPage>(c); }
}
