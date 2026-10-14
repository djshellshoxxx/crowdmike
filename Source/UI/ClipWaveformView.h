#pragma once
// Clip waveform (PlaybackEngine::getWaveform min/max envelope) with a draggable start/end trim region and play head.
// Also used without interaction as the clip-tile thumbnail. Keyboard: Left/Right move the start handle,
// Shift+Left/Right the end handle (1 % steps, Alt for 0.1 %).
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "UI/Theme.h"

namespace crowdmike::ui {
class ClipWaveformView final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit ClipWaveformView(bool interactive);
    void setEnvelope(const float* mn, const float* mx, int points);    // nullptr / 0 = no clip
    void clearEnvelope();
    bool hasEnvelope() const noexcept { return !mins.empty(); }
    void setTrim(double start, double end);
    double getTrimStart() const noexcept { return trimStart; }
    double getTrimEnd() const noexcept { return trimEnd; }
    void setPlayPosition(double regionPos01, bool playing, bool reversed);   // position inside the trim region
    void setMessage(const juce::String& m) { if (m != message) { message = m; repaint(); } }
    std::function<void(double start, double end, bool finished)> onTrimChanged;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    double xToFrac(float x) const;
    float fracToX(double f) const;
    void setTrimInternal(double s, double e, bool finished);
    bool interactive;
    std::vector<float> mins, maxs;
    double trimStart = 0.0, trimEnd = 1.0, playPos = 0.0;
    bool playing = false, reversed = false;
    int dragging = 0;     // 0 none, 1 start, 2 end, 3 region
    double dragAnchor = 0.0, dragStart0 = 0.0, dragEnd0 = 1.0;
    juce::String message;
};
}
