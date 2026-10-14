#include "PresetBrowser.h"
#include "Routing/WiringTemplates.h"
#include "UI/PageKit.h"

namespace crowdmike::ui {
namespace {
struct Target { const char* key; const char* label; Mod mod; };
const Target kTargets[] = { { "input", "Input", Mod::Input }, { "group", "Group", Mod::Group }, { "master", "Master", Mod::Master },
                            { "delay", "FX Delay", Mod::FxDelay }, { "echo", "FX Echo", Mod::FxEcho }, { "tape", "FX Tape", Mod::FxTape },
                            { "reverse", "FX Reverse", Mod::FxReverse }, { "noise", "FX Noise", Mod::FxNoise } };
constexpr int kNumTargets = static_cast<int>(sizeof(kTargets) / sizeof(kTargets[0]));

void paintRow(juce::Graphics& g, int row, int w, int h, bool selected, const juce::String& main, const juce::String& side) {
    if (selected) g.fillAll(palette().accentDim);
    else if (row % 2 == 1) g.fillAll(palette().panelRaised.withAlpha(0.35f));
    g.setColour(palette().text);
    g.setFont(font(12.5f, selected));
    g.drawText(main, 6, 0, w - 12, h, juce::Justification::centredLeft, true);
    if (side.isNotEmpty()) {
        g.setColour(palette().textMuted);
        g.setFont(font(11.0f));
        g.drawText(side, 6, 0, w - 12, h, juce::Justification::centredRight, true);
    }
}
}

class PresetBrowser::TemplateModel final : public juce::ListBoxModel {
public:
    explicit TemplateModel(PresetBrowser& o) : owner(o) {}
    int getNumRows() override { return static_cast<int>(WiringTemplates::Id::Count); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
        paintRow(g, row, w, h, sel, WiringTemplates::name(static_cast<WiringTemplates::Id>(row)), {});
    }
    void selectedRowsChanged(int row) override {
        owner.templateDesc.setText(row >= 0 ? juce::String(WiringTemplates::description(static_cast<WiringTemplates::Id>(row))) : juce::String(), juce::dontSendNotification);
    }
    PresetBrowser& owner;
};

class PresetBrowser::FactoryModel final : public juce::ListBoxModel {
public:
    explicit FactoryModel(PresetBrowser& o) : owner(o) {}
    int getNumRows() override { return static_cast<int>(owner.factory.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
        if (row < 0 || row >= getNumRows()) return;
        const auto* p = owner.factory[static_cast<size_t>(row)];
        paintRow(g, row, w, h, sel, p->name, juce::String(p->blockPrefix).isNotEmpty() ? juce::String(p->blockPrefix) + " block" : juce::String("whole module"));
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { owner.applyFactory(row); }
    void returnKeyPressed(int row) override { owner.applyFactory(row); }
    PresetBrowser& owner;
};

class PresetBrowser::UserModel final : public juce::ListBoxModel {
public:
    explicit UserModel(PresetBrowser& o) : owner(o) {}
    int getNumRows() override { return owner.userFiles.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
        if (row < 0 || row >= getNumRows()) return;
        const auto& f = owner.userFiles.getReference(row);
        paintRow(g, row, w, h, sel, f.getFileNameWithoutExtension(), f.getLastModificationTime().formatted("%Y-%m-%d %H:%M"));
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { if (row >= 0 && row < getNumRows()) owner.loadPresetFile(owner.userFiles[row]); }
    void returnKeyPressed(int row) override { if (row >= 0 && row < getNumRows()) owner.loadPresetFile(owner.userFiles[row]); }
    PresetBrowser& owner;
};

PresetBrowser::PresetBrowser(EditorContext& c) : ctx(c), proc(c.proc) {
    templateModel = std::make_unique<TemplateModel>(*this);
    factoryModel = std::make_unique<FactoryModel>(*this);
    userModel = std::make_unique<UserModel>(*this);
    templates.setModel(templateModel.get());
    factoryList.setModel(factoryModel.get());
    userList.setModel(userModel.get());
    templates.setTitle("Wiring templates");
    factoryList.setTitle("Factory presets");
    userList.setTitle("User presets");
    templates.setComponentID("presets.templates");
    factoryList.setComponentID("presets.factory");
    userList.setComponentID("presets.user");
    for (auto* l : { &templates, &factoryList, &userList }) l->setRowHeight(22);
    kit::styleLabel(templateDesc, 12.0f, false, true, juce::Justification::topLeft);
    templateDesc.setMinimumHorizontalScale(1.0f);

    applyTemplateButton.setButtonText("Apply template...");
    kit::setupButton(applyTemplateButton, "Rewrite the routing (and a few parameters) from the selected template; asks for confirmation; undoable", "presets.applyTemplate");
    applyTemplateButton.onClick = [this] {
        const int row = templates.getSelectedRow();
        if (row < 0) { ctx.say("Select a wiring template first", true); return; }
        juce::Component::SafePointer<PresetBrowser> safe(this);
        kit::confirmAsync(this, "Apply wiring template?",
                          juce::String("\"") + WiringTemplates::name(static_cast<WiringTemplates::Id>(row)) + "\" replaces the current routing matrices. This can be undone.",
                          "Apply", [safe, row] { if (safe != nullptr) safe->applyTemplate(row); });
    };
    target.setTitle("Preset target module");
    target.setTooltip("Module type the factory presets are for");
    target.setComponentID("presets.target");
    for (int t = 0; t < kNumTargets; ++t) target.addItem(kTargets[t].label, t + 1);
    target.setSelectedId(1, juce::dontSendNotification);
    target.onChange = [this] { rebuildFactory(); };
    instance.setTitle("Preset target instance");
    instance.setTooltip("Which input / group the preset is applied to");
    instance.setComponentID("presets.instance");
    applyFactoryButton.setButtonText("Apply preset");
    kit::setupButton(applyFactoryButton, "Apply the selected factory preset to the chosen module instance (undoable)", "presets.applyFactory");
    applyFactoryButton.onClick = [this] { applyFactory(factoryList.getSelectedRow()); };

    saveShow.setButtonText("Save show...");
    kit::setupButton(saveShow, "Save the whole show (parameters, routing, names, macros, recorder / playback references)", "presets.saveShow");
    saveShow.onClick = [this] { savePreset(false); };
    saveRouting.setButtonText("Save routing...");
    kit::setupButton(saveRouting, "Save only the routing matrices as a routing preset", "presets.saveRouting");
    saveRouting.onClick = [this] { savePreset(true); };
    loadFile.setButtonText("Load file...");
    kit::setupButton(loadFile, "Load a show or routing preset file", "presets.loadFile");
    loadFile.onClick = [this] { choosePresetFile(); };
    loadUser.setButtonText("Load selected");
    kit::setupButton(loadUser, "Load the selected user preset (double-click / Enter also load)", "presets.loadUser");
    loadUser.onClick = [this] { const int row = userList.getSelectedRow(); if (row >= 0 && row < userFiles.size()) loadPresetFile(userFiles[row]); };
    refreshButton.setButtonText("Rescan");
    kit::setupButton(refreshButton, "Rescan the user preset folder", "presets.rescan");
    refreshButton.onClick = [this] { refresh(); };

    for (auto* comp : std::initializer_list<juce::Component*> { &templates, &factoryList, &userList, &templateDesc, &applyTemplateButton, &applyFactoryButton,
                                                              &saveShow, &saveRouting, &loadFile, &loadUser, &refreshButton, &target, &instance })
        addAndMakeVisible(comp);
    templates.selectRow(0);
    rebuildFactory();
}

PresetBrowser::~PresetBrowser() {
    templates.setModel(nullptr);
    factoryList.setModel(nullptr);
    userList.setModel(nullptr);
}

Mod PresetBrowser::targetModule() const { return kTargets[juce::jlimit(0, kNumTargets - 1, target.getSelectedId() - 1)].mod; }

void PresetBrowser::rebuildFactory() {
    const int t = juce::jlimit(0, kNumTargets - 1, target.getSelectedId() - 1);
    const bool isFx = kTargets[t].mod >= Mod::FxDelay;
    factory.clear();
    for (const auto* p : PresetManager::factoryPresetsFor(kTargets[t].key))
        if (!isFx || juce::String(p->target) == kTargets[t].key) factory.push_back(p);
    factoryList.updateContent();
    factoryList.repaint();
    instance.clear(juce::dontSendNotification);
    const int n = ParamBank::info(kTargets[t].mod).instances;
    const auto& show = proc.getShow();
    for (int i = 0; i < n; ++i) {
        juce::String label = n == 1 ? juce::String(kTargets[t].label)
                           : kTargets[t].mod == Mod::Input ? show.inputDisplayName(i)
                           : kTargets[t].mod == Mod::Group ? show.groupDisplayName(i) : juce::String(kTargets[t].label) + " " + juce::String(i + 1);
        instance.addItem(label, i + 1);
    }
    instance.setSelectedId(1, juce::dontSendNotification);
    instance.setEnabled(n > 1);
}

void PresetBrowser::refresh() {
    userFiles.clear();
    const auto folder = PresetManager::userPresetFolder();
    if (folder.isDirectory())
        userFiles = folder.findChildFiles(juce::File::findFiles, false, "*.cmpreset;*.xml");
    userFiles.sort();
    userList.updateContent();
    userList.repaint();
    rebuildFactory();
}

void PresetBrowser::applyTemplate(int row) {
    if (row < 0 || row >= static_cast<int>(WiringTemplates::Id::Count)) return;
    proc.applyWiringTemplate(row);
    ctx.showModelChanged();
    ctx.say(juce::String("Wiring template applied: ") + WiringTemplates::name(static_cast<WiringTemplates::Id>(row)));
}

bool PresetBrowser::applyFactory(int row) {
    if (row < 0 || row >= static_cast<int>(factory.size())) { ctx.say("Select a factory preset first", true); return false; }
    proc.getUndoManager().beginNewTransaction(juce::String("Preset ") + factory[static_cast<size_t>(row)]->name);
    const int inst = juce::jmax(0, instance.getSelectedId() - 1);
    const int n = PresetManager::applyFactory(proc.getParameters(), *factory[static_cast<size_t>(row)], targetModule(), inst);
    ctx.say(juce::String("Preset \"") + factory[static_cast<size_t>(row)]->name + "\" applied to " + instance.getText() + " (" + juce::String(n) + " parameters)", n == 0);
    return n > 0;
}

bool PresetBrowser::loadPresetFile(const juce::File& file) {
    const auto tree = PresetManager::loadFromFile(file);
    if (!tree.isValid()) { ctx.say("Cannot load preset (invalid or newer version): " + file.getFileName(), true); return false; }
    proc.getUndoManager().beginNewTransaction("Load preset " + file.getFileNameWithoutExtension());
    bool ok = PresetManager::applyShow(proc.getParameters(), proc.getRouting(), proc.getShow(), tree);
    if (!ok) ok = PresetManager::applyRouting(proc.getRouting(), tree);
    if (!ok) { ctx.say("Preset file is not a show / routing preset: " + file.getFileName(), true); return false; }
    ctx.showModelChanged();
    ctx.say("Preset loaded: " + file.getFileNameWithoutExtension());
    return true;
}

void PresetBrowser::savePreset(bool routingOnly) {
    auto folder = PresetManager::userPresetFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser>(routingOnly ? "Save routing preset" : "Save show preset",
                                                  folder.getChildFile(routingOnly ? "Routing.cmpreset" : "Show.cmpreset"), "*.cmpreset");
    juce::Component::SafePointer<PresetBrowser> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, routingOnly](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (!f.hasFileExtension("cmpreset;xml")) f = f.withFileExtension("cmpreset");
        auto& p = safe->proc;
        const auto tree = routingOnly ? PresetManager::captureRouting(p.getRouting()) : PresetManager::captureShow(p.getParameters(), p.getRouting(), p.getShow());
        const bool ok = PresetManager::saveToFile(tree, f);
        safe->ctx.say(ok ? "Saved " + f.getFileName() : "Cannot save " + f.getFullPathName(), !ok);
        safe->refresh();
    });
}

void PresetBrowser::choosePresetFile() {
    chooser = std::make_unique<juce::FileChooser>("Load preset", PresetManager::userPresetFolder(), "*.cmpreset;*.xml");
    juce::Component::SafePointer<PresetBrowser> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f != juce::File()) safe->loadPresetFile(f);
    });
}

