#pragma once
// Small layout / text helpers shared by the Record, Playback and Scenes pages (section boxes, labels, toggles with a
// glyph so state never relies on colour, binding summaries, async confirmation).
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Core/ShowModel.h"
#include "UI/Theme.h"

namespace crowdmike::ui::kit {
constexpr int kSectionTitleH = 24;
constexpr int kRowH = 26;
constexpr int kGap = 6;

// Panel with a title strip; returns the content rectangle (inside the padding).
void paintSection(juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
juce::Rectangle<int> sectionContent(juce::Rectangle<int> area);

void styleLabel(juce::Label&, float height = 13.0f, bool bold = false, bool muted = false,
                juce::Justification = juce::Justification::centredLeft);
// Toggle text button whose caption shows a glyph for its state ("● ARM" / "○ ARM").
void setupToggle(juce::TextButton&, const juce::String& caption, const juce::String& tooltip, juce::Colour onColour);
void refreshToggleText(juce::TextButton&, const juce::String& caption);
// Plain (non-parameter) action button.
void setupButton(juce::Button&, const juce::String& tooltip, const juce::String& componentId = {});

// "CC 20 / ch 1", "Note 36 / omni", "-" ...
juce::String bindingSummary(const ShowModel&, BindTarget, int targetIndex);
juce::String formatClock(double seconds, bool tenths = false);   // m:ss / h:mm:ss

// Asynchronous OK/Cancel confirmation (never blocks). In a headless run (no MessageManager) it does nothing.
void confirmAsync(juce::Component* associated, const juce::String& title, const juce::String& message,
                  const juce::String& okText, std::function<void()> onConfirm);

// Lay out `items` in one row with relative weights (0 = use the fixed width given as a negative weight).
void layoutRow(juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Component*, float>> items, int gap = kGap);
}
