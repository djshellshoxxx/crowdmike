// Playback page (placeholder: replaced by the real page implementation).
#include "UI/Pages.h"

namespace crowdmike::ui {
namespace {
class PlaybackPage final : public Page {
public:
    explicit PlaybackPage(EditorContext& c) : Page(c) {}
    void paint(juce::Graphics& g) override {
        Page::paint(g);
        g.setColour(palette().textMuted);
        g.setFont(font(18.0f));
        g.drawText("Playback", getLocalBounds(), juce::Justification::centred);
    }
};
}
std::unique_ptr<Page> makePlaybackPage(EditorContext& c) { return std::make_unique<PlaybackPage>(c); }
}
