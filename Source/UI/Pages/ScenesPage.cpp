// Scenes page (PRODUCT_SPEC section 5, CONTROL_SPEC sections 2-4, 8): 32 scene slots (store / recall / rename / notes /
// duplicate / clear / next / previous), recall scope + ramp, locked parameters, per-scene MIDI / OSC / hotkey info,
// macros editor, wiring templates + preset browser, undo / redo with history.
#include "UI/MacroEditor.h"
#include "UI/PageKit.h"
#include "UI/Pages.h"
#include "UI/ParamPicker.h"
#include "UI/PresetBrowser.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
namespace {
struct ScopeDef { unsigned bit; const char* name; const char* tip; };
const ScopeDef kScopes[] = {
    { ScopeInputs, "Inputs", "Input channel parameters" }, { ScopeGroups, "Groups", "Group bus parameters" },
    { ScopeFx, "FX", "Effect parameters" }, { ScopeMaster, "Master", "Master chain parameters" },
    { ScopeOutputs, "Outputs", "Output parameters (trims, delays, limiters)" }, { ScopeRouting, "Routing", "Routing matrices" },
    { ScopeNames, "Names", "Port names, labels and zones" }, { ScopeMacros, "Macros", "Macro assignments and names" } };

// ---------------------------------------------------------------- recall options (scope, ramp, locked parameters)
class RecallPanel final : public juce::Component, private juce::ListBoxModel {
public:
    explicit RecallPanel(EditorContext& c) : ctx(c), proc(c.proc) {
        for (const auto& s : kScopes) {
            auto* t = scopes.add(new juce::ToggleButton(s.name));
            t->setTooltip(juce::String("Recall ") + s.tip);
            t->setComponentID(juce::String("scenes.scope.") + s.name);
            t->onClick = [this] { writeScope(); };
            addAndMakeVisible(t);
        }
        ramp.setSliderStyle(juce::Slider::LinearHorizontal);
        ramp.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        ramp.setRange(10.0, 500.0, 1.0);
        ramp.setTextValueSuffix(" ms");
        ramp.setTitle("Recall ramp time");
        ramp.setTooltip("Safe recall: parameters glide to the scene values over this time");
        ramp.setComponentID("scenes.ramp");
        ramp.onValueChange = [this] { proc.getShow().ui.recallRampMs = static_cast<float>(ramp.getValue()); };
        locked.setModel(this);
        locked.setRowHeight(22);
        locked.setTitle("Locked parameters (excluded from scene recall)");
        locked.setComponentID("scenes.locked");
        unlock.setButtonText("Unlock selected");
        kit::setupButton(unlock, "Remove the selected parameter from the locked list", "scenes.unlock");
        unlock.onClick = [this] { unlockRow(locked.getSelectedRow()); };
        unlockAll.setButtonText("Unlock all");
        kit::setupButton(unlockAll, "Clear the locked-parameter list", "scenes.unlockAll");
        unlockAll.onClick = [this] { proc.getShow().lockedParams.clear(); refresh(); ctx.say("All parameters unlocked"); };
        lockParam.setButtonText("Lock parameter...");
        kit::setupButton(lockParam, "Pick a parameter that scene recall must not change", "scenes.lockParam");
        lockParam.onClick = [this] {
            ParamPickerPanel::launch(proc, lockParam, [this](int idx) { proc.getShow().setLocked(idx, true); refresh(); ctx.say(paramDisplayName(proc, idx) + " locked against recall"); });
        };
        for (auto* l : { &scopeTitle, &rampTitle, &lockTitle }) { kit::styleLabel(*l, 12.0f, true, true); addAndMakeVisible(*l); }
        scopeTitle.setText("RECALL SCOPE (keyboard / GUI recalls)", juce::dontSendNotification);
        rampTitle.setText("RAMP", juce::dontSendNotification);
        for (auto* comp : std::initializer_list<juce::Component*> { &ramp, &locked, &unlock, &unlockAll, &lockParam }) addAndMakeVisible(comp);
    }

