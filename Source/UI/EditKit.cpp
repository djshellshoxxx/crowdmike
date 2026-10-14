#include "EditKit.h"
#include "Core/PresetManager.h"
#include "UI/Widgets.h"

namespace crowdmike::ui::kit {
void styleLabel(juce::Label& l, float h, bool bold, juce::Colour c, juce::Justification j) {
    l.setFont(font(h, bold));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(j);
    l.setMinimumHorizontalScale(0.6f);
    l.setBorderSize(juce::BorderSize<int>(0, 2, 0, 2));
}

float actual(CrowdMikeAudioProcessor& p, int idx) {
    if (auto* param = p.getBank().parameterAt(idx)) return param->convertFrom0to1(param->getValue());
    return 0.0f;
}

void setActual(CrowdMikeAudioProcessor& p, int idx, float value) {
    auto* param = p.getBank().parameterAt(idx);
    if (param == nullptr || !std::isfinite(value)) return;
    const float norm = juce::jlimit(0.0f, 1.0f, param->convertTo0to1(value));
    if (std::abs(norm - param->getValue()) < 1.0e-7f) return;
    param->beginChangeGesture();
    param->setValueNotifyingHost(norm);
    param->endChangeGesture();
}

int resetToDefaults(CrowdMikeAudioProcessor& p, const std::vector<int>& indices, const juce::String& name) {
    p.getUndoManager().beginNewTransaction(name);
    int changed = 0;
    for (int idx : indices) {
        auto* param = p.getBank().parameterAt(idx);
        if (param == nullptr) continue;
        const float def = param->getDefaultValue();
        if (std::abs(param->getValue() - def) < 1.0e-7f) continue;
        param->beginChangeGesture();
        param->setValueNotifyingHost(def);
        param->endChangeGesture();
        ++changed;
    }
    return changed;
}

juce::String fmtDb(float db, int decimals) {
    if (!std::isfinite(db) || db <= -79.95f) return "-inf";
    return (db > 0.05f ? "+" : "") + juce::String(db, decimals);
}
juce::String fmtHz(float hz) {
    if (hz >= 1000.0f) return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + "k";
    return juce::String(juce::roundToInt(hz));
}
juce::String fmtGainDb(float g) {
    if (std::abs(g) < 1.0e-4f) return "-inf";
    return fmtDb(juce::Decibels::gainToDecibels(std::abs(g), -80.0f));
}

// ---------------------------------------------------------------- alignment units
const char* alignUnitName(int unit) { return unit == 1 ? "smp" : (unit == 2 ? "m" : "ms"); }
juce::String alignToText(double ms, int unit, double sr) {
    if (sr <= 0.0) sr = 48000.0;
    if (unit == 1) return juce::String(juce::roundToInt(ms * sr / 1000.0)) + " smp";
    if (unit == 2) return juce::String(ms / 1000.0 * kSpeedOfSoundMps, 2) + " m";
    return juce::String(ms, 2) + " ms";
}
double alignFromText(const juce::String& text, int unit, double sr) {
    if (sr <= 0.0) sr = 48000.0;
    const auto t = text.trim().toLowerCase();
    const double v = t.retainCharacters("0123456789.-").getDoubleValue();
    // an explicit unit typed by the operator wins over the display unit
    if (t.endsWith("ms")) unit = 0;
    else if (t.endsWith("smp") || t.endsWith("samples")) unit = 1;
    else if (t.endsWith("m") || t.endsWith("metres") || t.endsWith("meters")) unit = 2;
    if (unit == 1) return v * 1000.0 / sr;
    if (unit == 2) return v / kSpeedOfSoundMps * 1000.0;
    return v;
}

// ---------------------------------------------------------------- Card
int Card::titleHeight() const { return title.isEmpty() ? 0 : px(24); }
juce::Rectangle<int> Card::getContentBounds() const { return getLocalBounds().reduced(px(6)).withTrimmedTop(titleHeight()); }
void Card::paint(juce::Graphics& g) {
    const auto& p = palette();
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(highlighted ? p.accent : p.panelEdge);
    g.drawRoundedRectangle(r, 5.0f, highlighted ? 2.0f : 1.0f);
    if (title.isNotEmpty()) {
        auto t = getLocalBounds().reduced(px(8), 0).removeFromTop(titleHeight() + px(4)).withTrimmedTop(px(3));
        g.setColour(p.accent);
        g.setFont(font(13.0f, true));
        const int tw = juce::jmin(t.getWidth(), juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), title.toUpperCase()) + px(12));
        g.drawText(title.toUpperCase(), t.removeFromLeft(tw), juce::Justification::centredLeft, true);
        if (subtitle.isNotEmpty()) {
            g.setColour(p.textMuted);
            g.setFont(font(12.0f));
            g.drawText(subtitle, t, juce::Justification::centredLeft, true);
        }
    }
}

