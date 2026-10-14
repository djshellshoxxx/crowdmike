#include "PageKit.h"

namespace crowdmike::ui::kit {

void paintSection(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) {
    const auto& p = palette();
    const auto r = area.toFloat();
    g.setColour(p.panel);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(p.panelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
    if (title.isNotEmpty()) {
        g.setColour(p.textMuted);
        g.setFont(font(12.0f, true));
        g.drawText(title.toUpperCase(), area.withHeight(kSectionTitleH).reduced(10, 0), juce::Justification::centredLeft, true);
    }
}

juce::Rectangle<int> sectionContent(juce::Rectangle<int> area) {
    area.removeFromTop(kSectionTitleH);
    return area.reduced(8, 0).withTrimmedBottom(8);
}

void styleLabel(juce::Label& l, float h, bool bold, bool muted, juce::Justification j) {
    l.setFont(font(h, bold));
    l.setColour(juce::Label::textColourId, muted ? palette().textMuted : palette().text);
    l.setJustificationType(j);
    l.setMinimumHorizontalScale(0.7f);
    l.setInterceptsMouseClicks(false, false);
}

void refreshToggleText(juce::TextButton& b, const juce::String& caption) {
    b.setButtonText(juce::String::charToString(b.getToggleState() ? juce::juce_wchar(0x25CF) : juce::juce_wchar(0x25CB)) + " " + caption);
}

void setupToggle(juce::TextButton& b, const juce::String& caption, const juce::String& tooltip, juce::Colour onColour) {
    b.setClickingTogglesState(true);
    b.setColour(juce::TextButton::buttonOnColourId, onColour);
    b.setTooltip(tooltip);
    b.setTitle(caption);
    b.setWantsKeyboardFocus(true);
    refreshToggleText(b, caption);
}

void setupButton(juce::Button& b, const juce::String& tooltip, const juce::String& id) {
    b.setTooltip(tooltip);
    if (b.getTitle().isEmpty()) b.setTitle(b.getButtonText());
    b.setWantsKeyboardFocus(true);
    if (id.isNotEmpty()) b.setComponentID(id);
}

juce::String bindingSummary(const ShowModel& show, BindTarget target, int idx) {
    juce::StringArray parts;
    for (const auto& b : show.bindings) {
        if (b.target != target || b.targetIndex != idx) continue;
        juce::String s = b.type == MidiMsgType::CC ? "CC " + juce::String(b.number)
                       : b.type == MidiMsgType::NoteOnOff ? "Note " + juce::String(b.number) : juce::String("Pitch bend");
        s << (b.channel == 0 ? " / omni" : " / ch " + juce::String(b.channel));
        parts.add(s);
    }
    return parts.isEmpty() ? juce::String("-") : parts.joinIntoString(", ");
}

juce::String formatClock(double seconds, bool tenths) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "--:--";
    const auto total = static_cast<juce::int64>(seconds);
    const int h = static_cast<int>(total / 3600), m = static_cast<int>((total / 60) % 60), s = static_cast<int>(total % 60);
    juce::String out = h > 0 ? juce::String::formatted("%d:%02d:%02d", h, m, s) : juce::String::formatted("%d:%02d", m, s);
    if (tenths) out << "." << juce::String(static_cast<int>((seconds - static_cast<double>(total)) * 10.0) % 10);
    return out;
}

void confirmAsync(juce::Component* associated, const juce::String& title, const juce::String& message,
                  const juce::String& okText, std::function<void()> onConfirm) {
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr || juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        return;
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle(title)
                       .withMessage(message)
                       .withButton(okText)
                       .withButton("Cancel")
                       .withAssociatedComponent(associated);
    juce::AlertWindow::showAsync(options, [cb = std::move(onConfirm)](int result) { if (result == 1 && cb) cb(); });
}

void layoutRow(juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Component*, float>> items, int gap) {
    float totalWeight = 0.0f;
    int fixed = 0;
    for (const auto& [c, w] : items) { if (w < 0.0f) fixed += static_cast<int>(-w); else totalWeight += w; }
    const int gaps = gap * juce::jmax(0, static_cast<int>(items.size()) - 1);
    const int flexible = juce::jmax(0, row.getWidth() - fixed - gaps);
    int x = row.getX();
    for (const auto& [c, w] : items) {
        const int width = w < 0.0f ? static_cast<int>(-w) : (totalWeight > 0.0f ? static_cast<int>(static_cast<float>(flexible) * w / totalWeight) : 0);
        if (c != nullptr) c->setBounds(x, row.getY(), juce::jmin(width, row.getRight() - x), row.getHeight());
        x += width + gap;
    }
}
}