    void refresh() {
        const auto& ui = proc.getShow().ui;
        for (int i = 0; i < scopes.size(); ++i) scopes[i]->setToggleState((ui.recallScope & kScopes[i].bit) != 0, juce::dontSendNotification);
        ramp.setValue(ui.recallRampMs, juce::dontSendNotification);
        lockTitle.setText("LOCKED PARAMETERS (" + juce::String(static_cast<int>(proc.getShow().lockedParams.size())) + ")", juce::dontSendNotification);
        locked.updateContent();
        locked.repaint();
        unlock.setEnabled(!proc.getShow().lockedParams.empty());
        unlockAll.setEnabled(!proc.getShow().lockedParams.empty());
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 8);
        scopeTitle.setBounds(r.removeFromTop(18));
        const int cols = r.getWidth() > 600 ? 8 : 4;
        const int w = r.getWidth() / cols;
        for (int row = 0; row < 8 / cols; ++row) {
            auto line = r.removeFromTop(26);
            for (int c2 = 0; c2 < cols; ++c2) scopes[row * cols + c2]->setBounds(line.removeFromLeft(w));
        }
        r.removeFromTop(8);
        auto rr = r.removeFromTop(26);
        rampTitle.setBounds(rr.removeFromLeft(50));
        ramp.setBounds(rr.removeFromLeft(juce::jmin(420, rr.getWidth())));
        r.removeFromTop(10);
        lockTitle.setBounds(r.removeFromTop(18));
        kit::layoutRow(r.removeFromBottom(28), { { &lockParam, 1.0f }, { &unlock, 1.0f }, { &unlockAll, 1.0f } });
        r.removeFromBottom(6);
        locked.setBounds(r);
    }

private:
    void writeScope() {
        unsigned scope = 0;
        for (int i = 0; i < scopes.size(); ++i) if (scopes[i]->getToggleState()) scope |= kScopes[i].bit;
        if (scope == 0) {   // CONTROL_SPEC: an empty scope is never accepted; put the last non-empty selection back
            ctx.say("Recall scope cannot be empty: keep at least one category selected", true);
            refresh();
            return;
        }
        proc.getShow().ui.recallScope = scope;
    }
    void unlockRow(int row) {
        auto& l = proc.getShow().lockedParams;
        if (row < 0 || row >= static_cast<int>(l.size())) { ctx.say("Select a locked parameter first", true); return; }
        const int idx = l[static_cast<size_t>(row)];
        proc.getShow().setLocked(idx, false);
        refresh();
        ctx.say(paramDisplayName(proc, idx) + " unlocked");
    }
    int getNumRows() override { return static_cast<int>(proc.getShow().lockedParams.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
        const auto& l = proc.getShow().lockedParams;
        if (row < 0 || row >= static_cast<int>(l.size())) return;
        const int idx = l[static_cast<size_t>(row)];
        if (sel) g.fillAll(palette().accentDim);
        g.setColour(palette().text);
        g.setFont(font(12.5f));
        g.drawText("LOCKED  " + paramDisplayName(proc, idx), 6, 0, w * 2 / 3, h, juce::Justification::centredLeft, true);
        g.setColour(palette().textMuted);
        g.setFont(monoFont(11.0f));
        g.drawText(ParamBank::idForIndex(idx), w * 2 / 3, 0, w / 3 - 6, h, juce::Justification::centredRight, true);
    }
    void deleteKeyPressed(int row) override { unlockRow(row); }

    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
    juce::OwnedArray<juce::ToggleButton> scopes;
    juce::Slider ramp;
    juce::ListBox locked;
    juce::TextButton unlock, unlockAll, lockParam;
    juce::Label scopeTitle, rampTitle, lockTitle;
};

// ---------------------------------------------------------------- undo history
class HistoryPanel final : public juce::Component, private juce::ListBoxModel {
public:
    explicit HistoryPanel(CrowdMikeAudioProcessor& p) : proc(p) {
        list.setModel(this);
        list.setRowHeight(22);
        list.setTitle("Undo history");
        kit::styleLabel(note, 11.5f, false, true);
        note.setText("Newest first. Parameter, routing and preset changes are undoable (Ctrl+Z / Ctrl+Shift+Z). Meters are never part of undo.", juce::dontSendNotification);
        addAndMakeVisible(list);
        addAndMakeVisible(note);
    }
    void refresh() {
        auto& um = proc.getUndoManager();
        auto undo = um.getUndoDescriptions(), redo = um.getRedoDescriptions();
        juce::StringArray next;
        for (int i = redo.size() - 1; i >= 0; --i) next.add(juce::String::fromUTF8("\xe2\x86\xb7 redo: ") + (redo[i].isNotEmpty() ? redo[i] : juce::String("(change)")));
        for (const auto& u : undo) next.add(juce::String::fromUTF8("\xe2\x86\xb6 undo: ") + (u.isNotEmpty() ? u : juce::String("(change)")));
        if (next != items) { items = next; list.updateContent(); list.repaint(); }
    }
    void resized() override { auto r = getLocalBounds().reduced(10, 8); note.setBounds(r.removeFromTop(20)); list.setBounds(r); }
private:
    int getNumRows() override { return items.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
        if (sel) g.fillAll(palette().accentDim);
        g.setColour(items[row].contains("redo") ? palette().textMuted : palette().text);
        g.setFont(font(12.5f));
        g.drawText(items[row], 6, 0, w - 12, h, juce::Justification::centredLeft, true);
    }
    CrowdMikeAudioProcessor& proc;
    juce::ListBox list;
    juce::Label note;
    juce::StringArray items;
};

// ---------------------------------------------------------------- page
class ScenesPage final : public Page, private Tickable, private juce::ListBoxModel {
public:
    explicit ScenesPage(EditorContext& c) : Page(c), recall(c), macros(c), presets(c), history(c.proc) {
        sceneList.setModel(this);
        sceneList.setRowHeight(26);
        sceneList.setTitle("Scene slots");
        sceneList.setComponentID("scenes.list");
        sceneList.setTooltip("Click to select, double-click or Enter to recall");
        addAndMakeVisible(sceneList);

        auto button = [this](juce::TextButton& b, const juce::String& text, const juce::String& tip, const juce::String& id, std::function<void()> fn) {
            b.setButtonText(text);
            kit::setupButton(b, tip, id);
            b.onClick = std::move(fn);
            addAndMakeVisible(b);
        };
        button(store, "Store", "Capture the current show state into the selected slot (asks before overwriting)", "scenes.capture", [this] { storeSelected(); });
        button(recallB, "Recall", "Recall the selected scene with the current scope and ramp", "scenes.recall", [this] { recallSelected(); });
        button(prev, "< Prev", "Recall the previous used scene (Ctrl+Left)", "scenes.prev", [this] { proc.getScenes().previous(proc.getShow().ui.recallScope); afterRecall(); });
        button(next, "Next >", "Recall the next used scene (Ctrl+Right)", "scenes.next", [this] { proc.getScenes().next(proc.getShow().ui.recallScope); afterRecall(); });
        button(duplicate, "Duplicate", "Copy the selected scene into the next empty slot", "scenes.duplicate", [this] { duplicateSelected(); });
        button(clearB, "Clear...", "Empty the selected slot (asks for confirmation)", "scenes.clear", [this] { clearSelected(false); });
        button(learn, "MIDI learn", "MIDI learn: the next note / CC recalls this scene", "scenes.learn", [this] {
            proc.getHub().startLearn({ BindTarget::SceneSelect, selected, BindMode::Trigger });
            ctx.say("MIDI learn: move a control to recall scene " + juce::String(selected + 1));
        });
        button(clearMidi, "Clear MIDI", "Remove the MIDI bindings of this scene", "scenes.clearMidi", [this] { proc.getHub().removeBindingsFor(BindTarget::SceneSelect, selected); loadSelected(); });
        button(undo, juce::String::fromUTF8("\xe2\x86\xb6 Undo"), "Undo the last change (Ctrl+Z)", "scenes.undo", [this] { proc.getUndoManager().undo(); ctx.showModelChanged(); refreshUndo(); });
        button(redo, juce::String::fromUTF8("\xe2\x86\xb7 Redo"), "Redo (Ctrl+Shift+Z / Ctrl+Y)", "scenes.redo", [this] { proc.getUndoManager().redo(); ctx.showModelChanged(); refreshUndo(); });

        name.setTitle("Scene name");
        name.setTooltip("Scene name (Enter to apply)");
        name.setComponentID("scenes.name");
        name.setJustification(juce::Justification::centredLeft);
        name.onReturnKey = name.onFocusLost = [this] {
            auto& s = proc.getScenes().at(selected);
            if (!s.used || s.name == name.getText()) return;
            s.name = name.getText().substring(0, 48);
            sceneList.repaintRow(selected);
            ctx.showModelChanged();
        };
        notes.setMultiLine(true, true);
        notes.setReturnKeyStartsNewLine(true);
        notes.setTitle("Scene notes");
        notes.setTooltip("Notes for this scene (cues, who to watch, ...)");
        notes.setComponentID("scenes.notes");
        notes.onFocusLost = [this] {
            auto& s = proc.getScenes().at(selected);
            if (s.used && s.notes != notes.getText()) { s.notes = notes.getText(); sceneList.repaintRow(selected); }
        };
        for (auto* l : { &assignInfo, &undoInfo, &currentInfo, &lName, &lNotes }) { kit::styleLabel(*l, 12.0f, false, true); addAndMakeVisible(*l); }
        lName.setText("Name", juce::dontSendNotification);
        lNotes.setText("Notes", juce::dontSendNotification);
        kit::styleLabel(currentInfo, 14.0f, true, false);
        addAndMakeVisible(name);
        addAndMakeVisible(notes);

        tabs.addTab("Recall & locks", palette().panel, &recall, false);
        tabs.addTab("Macros", palette().panel, &macros, false);
        tabs.addTab("Templates & presets", palette().panel, &presets, false);
        tabs.addTab("Undo history", palette().panel, &history, false);
        tabs.setTabBarDepth(30);
        tabs.setOutline(0);
        addAndMakeVisible(tabs);
        selectSlot(0);
    }
    ~ScenesPage() override { sceneList.setModel(nullptr); }

