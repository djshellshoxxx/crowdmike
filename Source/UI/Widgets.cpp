#include "Widgets.h"

namespace crowdmike::ui {
using namespace crowdmike;

std::vector<int> paramIndices(Mod module, int instance, const juce::String& prefix) {
    std::vector<int> v;
    const auto& mi = ParamBank::info(module);
    for (int i = 0; i < mi.defCount; ++i)
        if (prefix.isEmpty() || juce::String(mi.defs[i].suffix).startsWith(prefix)) v.push_back(ParamBank::index(module, instance, i));
    return v;
}
std::vector<int> paramRange(Mod module, int instance, int first, int count) {
    std::vector<int> v;
    for (int i = 0; i < count; ++i) v.push_back(ParamBank::index(module, instance, first + i));
    return v;
}

void showParamContextMenu(CrowdMikeAudioProcessor& proc, int idx, juce::Component* anchor) {
    auto* param = proc.getBank().parameterAt(idx);
    if (param == nullptr) return;
    auto& show = proc.getShow();
    juce::PopupMenu menu, macroMenu;
    menu.addSectionHeader(param->getName(48));
    menu.addItem(1, "MIDI Learn");
    bool hasBinding = false;
    for (const auto& b : show.bindings) hasBinding = hasBinding || (b.target == BindTarget::Param && b.targetIndex == idx);
    menu.addItem(2, "Clear MIDI Mapping", hasBinding);
    for (int m = 0; m < kNumMacros; ++m) {
        bool assigned = false;
        for (int d = 0; d < show.macros.macros[static_cast<size_t>(m)].count; ++d)
            assigned = assigned || show.macros.macros[static_cast<size_t>(m)].dests[static_cast<size_t>(d)].paramIndex == idx;
        macroMenu.addItem(10 + m, "Macro " + juce::String(m + 1) + " (" + show.macroNames[static_cast<size_t>(m)] + ")", true, assigned);
    }
    menu.addSubMenu("Assign to Macro", macroMenu);
    menu.addItem(3, "Remove From Macros");
    menu.addItem(4, "Lock Against Scene Recall", true, show.isLocked(idx));
    menu.addSeparator();
    menu.addItem(5, "Reset To Default");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor), [&proc, idx, param](int result) {
        auto& show = proc.getShow();
        if (result == 1) {
            const bool isBool = dynamic_cast<juce::AudioParameterBool*>(param) != nullptr;
            proc.getHub().startLearn({ BindTarget::Param, idx, isBool ? BindMode::Toggle : BindMode::Absolute });
        } else if (result == 2) {
            proc.getHub().removeBindingsFor(BindTarget::Param, idx);
        } else if (result >= 10 && result < 10 + kNumMacros) {
            auto& map = show.macros.macros[static_cast<size_t>(result - 10)];
            bool exists = false;
            for (int d = 0; d < map.count; ++d) exists = exists || map.dests[static_cast<size_t>(d)].paramIndex == idx;
            if (!exists && map.count < MacroMap::kMaxDests) {
                MacroDest dest;
                dest.paramIndex = idx;
                map.dests[static_cast<size_t>(map.count++)] = dest;
            }
            proc.applyShowChanges();
        } else if (result == 3) {
            for (auto& map : show.macros.macros) {
                for (int d = map.count - 1; d >= 0; --d)
                    if (map.dests[static_cast<size_t>(d)].paramIndex == idx) {
                        for (int k = d; k + 1 < map.count; ++k) map.dests[static_cast<size_t>(k)] = map.dests[static_cast<size_t>(k + 1)];
                        --map.count;
                    }
            }
            proc.applyShowChanges();
        } else if (result == 4) {
            show.setLocked(idx, !show.isLocked(idx));
        } else if (result == 5) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getDefaultValue());
            param->endChangeGesture();
        }
    });
}

// ---------------------------------------------------------------- ParamSlider
ParamSlider::ParamSlider(CrowdMikeAudioProcessor& p, int idx, juce::Slider::SliderStyle style, int textWidth)
    : proc(p), index(idx) {
    auto* param = p.getBank().parameterAt(idx);
    setSliderStyle(style);
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical)
        setTextBoxStyle(style == juce::Slider::LinearVertical ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight, false, textWidth, 20);
    else if (style == juce::Slider::RotaryHorizontalVerticalDrag || style == juce::Slider::RotaryVerticalDrag)
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, textWidth, 18);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(250);
    setWantsKeyboardFocus(true);
    setName(param != nullptr ? param->getName(64) : juce::String("Parameter"));
    setTitle(getName());
    setPopupMenuEnabled(false);
    if (param != nullptr) {
        attachment = std::make_unique<juce::SliderParameterAttachment>(*param, *this, &proc.getUndoManager());
        setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
    }
}

void ParamSlider::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { showParamContextMenu(proc, index, this); return; }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {     // reset to default
        if (auto* p = proc.getBank().parameterAt(index)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture(); }
        return;
    }
    setMouseDragSensitivity(e.mods.isShiftDown() ? 1500 : 250);
    juce::Slider::mouseDown(e);
}
void ParamSlider::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isPopupMenu()) return;
    setMouseDragSensitivity(e.mods.isShiftDown() ? 1500 : 250);
    juce::Slider::mouseDrag(e);
}
void ParamSlider::mouseDoubleClick(const juce::MouseEvent&) { showTextBox(); }   // numeric entry