// ---------------------------------------------------------------- Captioned
Captioned::Captioned(const juce::String& text, std::unique_ptr<juce::Component> control, int w) : ctl(std::move(control)), captionW(w) {
    caption.setText(text, juce::dontSendNotification);
    styleLabel(caption, 11.5f, false, palette().textMuted);
    caption.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(caption);
    addAndMakeVisible(*ctl);
    setTitle(text);
}
void Captioned::resized() {
    auto r = getLocalBounds();
    caption.setBounds(r.removeFromLeft(px(captionW)));
    ctl->setBounds(r);
}

// ---------------------------------------------------------------- flow
int flow(const std::vector<FlowItem>& items, juce::Rectangle<int> area, int lineH, int gap, bool apply) {
    int x = area.getX(), y = area.getY();
    const int right = area.getRight();
    bool lineEmpty = true;
    for (size_t k = 0; k < items.size(); ++k) {
        const auto& it = items[k];
        if (it.comp != nullptr && !it.comp->isVisible() && apply) continue;
        const int w = juce::jmin(area.getWidth(), px(it.width));
        if (!lineEmpty && (it.lineBreak || x + w > right)) { x = area.getX(); y += lineH + gap; lineEmpty = true; }
        int useW = w;
        if (it.fill) useW = juce::jmax(w, right - x);
        if (apply && it.comp != nullptr) it.comp->setBounds(x, y, useW, lineH);
        x += useW + gap;
        lineEmpty = false;
    }
    return (y - area.getY()) + lineH;
}

// ---------------------------------------------------------------- presets
juce::File userFolderFor(const PresetTarget& t) {
    return PresetManager::userPresetFolder().getChildFile(t.folderName.isNotEmpty() ? t.folderName : juce::String("misc"));
}

juce::ValueTree captureBlocks(CrowdMikeAudioProcessor& p, const PresetTarget& t) {
    if (t.blockPrefixes.isEmpty()) return PresetManager::capture(p.getParameters(), t.module, t.instance);
    juce::ValueTree out;
    for (const auto& prefix : t.blockPrefixes) {
        auto part = PresetManager::capture(p.getParameters(), t.module, t.instance, prefix);
        if (!out.isValid()) { out = part; continue; }
        for (int c = 0; c < part.getNumChildren(); ++c) out.appendChild(part.getChild(c).createCopy(), nullptr);
    }
    return out;
}

namespace {
std::unique_ptr<juce::FileChooser>& chooserHolder() { static std::unique_ptr<juce::FileChooser> c; return c; }

std::vector<const FactoryPreset*> factoryList(const juce::String& target, const juce::StringArray& prefixes) {
    std::vector<const FactoryPreset*> out;
    if (prefixes.isEmpty()) {
        for (auto* fp : PresetManager::factoryPresetsFor(target))
            if (juce::String(fp->blockPrefix).isEmpty() && juce::String(fp->target) == target) out.push_back(fp);
        return out;
    }
    for (const auto& prefix : prefixes)
        for (auto* fp : PresetManager::factoryPresetsFor(target, prefix)) out.push_back(fp);
    return out;
}
}

