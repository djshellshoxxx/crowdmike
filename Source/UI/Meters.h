#pragma once
// Meter-type components used on every page. All poll a std::function<float()> from the shared ticker.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "UI/Theme.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
// Peak meter with hold and a latched clip indicator. Linear peak in, dB scale out (-60 .. +3 dBFS).
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient, private Tickable {
public:
    enum class Orientation { Vertical, Horizontal };
    explicit LevelMeter(Orientation o = Orientation::Vertical) : orientation(o) { setInterceptsMouseClicks(true, false); setTooltip("Click to clear the clip latch"); }
    std::function<float()> source;      // returns the peak since the last call (linear)
    void tick() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { clipLatched = false; repaint(); }
    void clear() { level = hold = 0.0f; clipLatched = false; repaint(); }
    float getDisplayedLevelDb() const noexcept;
    bool isClipLatched() const noexcept { return clipLatched; }
    void feed(float peak) { process(peak); }       // for tests / manual feeding
private:
    void process(float peak);
    Orientation orientation;
    float level = 0.0f, hold = 0.0f;
    int holdCounter = 0;
    bool clipLatched = false;
};

// Gain-reduction bar (grows from the top / left). Value in dB of reduction (>= 0).
class GainReductionMeter final : public juce::Component, private Tickable {
public:
    explicit GainReductionMeter(float maxDb = 18.0f) : range(maxDb) {}
    std::function<float()> source;     // dB of reduction (positive)
    void tick() override { if (source) { const float v = source(); if (std::abs(v - shown) > 0.05f) { shown = v; repaint(); } } }
    void paint(juce::Graphics& g) override;
    void feed(float db) { shown = db; repaint(); }
    float shownDb() const noexcept { return shown; }
private:
    float range, shown = 0.0f;
};

// Text "LED" badge: always text + glyph so state never depends on colour only.
class StatusBadge final : public juce::Component {
public:
    enum class Level { Off, Ok, Warn, Danger };
    void set(const juce::String& t, Level l) { if (t != text || l != level) { text = t; level = l; repaint(); } }
    void paint(juce::Graphics&) override;
    juce::String text; Level level = Level::Off;
};

// Scrolling min/max waveform (LevelHistory snapshot) used for compact input/group/master history strips.
class HistoryStrip final : public juce::Component, private Tickable {
public:
    static constexpr int kBuckets = 160;
    std::function<void(float* mn, float* mx, float* rms, int buckets)> snapshot;
    void tick() override;
    void paint(juce::Graphics&) override;
private:
    float mn[kBuckets] {}, mx[kBuckets] {}, rms[kBuckets] {};
};
}
