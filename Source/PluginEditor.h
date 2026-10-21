#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <memory>
#include "PluginProcessor.h"
#include "UI/EditorContext.h"
#include "UI/LookAndFeel.h"
#include "UI/Shell.h"

// Main window: status bar / nav rail / page / inspector / performance strip (GUI_SPEC section 2).
class CrowdMikeAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::KeyListener,
                                            private crowdmike::ui::EditorContext::Listener
{
public:
    explicit CrowdMikeAudioProcessorEditor(CrowdMikeAudioProcessor&);
    ~CrowdMikeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool isKeyDown, juce::Component*) override;

    crowdmike::ui::EditorContext& getContext() noexcept { return context; }
    void showPage(crowdmike::ui::PageId);
    crowdmike::ui::PageId currentPage() const noexcept { return current; }
    crowdmike::ui::Page* getPage(crowdmike::ui::PageId);

private:
    void showModelChanged() override;
    void applyUiSettings();

    CrowdMikeAudioProcessor& processor;
    crowdmike::ui::EditorContext context;
    crowdmike::ui::CMLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    crowdmike::ui::StatusBar statusBar;
    crowdmike::ui::NavRail navRail;
    crowdmike::ui::Inspector inspector;
    crowdmike::ui::PerformanceStrip strip;
    std::array<std::unique_ptr<crowdmike::ui::Page>, static_cast<size_t>(crowdmike::ui::PageId::Count)> pages;
    crowdmike::ui::PageId current = crowdmike::ui::PageId::Live;
    bool inspectorVisible = true;
    bool echoKeyDown = false, reverseKeyDown = false;
};
