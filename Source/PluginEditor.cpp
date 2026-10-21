#include "PluginEditor.h"
#include "UI/Pages.h"

using namespace crowdmike;
using namespace crowdmike::ui;

CrowdMikeAudioProcessorEditor::CrowdMikeAudioProcessorEditor(CrowdMikeAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p), context(p), statusBar(context), navRail(context),
      inspector(context), strip(context)
{
    setLookAndFeel(&lookAndFeel);
    context.navigate = [this](PageId id) { showPage(id); };
    context.addListener(this);
    addAndMakeVisible(statusBar);
    addAndMakeVisible(navRail);
    addAndMakeVisible(inspector);
    addAndMakeVisible(strip);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    applyUiSettings();
    setResizable(true, true);
    setResizeLimits(1100, 650, 4096, 2304);
    setSize(1280, 720);
    showPage(PageId::Live);
}

CrowdMikeAudioProcessorEditor::~CrowdMikeAudioProcessorEditor()
{
    context.removeListener(this);
    removeKeyListener(this);
    context.navigate = nullptr;
    setLookAndFeel(nullptr);
}

void CrowdMikeAudioProcessorEditor::applyUiSettings()
{
    const auto& ui = processor.getShow().ui;
    setHighContrast(ui.highContrast);
    lookAndFeel.refreshColours();
    setScaleFactor(ui.scale > 0.0f ? ui.scale : 1.0f);
    tooltips.setMillisecondsBeforeTipAppears(ui.tooltips ? 600 : 0x3fffffff);   // "tooltips off" = never appear
    inspectorVisible = !ui.compact;
    repaint();
}

void CrowdMikeAudioProcessorEditor::showModelChanged()
{
    applyUiSettings();
    resized();
}

Page* CrowdMikeAudioProcessorEditor::getPage(PageId id)
{
    auto& slot = pages[static_cast<size_t>(id)];
    if (slot == nullptr) {
        switch (id) {
            case PageId::Live: slot = makeLivePage(context); break;
            case PageId::Inputs: slot = makeInputsPage(context); break;
            case PageId::Groups: slot = makeGroupsPage(context); break;
            case PageId::Fx: slot = makeFxPage(context); break;
            case PageId::Matrix: slot = makeMatrixPage(context); break;
            case PageId::Record: slot = makeRecordPage(context); break;
            case PageId::Playback: slot = makePlaybackPage(context); break;
            case PageId::Scenes: slot = makeScenesPage(context); break;
            case PageId::Settings: slot = makeSettingsPage(context); break;
            case PageId::Diagnostics: slot = makeDiagnosticsPage(context); break;
            case PageId::Count: break;
        }
        if (slot != nullptr) addChildComponent(*slot);
    }
    return slot.get();
}

void CrowdMikeAudioProcessorEditor::showPage(PageId id)
{
    for (size_t i = 0; i < pages.size(); ++i)
        if (pages[i] != nullptr && static_cast<PageId>(i) != id && pages[i]->isVisible()) { pages[i]->setVisible(false); pages[i]->pageHidden(); }
    current = id;
    if (auto* page = getPage(id)) {
        page->setVisible(true);
        resized();
        page->pageShown();
    }
    navRail.setCurrent(id);
}

void CrowdMikeAudioProcessorEditor::paint(juce::Graphics& g) { g.fillAll(palette().background); }

void CrowdMikeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    const bool compact = processor.getShow().ui.compact || getWidth() < 1180;
    statusBar.setBounds(r.removeFromTop(44));
    strip.setBounds(r.removeFromBottom(compact ? 84 : 96));
    navRail.setBounds(r.removeFromLeft(compact ? 96 : 118));
    const bool showInspector = inspectorVisible && !compact && current != PageId::Settings && current != PageId::Matrix;
    inspector.setVisible(showInspector);
    if (showInspector) inspector.setBounds(r.removeFromRight(juce::jlimit(280, 360, getWidth() / 5)));
    if (auto* page = pages[static_cast<size_t>(current)].get()) page->setBounds(r);
}

bool CrowdMikeAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    // Pages: F1..F10
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i)
        if (code == juce::KeyPress::F1Key + i) { showPage(static_cast<PageId>(i)); return true; }
    if (code == juce::KeyPress::F12Key) {
        auto* p = processor.getParameters().getParameter(ParamBank::paramId(Mod::Global, 0, params::GlobalP::Panic));
        processor.getHub().setPanic(p == nullptr || p->getValue() < 0.5f);
        return true;
    }
    if (mods.isCommandDown() || mods.isCtrlDown()) {
        if (code == 'Z' || code == 'z') { if (mods.isShiftDown()) processor.getUndoManager().redo(); else processor.getUndoManager().undo(); return true; }
        if (code == 'Y' || code == 'y') { processor.getUndoManager().redo(); return true; }
        if (code == juce::KeyPress::rightKey) { processor.getScenes().next(); return true; }
        if (code == juce::KeyPress::leftKey) { processor.getScenes().previous(); return true; }
        if (code >= '1' && code <= '9') { processor.recallScene(code - '1' + (mods.isShiftDown() ? 9 : 0)); return true; }
        if (code == 'R' || code == 'r') { processor.getHub().execute([] { OscAction a; a.kind = OscAction::Kind::RecordToggle; return a; }()); return true; }
        if (code == 'M' || code == 'm') { processor.addMarker(5); context.say("Marker added"); return true; }
        if (code == 'I' || code == 'i') { inspectorVisible = !inspectorVisible; resized(); return true; }
    }
    // Clip hotkeys (Playback page assignments, stored in ShowModel::playback); never while typing into a text field.
    if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) == nullptr) {
        if (const int slot = processor.findClipForHotkey(key.getTextDescription()); slot >= 0) {
            processor.getPlayback().trigger(slot);
            return true;
        }
    }
    return false;
}

bool CrowdMikeAudioProcessorEditor::keyStateChanged(bool, juce::Component*)
{
    // Momentary performance keys: E = echo throw, V = reverse, K = stadium reverb tail kill (held).
    const bool e = juce::KeyPress::isKeyCurrentlyDown('E');
    const bool v = juce::KeyPress::isKeyCurrentlyDown('V');
    const bool k = juce::KeyPress::isKeyCurrentlyDown('K');
    const bool typing = dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    if (typing) return false;
    auto set = [this](Mod m, int id, bool on) {
        if (auto* p = processor.getBank().parameterAt(ParamBank::index(m, 0, id))) p->setValueNotifyingHost(on ? 1.0f : 0.0f);
    };
    if (e != echoKeyDown) { echoKeyDown = e; set(Mod::FxEcho, params::EchoP::Trigger, e); }
    if (v != reverseKeyDown) { reverseKeyDown = v; set(Mod::FxReverse, params::ReverseP::Trigger, v); }
    if (k != reverbKillKeyDown) { reverbKillKeyDown = k; set(Mod::FxReverb, params::ReverbP::TailKill, k); }
    return false;
}
