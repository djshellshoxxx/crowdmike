#include "Theme.h"
#include <atomic>

namespace crowdmike::ui {
namespace {
std::atomic<bool> highContrast { false };
std::atomic<float> uiScale { 1.0f };
const Palette normal {
    juce::Colour(0xff0f1317), juce::Colour(0xff1a2128), juce::Colour(0xff232d36), juce::Colour(0xff33414d),
    juce::Colour(0xff55d6be), juce::Colour(0xff2b7f70), juce::Colour(0xffe8eef1), juce::Colour(0xff9aabb4),
    juce::Colour(0xff6fdc8c), juce::Colour(0xffffc857), juce::Colour(0xffff625d), juce::Colour(0xffff9f43),
    juce::Colour(0xfff2d95c), juce::Colour(0xffff4d6d) };
const Palette contrast {
    juce::Colour(0xff000000), juce::Colour(0xff0a0a0a), juce::Colour(0xff161616), juce::Colour(0xffffffff),
    juce::Colour(0xff00ffd5), juce::Colour(0xff009c85), juce::Colour(0xffffffff), juce::Colour(0xffd8d8d8),
    juce::Colour(0xff39ff6a), juce::Colour(0xffffe600), juce::Colour(0xffff3b30), juce::Colour(0xffffa000),
    juce::Colour(0xffffff00), juce::Colour(0xffff2d55) };
}
const Palette& palette() noexcept { return highContrast.load(std::memory_order_relaxed) ? contrast : normal; }
void setHighContrast(bool on) noexcept { highContrast.store(on); }
bool isHighContrast() noexcept { return highContrast.load(); }
void setScale(float s) noexcept { uiScale.store(juce::jlimit(0.5f, 3.0f, s)); }
float scale() noexcept { return uiScale.load(std::memory_order_relaxed); }
juce::Font font(float h, bool bold) {
    return juce::Font(juce::FontOptions(h * scale(), bold ? juce::Font::bold : juce::Font::plain));
}
juce::Font monoFont(float h) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h * scale(), juce::Font::plain));
}
}
