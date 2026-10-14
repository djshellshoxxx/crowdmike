#include "LookAndFeel.h"

namespace crowdmike::ui {
CMLookAndFeel::CMLookAndFeel() { refreshColours(); }

void CMLookAndFeel::refreshColours() {
    const auto& p = palette();
    setColour(juce::ResizableWindow::backgroundColourId, p.background);
    setColour(juce::Label::textColourId, p.text);
    setColour(juce::Slider::textBoxTextColourId, p.text);
    setColour(juce::Slider::textBoxBackgroundColourId, p.panel);
    setColour(juce::Slider::textBoxOutlineColourId, p.panelEdge);
    setColour(juce::Slider::textBoxHighlightColourId, p.accentDim);
    setColour(juce::TextEditor::backgroundColourId, p.panel);
    setColour(juce::TextEditor::textColourId, p.text);
    setColour(juce::TextEditor::outlineColourId, p.panelEdge);
    setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour(juce::ComboBox::backgroundColourId, p.panelRaised);
    setColour(juce::ComboBox::textColourId, p.text);
    setColour(juce::ComboBox::outlineColourId, p.panelEdge);
    setColour(juce::ComboBox::arrowColourId, p.textMuted);
    setColour(juce::PopupMenu::backgroundColourId, p.panelRaised);
    setColour(juce::PopupMenu::textColourId, p.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accentDim);
    setColour(juce::PopupMenu::highlightedTextColourId, p.text);
    setColour(juce::TextButton::buttonColourId, p.panelRaised);
    setColour(juce::TextButton::buttonOnColourId, p.accentDim);
    setColour(juce::TextButton::textColourOffId, p.text);
    setColour(juce::TextButton::textColourOnId, p.text);
    setColour(juce::ToggleButton::textColourId, p.text);
    setColour(juce::ScrollBar::thumbColourId, p.panelEdge);
    setColour(juce::TooltipWindow::backgroundColourId, p.panelRaised);
    setColour(juce::TooltipWindow::textColourId, p.text);
    setColour(juce::ListBox::backgroundColourId, p.panel);
    setColour(juce::ListBox::textColourId, p.text);
    setColour(juce::TableHeaderComponent::backgroundColourId, p.panelRaised);
    setColour(juce::TableHeaderComponent::textColourId, p.text);
}

juce::Font CMLookAndFeel::getTextButtonFont(juce::TextButton&, int h) { return font(juce::jmin(15.0f, static_cast<float>(h) * 0.55f / scale()), true); }
juce::Font CMLookAndFeel::getLabelFont(juce::Label& l) { return l.getFont().getHeight() > 0 ? l.getFont() : font(13.0f); }
juce::Font CMLookAndFeel::getComboBoxFont(juce::ComboBox&) { return font(13.0f); }
juce::Font CMLookAndFeel::getPopupMenuFont() { return font(14.0f); }

void CMLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                     juce::Slider::SliderStyle style, juce::Slider& s) {
    const auto& p = palette();
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    auto track = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
    const bool focus = s.hasKeyboardFocus(false);
    const auto colour = s.isEnabled() ? p.accent : p.textMuted;
    if (horizontal) {
        const float cy = track.getCentreY();
        const float thick = juce::jmax(4.0f, track.getHeight() * 0.28f);
        auto bar = juce::Rectangle<float>(track.getX(), cy - thick * 0.5f, track.getWidth(), thick);
        g.setColour(p.panelEdge);
        g.fillRoundedRectangle(bar, thick * 0.5f);
        const double zero = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 ? s.valueToProportionOfLength(0.0) : 0.0;
        const float zx = track.getX() + static_cast<float>(zero) * track.getWidth();
        g.setColour(colour);
        g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(zx, pos), bar.getY(), std::abs(pos - zx), thick), thick * 0.5f);
        g.setColour(p.text);
        g.fillEllipse(pos - thick, cy - thick, thick * 2.0f, thick * 2.0f);
        if (focus) { g.setColour(p.accent); g.drawEllipse(pos - thick - 2, cy - thick - 2, thick * 2 + 4, thick * 2 + 4, 1.5f); }
    } else {
        const float cx = track.getCentreX();
        const float thick = juce::jmax(5.0f, track.getWidth() * 0.22f);
        auto bar = juce::Rectangle<float>(cx - thick * 0.5f, track.getY(), thick, track.getHeight());
        g.setColour(p.panelEdge);
        g.fillRoundedRectangle(bar, thick * 0.5f);
        g.setColour(colour);
        g.fillRoundedRectangle(juce::Rectangle<float>(bar.getX(), pos, thick, bar.getBottom() - pos), thick * 0.5f);
        g.setColour(p.text);
        g.fillRoundedRectangle(cx - thick * 1.6f, pos - 4.0f, thick * 3.2f, 8.0f, 3.0f);
        if (focus) { g.setColour(p.accent); g.drawRoundedRectangle(cx - thick * 1.6f - 2, pos - 6.0f, thick * 3.2f + 4, 12.0f, 4.0f, 1.5f); }
    }
}

void CMLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) {
    const auto& p = palette();
    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(3.0f);
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    const auto centre = area.getCentre();
    const float angle = start + pos * (end - start);
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius - 2, radius - 2, 0, start, end, true);
    g.setColour(p.panelEdge);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius - 2, radius - 2, 0, start, angle, true);
    g.setColour(s.isEnabled() ? p.accent : p.textMuted);
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(p.panelRaised);
    g.fillEllipse(area.reduced(radius * 0.32f));
    juce::Path tick;
    tick.addRectangle(-1.5f, -(radius * 0.62f), 3.0f, radius * 0.32f);
    g.setColour(p.text);
    g.fillPath(tick, juce::AffineTransform::rotation(angle).translated(centre));
    if (s.hasKeyboardFocus(false)) { g.setColour(p.accent); g.drawEllipse(area, 1.5f); }
}

void CMLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) {
    const auto& p = palette();
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    g.setColour(on ? p.accentDim : p.panelRaised);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(b.hasKeyboardFocus(false) ? p.accent : (highlighted ? p.text : p.panelEdge));
    g.drawRoundedRectangle(r, 4.0f, b.hasKeyboardFocus(false) ? 2.0f : 1.0f);
    g.setColour(on ? p.text : p.textMuted);
    g.setFont(font(12.0f, true));
    // Text + a marker glyph so state never relies on colour alone.
    g.drawFittedText((on ? juce::String::fromUTF8("\xe2\x97\x8f ") : juce::String::fromUTF8("\xe2\x97\x8b ")) + b.getButtonText(),
                     b.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1);
}

void CMLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool highlighted, bool down) {
    const auto& p = palette();
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = b.getToggleState() ? p.accentDim : base;
    if (down) fill = fill.brighter(0.2f); else if (highlighted) fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(b.hasKeyboardFocus(false) ? p.accent : p.panelEdge);
    g.drawRoundedRectangle(r, 4.0f, b.hasKeyboardFocus(false) ? 2.0f : 1.0f);
}

void CMLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
    const auto& p = palette();
    g.fillAll(p.panelRaised);
    g.setColour(p.panelEdge);
    g.drawRect(0, 0, w, h);
    g.setColour(p.text);
    g.setFont(font(12.5f));
    g.drawFittedText(text, 4, 2, w - 8, h - 4, juce::Justification::centredLeft, 4);
}
}
