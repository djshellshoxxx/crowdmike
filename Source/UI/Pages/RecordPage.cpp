// Record page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class RecordPage final : public Page {
public:
    explicit RecordPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Record", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeRecordPage(EditorContext& c) { return std::make_unique<RecordPage>(c); }
}