void showPresetMenu(CrowdMikeAudioProcessor& proc, juce::Component* anchor, const PresetTarget& t) {
    juce::PopupMenu menu;
    std::vector<const FactoryPreset*> factory;
    std::vector<juce::File> userFiles;
    menu.addSectionHeader(t.title.isNotEmpty() ? t.title : juce::String("Presets"));
    const auto addFactory = [&](juce::PopupMenu& m, const std::vector<const FactoryPreset*>& list) {
        for (auto* fp : list) { factory.push_back(fp); m.addItem(static_cast<int>(factory.size()), fp->name); }
    };
    addFactory(menu, factoryList(t.factoryTarget, t.blockPrefixes));
    for (const auto& [sectionTitle, prefixes] : t.extraSections) {
        juce::PopupMenu sub;
        addFactory(sub, factoryList(t.factoryTarget, prefixes));
        menu.addSubMenu(sectionTitle, sub, sub.getNumItems() > 0);
    }
    const auto folder = userFolderFor(t);
    if (folder.isDirectory()) {
        auto files = folder.findChildFiles(juce::File::findFiles, false, "*.cmpreset");
        files.sort();
        if (!files.isEmpty()) menu.addSectionHeader("User presets");
        for (const auto& f : files) {
            if (userFiles.size() >= 40) break;
            userFiles.push_back(f);
            menu.addItem(1000 + static_cast<int>(userFiles.size()), f.getFileNameWithoutExtension());
        }
    }
    menu.addSeparator();
    menu.addItem(2001, "Save as user preset...");
    menu.addItem(2002, "Load preset file...");

    juce::Component::SafePointer<juce::Component> safeAnchor(anchor);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor), [&proc, t, factory, userFiles, safeAnchor](int r) {
        if (r <= 0) return;
        if (r <= static_cast<int>(factory.size())) {
            const auto* fp = factory[static_cast<size_t>(r - 1)];
            proc.getUndoManager().beginNewTransaction("Preset: " + juce::String(fp->name));
            PresetManager::applyFactory(proc.getParameters(), *fp, t.module, t.instance);
            return;
        }
        if (r > 1000 && r <= 1000 + static_cast<int>(userFiles.size())) {
            const auto tree = PresetManager::loadFromFile(userFiles[static_cast<size_t>(r - 1001)]);
            proc.getUndoManager().beginNewTransaction("Load preset");
            PresetManager::apply(proc.getParameters(), tree, t.module, t.instance);
            return;
        }
        const auto folder = userFolderFor(t);
        folder.createDirectory();
        auto& holder = chooserHolder();
        if (r == 2001) {
            holder = std::make_unique<juce::FileChooser>("Save preset", folder.getChildFile("My preset.cmpreset"), "*.cmpreset");
            holder->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                [&proc, t](const juce::FileChooser& fc) {
                                    auto f = fc.getResult();
                                    if (f == juce::File()) return;
                                    if (!f.hasFileExtension("cmpreset")) f = f.withFileExtension("cmpreset");
                                    PresetManager::saveToFile(captureBlocks(proc, t), f);
                                });
        } else if (r == 2002) {
            holder = std::make_unique<juce::FileChooser>("Load preset", folder, "*.cmpreset");
            holder->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                [&proc, t](const juce::FileChooser& fc) {
                                    const auto f = fc.getResult();
                                    if (!f.existsAsFile()) return;
                                    const auto tree = PresetManager::loadFromFile(f);
                                    proc.getUndoManager().beginNewTransaction("Load preset");
                                    PresetManager::apply(proc.getParameters(), tree, t.module, t.instance);
                                });
        }
    });
}

void showAssignMenu(CrowdMikeAudioProcessor& proc, juce::Component* anchor, const std::vector<int>& indices) {
    juce::PopupMenu menu;
    menu.addSectionHeader("MIDI learn / macro: pick a parameter");
    for (size_t k = 0; k < indices.size(); ++k)
        if (auto* p = proc.getBank().parameterAt(indices[k])) menu.addItem(static_cast<int>(k) + 1, p->getName(48));
    juce::Component::SafePointer<juce::Component> safe(anchor);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor), [&proc, indices, safe](int r) {
        if (r > 0 && safe != nullptr) showParamContextMenu(proc, indices[static_cast<size_t>(r - 1)], safe.getComponent());
    });
}
}