// ---------------------------------------------------------------- ParamToggle
ParamToggle::ParamToggle(CrowdMikeAudioProcessor& p, int idx, const juce::String& text) : proc(p), index(idx) {
    auto* param = p.getBank().parameterAt(idx);
    setButtonText(text);
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
    setName(param != nullptr ? param->getName(64) : text);
    setTitle(getName());
    setTooltip(getName());
    if (param != nullptr) attachment = std::make_unique<juce::ButtonParameterAttachment>(*param, *this, &proc.getUndoManager());
}
void ParamToggle::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { showParamContextMenu(proc, index, this); return; }
    juce::ToggleButton::mouseDown(e);
}

// ---------------------------------------------------------------- ParamChoice
ParamChoice::ParamChoice(CrowdMikeAudioProcessor& p, int idx) : proc(p), index(idx) {
    auto* param = dynamic_cast<juce::AudioParameterChoice*>(p.getBank().parameterAt(idx));
    setWantsKeyboardFocus(true);
    setName(param != nullptr ? param->getName(64) : juce::String("Choice"));
    setTitle(getName());
    if (param != nullptr) {
        addItemList(param->choices, 1);
        attachment = std::make_unique<juce::ComboBoxParameterAttachment>(*param, *this, &proc.getUndoManager());
    }
}
void ParamChoice::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { showParamContextMenu(proc, index, this); return; }
    juce::ComboBox::mouseDown(e);
}

// ---------------------------------------------------------------- MomentaryButton
MomentaryButton::MomentaryButton(CrowdMikeAudioProcessor& p, int idx, const juce::String& text)
    : juce::TextButton(text), proc(p), index(idx) {
    setWantsKeyboardFocus(true);
    setTitle(text);
    setTooltip(text + " (momentary: active while held)");
}
void MomentaryButton::buttonStateChanged() {
    if (auto* param = proc.getBank().parameterAt(index)) {
        const bool down = isDown();
        if (down != (param->getValue() >= 0.5f)) {
            if (down) param->beginChangeGesture();
            param->setValueNotifyingHost(down ? 1.0f : 0.0f);
            if (!down) param->endChangeGesture();
        }
    }
}

// ---------------------------------------------------------------- ParamCell / ParamPanel
ParamCell::ParamCell(CrowdMikeAudioProcessor& p, int idx, const juce::String& labelOverride, bool rotary) : index(idx) {
    auto* param = p.getBank().parameterAt(idx);
    const juce::String name = labelOverride.isNotEmpty() ? labelOverride : (param != nullptr ? param->getName(40) : juce::String());
    if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr) {
        toggleKind = true;
        ctl = std::make_unique<ParamToggle>(p, idx, name);
    } else if (dynamic_cast<juce::AudioParameterChoice*>(param) != nullptr) {
        ctl = std::make_unique<ParamChoice>(p, idx);
    } else {
        ctl = std::make_unique<ParamSlider>(p, idx, rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    }
    addAndMakeVisible(*ctl);
    if (!toggleKind) {
        label.setText(name, juce::dontSendNotification);
        label.setFont(font(12.0f));
        label.setColour(juce::Label::textColourId, palette().textMuted);
        label.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(label);
    }
}
void ParamCell::resized() {
    auto r = getLocalBounds();
    if (toggleKind) { ctl->setBounds(r.reduced(2)); return; }
    label.setBounds(r.removeFromTop(juce::jmin(16, r.getHeight() / 2)));
    ctl->setBounds(r);
}

void ParamPanel::setParams(const std::vector<int>& indices, const juce::String& t, bool rotary) {
    title = t;
    useRotary = rotary;
    cells.clear();
    for (int idx : indices) {
        cells.add(new ParamCell(proc, idx, {}, rotary));
        addAndMakeVisible(cells.getLast());
    }
    resized();
    repaint();
}
int ParamPanel::getPreferredHeight(int width) const {
    const int headerH = title.isNotEmpty() ? 22 : 0;
    const int cols = juce::jmax(1, width / juce::jmax(1, cellW));
    const int rows = (cells.size() + cols - 1) / cols;
    return headerH + rows * cellH + 4;
}
void ParamPanel::paint(juce::Graphics& g) {
    if (title.isEmpty()) return;
    g.setColour(palette().accent);
    g.setFont(font(13.0f, true));
    g.drawText(title.toUpperCase(), 4, 0, getWidth() - 8, 20, juce::Justification::centredLeft);
}
void ParamPanel::resized() {
    auto area = getLocalBounds();
    if (title.isNotEmpty()) area.removeFromTop(22);
    const int cols = juce::jmax(1, area.getWidth() / juce::jmax(1, cellW));
    const int w = area.getWidth() / cols;
    int i = 0;
    for (auto* c : cells) {
        c->setBounds(area.getX() + (i % cols) * w, area.getY() + (i / cols) * cellH, w, cellH);
        ++i;
    }
}
}