    void pageShown() override { active = true; refreshAll(); }
    void pageHidden() override { active = false; }

    void paint(juce::Graphics& g) override {
        Page::paint(g);
        kit::paintSection(g, listArea, "Scenes");
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10);
        auto header = r.removeFromTop(32);
        undo.setBounds(header.removeFromLeft(90));
        header.removeFromLeft(6);
        redo.setBounds(header.removeFromLeft(90));
        header.removeFromLeft(10);
        currentInfo.setBounds(header.removeFromRight(juce::jmin(360, header.getWidth() / 2)));
        undoInfo.setBounds(header);
        r.removeFromTop(8);
        listArea = r.removeFromLeft(juce::jlimit(330, 520, r.getWidth() * 36 / 100));
        r.removeFromLeft(8);
        tabs.setBounds(r);
        auto c = kit::sectionContent(listArea);
        const int bh = 28;
        auto assign = c.removeFromBottom(36);
        c.removeFromBottom(4);
        auto learnRow = c.removeFromBottom(bh);
        c.removeFromBottom(6);
        auto notesRow = c.removeFromBottom(juce::jlimit(40, 90, c.getHeight() / 6));
        c.removeFromBottom(6);
        auto nameRow = c.removeFromBottom(bh);
        c.removeFromBottom(6);
        auto row2 = c.removeFromBottom(bh);
        c.removeFromBottom(6);
        auto row1 = c.removeFromBottom(bh);
        c.removeFromBottom(6);
        sceneList.setBounds(c);
        kit::layoutRow(row1, { { &store, 1.0f }, { &recallB, 1.0f }, { &prev, 1.0f }, { &next, 1.0f } });
        kit::layoutRow(row2, { { &duplicate, 1.0f }, { &clearB, 1.0f } });
        lName.setBounds(nameRow.removeFromLeft(50));
        name.setBounds(nameRow);
        lNotes.setBounds(notesRow.removeFromLeft(50).withHeight(bh));
        notes.setBounds(notesRow);
        kit::layoutRow(learnRow, { { &learn, 1.0f }, { &clearMidi, 1.0f } });
        assignInfo.setBounds(assign);
    }

