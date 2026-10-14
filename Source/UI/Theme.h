#pragma once
// Colours / fonts / scale. High-contrast mode swaps the palette; critical states always pair colour with text/icon.
#include <juce_gui_basics/juce_gui_basics.h>

namespace crowdmike::ui {
struct Palette {
    juce::Colour background, panel, panelRaised, panelEdge, accent, accentDim, text, textMuted, ok, warn, danger, mute, solo, rec;
};
const Palette& palette() noexcept;
void setHighContrast(bool) noexcept;
bool isHighContrast() noexcept;
// Global UI scale (user setting x window scale); components multiply their metrics by this.
void setScale(float) noexcept;
float scale() noexcept;
inline int px(int v) noexcept { return juce::roundToInt(static_cast<float>(v) * scale()); }
juce::Font font(float height, bool bold = false);
juce::Font monoFont(float height);
}
