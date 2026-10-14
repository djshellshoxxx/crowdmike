#include "Meters.h"
#include <cmath>

namespace crowdmike::ui {
namespace {
constexpr float kMinDb = -60.0f, kMaxDb = 3.0f;
float toProportion(float linear) {
    const float db = juce::Decibels::gainToDecibels(linear, kMinDb);
    return juce::jlimit(0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
}
}

void LevelMeter::process(float peak) {
    if (!std::isfinite(peak)) peak = 0.0f;
    level = juce::jmax(peak, level * 0.80f);
    if (peak >= hold) { hold = peak; holdCounter = 36; }
    else if (holdCounter > 0) --holdCounter;
    else hold *= 0.92f;
    if (peak >= 1.0f) clipLatched = true;
    repaint();
}
void LevelMeter::tick() { if (source) process(source()); }
float LevelMeter::getDisplayedLevelDb() const noexcept { return juce::Decibels::gainToDecibels(level, -100.0f); }

void LevelMeter::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat();
    g.setColour(p.background.darker(0.3f));
    g.fillRoundedRectangle(b, 2.0f);
    b = b.reduced(1.5f);
    const float prop = toProportion(level), holdProp = toProportion(hold);
    const auto colourAt = [&](float proportion) {
        const float db = kMinDb + proportion * (kMaxDb - kMinDb);
        return db > -3.0f ? p.danger : (db > -12.0f ? p.warn : p.accent);
    };
    if (orientation == Orientation::Vertical) {
        const float h = b.getHeight() * prop;
        g.setColour(colourAt(prop));
        g.fillRect(b.getX(), b.getBottom() - h, b.getWidth(), h);
        g.setColour(p.text);
        g.fillRect(b.getX(), b.getBottom() - b.getHeight() * holdProp - 1.0f, b.getWidth(), 2.0f);
        if (clipLatched) { g.setColour(p.danger); g.fillRect(b.getX(), b.getY(), b.getWidth(), juce::jmin(5.0f, b.getHeight() * 0.1f)); }
    } else {
        const float w = b.getWidth() * prop;
        g.setColour(colourAt(prop));
        g.fillRect(b.getX(), b.getY(), w, b.getHeight());
        g.setColour(p.text);
        g.fillRect(b.getX() + b.getWidth() * holdProp - 1.0f, b.getY(), 2.0f, b.getHeight());
        if (clipLatched) { g.setColour(p.danger); g.fillRect(b.getRight() - juce::jmin(5.0f, b.getWidth() * 0.1f), b.getY(), 5.0f, b.getHeight()); }
    }
}

void GainReductionMeter::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat();
    g.setColour(p.background.darker(0.3f));
    g.fillRoundedRectangle(b, 2.0f);
    const float prop = juce::jlimit(0.0f, 1.0f, shown / range);
    g.setColour(p.warn);
    if (b.getWidth() >= b.getHeight()) g.fillRect(b.reduced(1.5f).withWidth((b.getWidth() - 3.0f) * prop));
    else g.fillRect(b.reduced(1.5f).withHeight((b.getHeight() - 3.0f) * prop));
}

void StatusBadge::paint(juce::Graphics& g) {
    const auto& p = palette();
    juce::Colour c = p.textMuted;
    juce::String glyph = juce::String::fromUTF8("\xe2\x97\x8b ");
    switch (level) {
        case Level::Ok: c = p.ok; glyph = juce::String::fromUTF8("\xe2\x97\x8f "); break;
        case Level::Warn: c = p.warn; glyph = juce::String::fromUTF8("\xe2\x96\xb2 "); break;
        case Level::Danger: c = p.danger; glyph = juce::String::fromUTF8("\xe2\x9c\x95 "); break;
        case Level::Off: break;
    }
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(c.withAlpha(level == Level::Off ? 0.0f : 0.18f));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(c);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    g.setFont(font(12.0f, true));
    g.drawFittedText(glyph + text, getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void HistoryStrip::tick() { if (snapshot) { snapshot(mn, mx, rms, kBuckets); repaint(); } }
void HistoryStrip::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto b = getLocalBounds().toFloat();
    g.setColour(p.background.darker(0.3f));
    g.fillRoundedRectangle(b, 2.0f);
    const float cy = b.getCentreY(), half = b.getHeight() * 0.5f - 1.0f;
    for (int i = 0; i < kBuckets; ++i) {
        const float x = b.getX() + b.getWidth() * (static_cast<float>(i) + 0.5f) / kBuckets;
        g.setColour(p.accentDim);
        g.drawLine(x, cy - juce::jlimit(0.0f, 1.0f, mx[i]) * half, x, cy - juce::jlimit(-1.0f, 0.0f, mn[i]) * half, 1.0f);
        g.setColour(p.accent);
        const float r = juce::jlimit(0.0f, 1.0f, rms[i]) * half;
        g.drawLine(x, cy - r, x, cy + r, 1.0f);
    }
}
}