void PresetBrowser::paint(juce::Graphics& g) {
    kit::paintSection(g, templArea, "Wiring templates");
    kit::paintSection(g, factArea, "Factory presets");
    kit::paintSection(g, userArea, "User show / routing presets");
}

void PresetBrowser::resized() {
    auto r = getLocalBounds().reduced(8);
    const int colW = (r.getWidth() - 16) / 3;
    templArea = r.removeFromLeft(colW);
    r.removeFromLeft(8);
    factArea = r.removeFromLeft(colW);
    r.removeFromLeft(8);
    userArea = r;
    {
        auto c = kit::sectionContent(templArea);
        applyTemplateButton.setBounds(c.removeFromBottom(28));
        c.removeFromBottom(6);
        templateDesc.setBounds(c.removeFromBottom(juce::jmin(64, c.getHeight() / 3)));
        templates.setBounds(c);
    }
    {
        auto c = kit::sectionContent(factArea);
        auto top = c.removeFromTop(26);
        kit::layoutRow(top, { { &target, 1.0f }, { &instance, 1.0f } });
        c.removeFromTop(6);
        applyFactoryButton.setBounds(c.removeFromBottom(28));
        c.removeFromBottom(6);
        factoryList.setBounds(c);
    }
    {
        auto c = kit::sectionContent(userArea);
        kit::layoutRow(c.removeFromTop(28), { { &saveShow, 1.0f }, { &saveRouting, 1.0f } });
        c.removeFromTop(6);
        kit::layoutRow(c.removeFromBottom(28), { { &loadUser, 1.2f }, { &loadFile, 1.0f }, { &refreshButton, 0.8f } });
        c.removeFromBottom(6);
        userList.setBounds(c);
    }
}
}
