#pragma once
// Persistent regions of the main window (GUI_SPEC section 2): status bar, navigation rail, inspector, performance strip.
#include "UI/EditorContext.h"
#include "UI/Meters.h"
#include "UI/TempoWidget.h"
#include "UI/Widgets.h"

namespace crowdmike::ui {
class StatusBar final : public juce::Component, private Tickable, private EditorContext::Listener {
public:
    // Two rows: the original row plus a tempo row (Beat Sync widget + transient message).
    static constexpr int kRowHeight = 44, kTempoRowHeight = 24, kHeight = kRowHeight + kTempoRowHeight;
    explicit StatusBar(EditorContext&);
    ~StatusBar() override;
    TempoWidget& getTempoWidget() noexcept { return tempoWidget; }
    void resized() override;
    void paint(juce::Graphics&) override;
    void tick() override;
    void showMessage(const juce::String&, bool warning);
private:
    void showModelChanged() override { tick(); }
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::Label title, sceneLabel, rateLabel, latencyLabel, cpuLabel, messageLabel;
    juce::TextButton prevScene { "<" }, nextScene { ">" };
    StatusBadge deviceBadge, recBadge, xrunBadge, modeBadge;
    ParamToggle panic, bypass;
    TempoWidget tempoWidget;
    int messageTicks = 0;
};

class NavRail final : public juce::Component {
public:
    explicit NavRail(EditorContext&);
    void setCurrent(PageId);
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(palette().panel); }
private:
    EditorContext& ctx;
    juce::OwnedArray<juce::TextButton> buttons;
};

class PerformanceStrip final : public juce::Component, private EditorContext::Listener {
public:
    explicit PerformanceStrip(EditorContext&);
    ~PerformanceStrip() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(palette().panel); g.setColour(palette().panelEdge); g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth())); }
private:
    void showModelChanged() override;
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    ParamSlider masterLevel;
    ParamToggle masterMute, freeze;
    juce::OwnedArray<ParamSlider> macros;
    juce::OwnedArray<juce::Label> macroLabels;
    MomentaryButton echoThrow, reverseTrigger, tailKill;
    juce::TextButton marker { "MARKER" }, record { "REC" }, panicBtn { "PANIC" };
    juce::Label masterLabel;
};

class Inspector final : public juce::Component, private EditorContext::Listener {
public:
    explicit Inspector(EditorContext&);
    ~Inspector() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(palette().panel); }
    void rebuild();
private:
    void selectionChanged() override { rebuild(); }
    void showModelChanged() override { rebuild(); }
    struct Content;
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::Label header;
    juce::TextButton presetButton { "Presets" };
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
};
}
