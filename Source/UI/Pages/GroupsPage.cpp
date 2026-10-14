// Groups page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class GroupsPage final : public Page {
public:
    explicit GroupsPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Groups", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makeGroupsPage(EditorContext& c) { return std::make_unique<GroupsPage>(c); }
}