    void tick() override {
        if (!active || ++tickCount % 6 != 0) return;
        const int cur = proc.getScenes().current();
        if (cur != lastCurrent) { lastCurrent = cur; sceneList.repaint(); updateCurrent(); }
        refreshUndo();
        if (tabs.getCurrentTabIndex() == 3) history.refresh();
        const bool learning = proc.getHub().isLearning();
        if (learning != wasLearning) { wasLearning = learning; if (!learning) loadSelected(); learn.setButtonText(learning ? "Learning..." : "MIDI learn"); }
    }

private:
    void showModelChanged() override { refreshAll(); }

    void refreshAll() {
        sceneList.updateContent();
        sceneList.repaint();
        loadSelected();
        recall.refresh();
        macros.refresh();
        presets.refresh();
        history.refresh();
        refreshUndo();
        updateCurrent();
    }

    void refreshUndo() {
        auto& um = proc.getUndoManager();
        undo.setEnabled(um.canUndo());
        redo.setEnabled(um.canRedo());
        undoInfo.setText((um.canUndo() ? "Undo: " + um.getUndoDescription() : juce::String("Nothing to undo"))
                             + (um.canRedo() ? "    Redo: " + um.getRedoDescription() : juce::String()),
                         juce::dontSendNotification);
    }

    void updateCurrent() {
        const int cur = proc.getScenes().current();
        currentInfo.setText(cur >= 0 ? "Current: " + juce::String(cur + 1) + "  " + proc.getScenes().at(cur).name : juce::String("No scene recalled"), juce::dontSendNotification);
    }

