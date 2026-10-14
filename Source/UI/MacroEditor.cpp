#include "MacroEditor.h"
#include "UI/PageKit.h"
#include "UI/ParamPicker.h"

namespace crowdmike::ui {
struct MacroEditor::DestRow final : public juce::Component {
    DestRow(MacroEditor& o, int d) : owner(o), index(d) {
        const juce::String id = "macro.dest" + juce::String(d) + ".";
        kit::styleLabel(param, 12.5f);
        auto setup = [&](juce::Slider& s, double lo, double hi, const juce::String& tip, const juce::String& sid) {
            s.setSliderStyle(juce::Slider::LinearBar);
            s.setRange(lo, hi, 0.01);
            s.setTooltip(tip);
            s.setTitle(tip.upToFirstOccurrenceOf(":", false, false));
            s.setComponentID(id + sid);
            s.setWantsKeyboardFocus(true);
            s.onValueChange = [this] { write(); };
            addAndMakeVisible(s);
        };
        setup(minS, 0.0, 1.0, "Min: destination position (0..1) at macro 0", "min");
        setup(maxS, 0.0, 1.0, "Max: destination position (0..1) at macro 1", "max");
        setup(curve, -1.0, 1.0, "Curve: -1 = fast start, 0 = linear, +1 = slow start", "curve");
        kit::setupToggle(invert, "Inv", "Invert: macro 1 gives Min, macro 0 gives Max", palette().accentDim);
        kit::setupToggle(bipolar, "Bi", "Bipolar: modulates around the parameter's own value (macro 0.5 = no change)", palette().accentDim);
        invert.setComponentID(id + "invert");
        bipolar.setComponentID(id + "bipolar");
        invert.onClick = [this] { kit::refreshToggleText(invert, "Inv"); write(); };
        bipolar.onClick = [this] { kit::refreshToggleText(bipolar, "Bi"); write(); };
        remove.setButtonText("Remove");
        kit::setupButton(remove, "Remove this destination", id + "remove");
        remove.onClick = [this] { owner.removeDestination(index); };
        for (auto* c : std::initializer_list<juce::Component*> { &param, &invert, &bipolar, &remove }) addAndMakeVisible(c);
    }
    MacroDest& dest() { return owner.proc.getShow().macros.macros[static_cast<size_t>(owner.macro)].dests[static_cast<size_t>(index)]; }
    void load() {
        const auto& d = dest();
        param.setText(paramDisplayName(owner.proc, d.paramIndex), juce::dontSendNotification);
        param.setTooltip(ParamBank::idForIndex(d.paramIndex));
        minS.setValue(d.min01, juce::dontSendNotification);
        maxS.setValue(d.max01, juce::dontSendNotification);
        curve.setValue(d.curve, juce::dontSendNotification);
        invert.setToggleState(d.invert, juce::dontSendNotification);
        bipolar.setToggleState(d.bipolar, juce::dontSendNotification);
        kit::refreshToggleText(invert, "Inv");
        kit::refreshToggleText(bipolar, "Bi");
    }
    void write() {
        auto& d = dest();
        d.min01 = static_cast<float>(minS.getValue());
        d.max01 = static_cast<float>(maxS.getValue());
        d.curve = static_cast<float>(curve.getValue());
        d.invert = invert.getToggleState();
        d.bipolar = bipolar.getToggleState();
        owner.commit(false);
    }
    void resized() override {
        kit::layoutRow(getLocalBounds().reduced(4, 3), { { &param, 1.8f }, { &minS, 1.0f }, { &maxS, 1.0f }, { &curve, 1.0f },
                                                          { &invert, -60.0f }, { &bipolar, -52.0f }, { &remove, -70.0f } }, 4);
    }
    void paint(juce::Graphics& g) override { if (index % 2 == 1) g.fillAll(palette().panelRaised.withAlpha(0.35f)); }

