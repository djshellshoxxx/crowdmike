#include "ParamPicker.h"
#include "UI/Theme.h"

namespace crowdmike::ui {
juce::String moduleDisplayName(Mod m) noexcept {
    switch (m) {
        case Mod::Input: return "Inputs";
        case Mod::Group: return "Groups";
        case Mod::Output: return "Outputs";
        case Mod::Master: return "Master";
        case Mod::Global: return "Global";
        case Mod::FxDelay: return "FX Delay";
        case Mod::FxEcho: return "FX Echo";
        case Mod::FxTape: return "FX Tape";
        case Mod::FxReverse: return "FX Reverse";
        case Mod::FxNoise: return "FX Noise";
        case Mod::Macro: return "Macros";
        case Mod::Tempo: return "Tempo";
        case Mod::Count: break;
    }
    return "All modules";
}

Mod moduleOfIndex(int idx) noexcept {
    for (int m = static_cast<int>(Mod::Count) - 1; m >= 0; --m)
        if (idx >= ParamBank::baseIndex(static_cast<Mod>(m), 0)) return static_cast<Mod>(m);
    return Mod::Input;
}

juce::String paramDisplayName(CrowdMikeAudioProcessor& proc, int idx) {
    if (auto* p = proc.getBank().parameterAt(idx)) return p->getName(80);
    return ParamBank::idForIndex(idx);
}

std::vector<int> searchParams(CrowdMikeAudioProcessor& proc, const juce::String& query, Mod module, int maxResults) {
    std::vector<int> out;
    const auto words = juce::StringArray::fromTokens(query.trim(), " ", "");
    const int total = ParamBank::totalCount();
    for (int i = 0; i < total && static_cast<int>(out.size()) < maxResults; ++i) {
        const Mod m = moduleOfIndex(i);
        if (m == Mod::Macro) continue;
        if (module != Mod::Count && m != module) continue;
        const auto name = paramDisplayName(proc, i) + " " + ParamBank::idForIndex(i);
        bool ok = true;
        for (const auto& w : words) ok = ok && name.containsIgnoreCase(w);
        if (ok) out.push_back(i);
    }
    return out;
}

ParamPickerPanel::ParamPickerPanel(CrowdMikeAudioProcessor& p, std::function<void(int)> cb) : proc(p), onPick(std::move(cb)) {
    search.setTextToShowWhenEmpty("Search parameters (e.g. \"input 1 gain\")", palette().textMuted);
    search.setTitle("Parameter search");
        search.setJustification(juce::Justification::centredLeft);
    search.setTooltip("Type words of the parameter name; Down moves into the list, Enter picks");
    search.onTextChange = [this] { refresh(); };
    search.onReturnKey = [this] { pick(juce::jmax(0, list.getSelectedRow())); };
    module.setTitle("Module filter");
    module.setTooltip("Limit the list to one module");
    module.addItem("All modules", 1);
    for (int m = 0; m < static_cast<int>(Mod::Count); ++m)
        if (static_cast<Mod>(m) != Mod::Macro) module.addItem(moduleDisplayName(static_cast<Mod>(m)), m + 2);
    module.setSelectedId(1, juce::dontSendNotification);
    module.onChange = [this] { refresh(); };
    list.setModel(this);
    list.setRowHeight(22);
    list.setTitle("Matching parameters");
    count.setFont(font(11.0f));
    count.setColour(juce::Label::textColourId, palette().textMuted);
    for (auto* c : std::initializer_list<juce::Component*> { &search, &module, &list, &count }) addAndMakeVisible(c);
    setSize(460, 420);
    refresh();
}

void ParamPickerPanel::launch(CrowdMikeAudioProcessor& p, juce::Component& anchor, std::function<void(int)> cb) {
    auto panel = std::make_unique<ParamPickerPanel>(p, nullptr);
    auto* raw = panel.get();
    auto* top = anchor.getTopLevelComponent();
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(panel), top->getLocalArea(&anchor, anchor.getLocalBounds()), top);
    raw->onPick = [cb = std::move(cb), boxPtr = juce::Component::SafePointer<juce::CallOutBox>(&box)](int idx) {
        if (cb) cb(idx);
        if (boxPtr != nullptr) boxPtr->dismiss();
    };
    raw->search.grabKeyboardFocus();
}

void ParamPickerPanel::paint(juce::Graphics& g) { g.fillAll(palette().panel); }

void ParamPickerPanel::resized() {
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(26);
    module.setBounds(top.removeFromRight(130));
    top.removeFromRight(6);
    search.setBounds(top);
    r.removeFromTop(6);
    count.setBounds(r.removeFromBottom(18));
    list.setBounds(r);
}

void ParamPickerPanel::refresh() {
    const int sel = module.getSelectedId();
    const Mod m = sel <= 1 ? Mod::Count : static_cast<Mod>(sel - 2);
    results = searchParams(proc, search.getText(), m);
    list.updateContent();
    list.selectRow(0);
    list.repaint();
    count.setText(juce::String(static_cast<int>(results.size())) + (results.size() >= 400 ? "+ matches (refine the search)" : " matches"), juce::dontSendNotification);
}

void ParamPickerPanel::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= static_cast<int>(results.size())) return;
    const int idx = results[static_cast<size_t>(row)];
    if (selected) g.fillAll(palette().accentDim);
    g.setColour(palette().textMuted);
    g.setFont(font(11.0f));
    g.drawText(moduleDisplayName(moduleOfIndex(idx)), 6, 0, 80, h, juce::Justification::centredLeft, true);
    g.setColour(palette().text);
    g.setFont(font(13.0f));
    g.drawText(paramDisplayName(proc, idx), 90, 0, w - 96, h, juce::Justification::centredLeft, true);
}

void ParamPickerPanel::pick(int row) {
    if (row < 0 || row >= static_cast<int>(results.size())) return;
    if (onPick) onPick(results[static_cast<size_t>(row)]);
}
}