    void selectSlot(int s) {
        selected = juce::jlimit(0, kNumScenes - 1, s);
        if (sceneList.getSelectedRow() != selected) sceneList.selectRow(selected);
        loadSelected();
    }

    void loadSelected() {
        const auto& s = proc.getScenes().at(selected);
        if (!name.hasKeyboardFocus(true)) name.setText(s.used ? s.name : juce::String(), false);
        name.setTextToShowWhenEmpty(s.used ? juce::String("(unnamed)") : "Empty slot: Store to capture as \"Scene " + juce::String(selected + 1) + "\"", palette().textMuted);
        if (!notes.hasKeyboardFocus(true)) notes.setText(s.notes, false);
        recallB.setEnabled(s.used);
        duplicate.setEnabled(s.used);
        clearB.setEnabled(s.used);
        const int n = selected + 1;
        const juce::String hotkey = n <= 9 ? "Ctrl+" + juce::String(n) : (n <= 18 ? "Ctrl+Shift+" + juce::String(n - 9) : juce::String("-"));
        assignInfo.setText("Scene " + juce::String(n) + "   MIDI: " + kit::bindingSummary(proc.getShow(), BindTarget::SceneSelect, selected)
                               + "\nOSC: /crowdmike/scene/" + juce::String(n) + "/select    Key: " + hotkey,
                           juce::dontSendNotification);
    }

    void storeSelected() {
        auto& s = proc.getScenes().at(selected);
        const auto doStore = [this] {
            auto& sc = proc.getScenes().at(selected);
            const auto label = name.getText().isNotEmpty() ? name.getText() : (sc.used && sc.name.isNotEmpty() ? sc.name : "Scene " + juce::String(selected + 1));
            const auto keepNotes = notes.getText();
            proc.getScenes().capture(selected, label);
            proc.getScenes().at(selected).notes = keepNotes;
            sceneList.updateContent();
            sceneList.repaint();
            loadSelected();
            updateCurrent();
            ctx.say("Scene " + juce::String(selected + 1) + " stored: " + label);
        };
        if (!s.used) { doStore(); return; }
        juce::Component::SafePointer<ScenesPage> safe(this);
        kit::confirmAsync(this, "Overwrite scene?", "Scene " + juce::String(selected + 1) + " \"" + s.name + "\" will be replaced by the current state.", "Overwrite",
                          [safe, doStore] { if (safe != nullptr) doStore(); });
    }