    MacroEditor& owner;
    int index;
    juce::Label param;
    juce::Slider minS, maxS, curve;
    juce::TextButton invert, bipolar, remove;
};

MacroEditor::MacroEditor(EditorContext& c) : ctx(c), proc(c.proc) {
    for (int m = 0; m < kNumMacros; ++m) {
        auto* b = macroButtons.add(new juce::TextButton());
        b->setClickingTogglesState(false);
        b->setRadioGroupId(4711);
        kit::setupButton(*b, "Edit macro " + juce::String(m + 1), "macro.select" + juce::String(m + 1));
        b->onClick = [this, m] { selectMacro(m); };
        addAndMakeVisible(b);
    }
    name.setTitle("Macro name");
    name.setTooltip("Macro name (shown on the performance strip)");
    name.setComponentID("macro.name");
        name.setJustification(juce::Justification::centredLeft);
    name.onReturnKey = name.onFocusLost = [this] {
        auto& n = proc.getShow().macroNames[static_cast<size_t>(macro)];
        if (n == name.getText()) return;
        n = name.getText().substring(0, 32);
        commit(true);
    };
    addDest.setButtonText("Add destination...");
    kit::setupButton(addDest, "Pick a parameter (searchable, grouped by module) to control with this macro", "macro.addDest");
    addDest.onClick = [this] { ParamPickerPanel::launch(proc, addDest, [this](int idx) { addDestination(idx); }); };
    kit::styleLabel(header, 12.0f, true, true);
    for (auto* t : { "DESTINATION", "MIN", "MAX", "CURVE", "INVERT", "BIPOLAR", "" }) {
        auto* l = colLabels.add(new juce::Label({}, t));
        kit::styleLabel(*l, 10.5f, true, true);
        addAndMakeVisible(l);
    }
    kit::styleLabel(emptyNote, 12.5f, false, true, juce::Justification::centred);
    emptyNote.setText("No destinations yet: use \"Add destination...\" or right-click any control > Assign to Macro.", juce::dontSendNotification);
    view.setViewedComponent(&holder, false);
    view.setScrollBarsShown(true, false);
    for (auto* comp : std::initializer_list<juce::Component*> { &name, &addDest, &header, &emptyNote, &view }) addAndMakeVisible(comp);
    selectMacro(0);
}

MacroEditor::~MacroEditor() = default;

void MacroEditor::selectMacro(int m) {
    macro = juce::jlimit(0, kNumMacros - 1, m);
    value = std::make_unique<ParamSlider>(proc, ParamBank::index(Mod::Macro, macro, 0));
    value->setComponentID("macro.value");
    value->setTooltip("Current macro value (automatable parameter)");
    addAndMakeVisible(*value);
    refresh();
    resized();
}

void MacroEditor::refresh() {
    const auto& show = proc.getShow();
    for (int m = 0; m < macroButtons.size(); ++m) {
        const auto& map = show.macros.macros[static_cast<size_t>(m)];
        macroButtons[m]->setButtonText(juce::String(m + 1) + " " + show.macroNames[static_cast<size_t>(m)] + " (" + juce::String(map.count) + ")");
        macroButtons[m]->setToggleState(m == macro, juce::dontSendNotification);
    }
    if (!name.hasKeyboardFocus(true)) name.setText(show.macroNames[static_cast<size_t>(macro)], false);
    const auto& map = show.macros.macros[static_cast<size_t>(macro)];
    header.setText("Macro " + juce::String(macro + 1) + ": " + juce::String(map.count) + " / " + juce::String(MacroMap::kMaxDests) + " destinations", juce::dontSendNotification);
    addDest.setEnabled(map.count < MacroMap::kMaxDests);
    if (rows.size() != map.count) {
        rows.clear();
        for (int d = 0; d < map.count; ++d) holder.addAndMakeVisible(rows.add(new DestRow(*this, d)));
    }
    for (auto* r : rows) r->load();
    emptyNote.setVisible(map.count == 0);
    layoutRows();
}

void MacroEditor::addDestination(int idx) {
    auto& map = proc.getShow().macros.macros[static_cast<size_t>(macro)];
    for (int d = 0; d < map.count; ++d)
        if (map.dests[static_cast<size_t>(d)].paramIndex == idx) { ctx.say("Already a destination of this macro", true); return; }
    if (map.count >= MacroMap::kMaxDests) { ctx.say("Macro is full (16 destinations)", true); return; }
    MacroDest dest;
    dest.paramIndex = idx;
    map.dests[static_cast<size_t>(map.count++)] = dest;
    commit(true);
    ctx.say("Macro " + juce::String(macro + 1) + " now controls " + paramDisplayName(proc, idx));
}

void MacroEditor::removeDestination(int d) {
    auto& map = proc.getShow().macros.macros[static_cast<size_t>(macro)];
    if (d < 0 || d >= map.count) return;
    for (int k = d; k + 1 < map.count; ++k) map.dests[static_cast<size_t>(k)] = map.dests[static_cast<size_t>(k + 1)];
    --map.count;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MacroEditor>(this)] { if (safe != nullptr) safe->commit(true); });
}

void MacroEditor::commit(bool notifyListeners) {
    if (notifyListeners) ctx.showModelChanged();     // applies + refreshes names on the performance strip / inspector
    else proc.applyShowChanges();
    if (notifyListeners) refresh();
}

void MacroEditor::paint(juce::Graphics&) {}

void MacroEditor::layoutRows() {
    const int rowH = 32;
    holder.setSize(juce::jmax(10, view.getWidth() - view.getScrollBarThickness()), rowH * rows.size());
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, i * rowH, holder.getWidth(), rowH);
}

void MacroEditor::resized() {
    auto r = getLocalBounds().reduced(10, 8);
    auto left = r.removeFromLeft(juce::jlimit(130, 220, r.getWidth() / 6));
    const int bh = juce::jlimit(24, 34, left.getHeight() / kNumMacros - 4);
    for (auto* b : macroButtons) { b->setBounds(left.removeFromTop(bh)); left.removeFromTop(4); }
    r.removeFromLeft(10);
    auto top = r.removeFromTop(28);
    kit::layoutRow(top, { { &name, 1.0f }, { value.get(), 1.6f }, { &addDest, -150.0f } });
    r.removeFromTop(6);
    header.setBounds(r.removeFromTop(18));
    kit::layoutRow(r.removeFromTop(16).reduced(4, 0).withTrimmedRight(view.getScrollBarThickness()),
                   { { colLabels[0], 1.8f }, { colLabels[1], 1.0f }, { colLabels[2], 1.0f }, { colLabels[3], 1.0f },
                     { colLabels[4], -60.0f }, { colLabels[5], -52.0f }, { colLabels[6], -70.0f } }, 4);
    view.setBounds(r);
    emptyNote.setBounds(r.withHeight(juce::jmin(60, r.getHeight())));
    layoutRows();
}
}