    void recallSelected() {
        if (!proc.getScenes().at(selected).used) { ctx.say("Scene " + juce::String(selected + 1) + " is empty", true); return; }
        proc.recallScene(selected);
        afterRecall();
    }

    void afterRecall() {
        const int cur = proc.getScenes().current();
        if (cur >= 0) { ctx.say("Recalled scene " + juce::String(cur + 1) + ": " + proc.getScenes().at(cur).name); selectSlot(cur); }
        ctx.showModelChanged();
    }

    void duplicateSelected() {
        if (!proc.getScenes().at(selected).used) return;
        for (int i = 1; i < kNumScenes; ++i) {
            const int to = (selected + i) % kNumScenes;
            if (proc.getScenes().at(to).used) continue;
            proc.getScenes().duplicate(selected, to);
            sceneList.updateContent();
            selectSlot(to);
            ctx.say("Scene duplicated into slot " + juce::String(to + 1));
            return;
        }
        ctx.say("No empty slot to duplicate into", true);
    }

    void clearSelected(bool confirmed) {
        if (!proc.getScenes().at(selected).used) return;
        if (!confirmed) {
            juce::Component::SafePointer<ScenesPage> safe(this);
            kit::confirmAsync(this, "Clear scene?", "Scene " + juce::String(selected + 1) + " \"" + proc.getScenes().at(selected).name + "\" will be emptied.", "Clear",
                              [safe] { if (safe != nullptr) safe->clearSelected(true); });
            return;
        }
        proc.getScenes().clear(selected);
        sceneList.updateContent();
        sceneList.repaint();
        loadSelected();
        ctx.say("Scene " + juce::String(selected + 1) + " cleared");
    }

    // ---- scene list model
    int getNumRows() override { return kNumScenes; }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
        const auto& s = proc.getScenes().at(row);
        const auto& p = palette();
        const bool current = proc.getScenes().current() == row;
        if (sel) g.fillAll(p.accentDim);
        else if (row % 2 == 1) g.fillAll(p.panelRaised.withAlpha(0.35f));
        g.setFont(monoFont(12.0f));
        g.setColour(p.textMuted);
        g.drawText(juce::String(row + 1).paddedLeft(' ', 2), 6, 0, 26, h, juce::Justification::centredLeft);
        g.setFont(font(13.0f, current));
        g.setColour(s.used ? p.text : p.textMuted);
        g.drawText(s.used ? (s.name.isNotEmpty() ? s.name : juce::String("(unnamed)")) : juce::String("- empty -"), 34, 0, w - 160, h, juce::Justification::centredLeft, true);
        juce::String tags;
        if (s.used && s.notes.isNotEmpty()) tags << "NOTES  ";
        if (kit::bindingSummary(proc.getShow(), BindTarget::SceneSelect, row) != "-") tags << "MIDI  ";
        if (current) tags << juce::String::fromUTF8("\xe2\x97\x8f LIVE");
        g.setFont(font(11.0f, current));
        g.setColour(current ? p.ok : p.textMuted);
        g.drawText(tags, w - 130, 0, 124, h, juce::Justification::centredRight, true);
    }
    void selectedRowsChanged(int row) override { if (row >= 0 && row != selected) { selected = row; loadSelected(); } }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { selectSlot(row); recallSelected(); }
    void returnKeyPressed(int row) override { selectSlot(row); recallSelected(); }
    void deleteKeyPressed(int row) override { selectSlot(row); clearSelected(false); }

    RecallPanel recall;
    MacroEditor macros;
    PresetBrowser presets;
    HistoryPanel history;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::ListBox sceneList;
    juce::TextButton store, recallB, prev, next, duplicate, clearB, learn, clearMidi, undo, redo;
    juce::TextEditor name, notes;
    juce::Label assignInfo, undoInfo, currentInfo, lName, lNotes;
    juce::Rectangle<int> listArea;
    bool active = false, wasLearning = false;
    int selected = 0, tickCount = 0, lastCurrent = -2;
};
}
std::unique_ptr<Page> makeScenesPage(EditorContext& c) { return std::make_unique<ScenesPage>(c); }
}
