// GUI audit (docs/feature-audit/part-C): walks every page and the persistent shell and checks, for every VISIBLE control,
// that it is wired (parameter attachment or handler), has an accessible name, and that the layout is sane (no
// degenerate / clipped / overlapping interactive controls, no truncated button captions) at the supported window
// sizes and in compact / high-contrast / scaled modes. Set CM_SNAPSHOT_DIR to also write PNGs of every combination.
#include "TestHarness.h"
#include "PluginEditor.h"
#include "UI/Ticker.h"
#include "UiTestUtil.h"
#include "UI/SettingsSections.h"
#include <cstdlib>
#include <set>

using namespace crowdmike;
using namespace crowdmike::ui;

namespace {
juce::String describe(juce::Component& c) {
    juce::String s = juce::String(typeid(c).name()).fromFirstOccurrenceOf("crowdmike", false, false).substring(0, 36);
    if (auto* b = dynamic_cast<juce::Button*>(&c)) s << " '" << b->getButtonText() << "'";
    if (c.getTitle().isNotEmpty()) s << " title='" << c.getTitle() << "'";
    if (c.getComponentID().isNotEmpty()) s << " id=" << c.getComponentID();
    if (dynamic_cast<juce::Button*>(&c) == nullptr) s << " name=" << c.getName();
    return s;
}

// Visible descendants (invisible subtrees are skipped; internal parts of slider / combo / button are not entered).
void gather(juce::Component& root, std::vector<juce::Component*>& out) {
    for (auto* c : root.getChildren()) {
        if (!c->isVisible()) continue;
        out.push_back(c);
        if (dynamic_cast<juce::Slider*>(c) || dynamic_cast<juce::ComboBox*>(c) || dynamic_cast<juce::Button*>(c)
            || dynamic_cast<juce::Label*>(c) || dynamic_cast<juce::TextEditor*>(c) || dynamic_cast<juce::ScrollBar*>(c)) continue;
        gather(*c, out);
    }
}

bool isInteractive(juce::Component* c) {
    return dynamic_cast<juce::Button*>(c) || dynamic_cast<juce::Slider*>(c) || dynamic_cast<juce::ComboBox*>(c)
           || dynamic_cast<juce::TextEditor*>(c);
}

struct Findings {
    std::set<std::string> wiring, access, layout;
};

void auditWiring(const juce::String& where, juce::Component& root, Findings& f) {
    std::vector<juce::Component*> all;
    gather(root, all);
    for (auto* c : all) {
        if (dynamic_cast<ParamSlider*>(c) || dynamic_cast<ParamToggle*>(c) || dynamic_cast<ParamChoice*>(c) || dynamic_cast<MomentaryButton*>(c)) continue;
        if (dynamic_cast<juce::TabBarButton*>(c) != nullptr) continue;   // driven by their TabbedButtonBar
        if (auto* b = dynamic_cast<juce::Button*>(c)) {
            if (!b->onClick && !b->onStateChange) f.wiring.insert((where + ": button without handler " + describe(*c)).toStdString());
        } else if (auto* s = dynamic_cast<juce::Slider*>(c)) {
            if (!s->onValueChange && !s->onDragEnd) f.wiring.insert((where + ": slider without handler " + describe(*c)).toStdString());
        } else if (auto* cb = dynamic_cast<juce::ComboBox*>(c)) {
            if (!cb->onChange) f.wiring.insert((where + ": combo without handler " + describe(*c)).toStdString());
        } else if (auto* te = dynamic_cast<juce::TextEditor*>(c)) {
            if (!te->onTextChange && !te->onReturnKey && !te->onFocusLost && te->isReadOnly() == false)
                f.wiring.insert((where + ": text editor without handler " + describe(*c)).toStdString());
        } else if (auto* l = dynamic_cast<juce::Label*>(c)) {
            if (l->isEditableOnSingleClick() || l->isEditableOnDoubleClick())
                if (!l->onTextChange) f.wiring.insert((where + ": editable label without handler " + describe(*c)).toStdString());
        }
    }
}

void auditAccess(const juce::String& where, juce::Component& root, Findings& f) {
    std::vector<juce::Component*> all;
    gather(root, all);
    for (auto* c : all) {
        if (!isInteractive(c)) continue;
        juce::String name = c->getTitle();
        if (name.isEmpty()) if (auto* b = dynamic_cast<juce::Button*>(c)) name = b->getButtonText();
        if (name.isEmpty()) if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(c)) name = tip->getTooltip();
        if (name.isEmpty()) name = c->getName();
        if (name.isEmpty()) f.access.insert((where + ": control without accessible name " + describe(*c)).toStdString());
    }
}

void auditLayout(const juce::String& where, juce::Component& root, Findings& f) {
    std::vector<juce::Component*> all;
    gather(root, all);
    for (auto* c : all) {
        if (!isInteractive(c)) continue;
        const auto b = c->getBounds();
        if (b.getWidth() < 10 || b.getHeight() < 10) f.layout.insert((where + ": degenerate size " + b.toString() + " " + describe(*c)).toStdString());
        if (auto* p = c->getParentComponent(); p != nullptr && !p->getLocalBounds().expanded(1).contains(b)
            && dynamic_cast<juce::Viewport*>(p) == nullptr && dynamic_cast<juce::TabbedComponent*>(p) == nullptr)
            f.layout.insert((where + ": outside parent " + p->getLocalBounds().toString() + " " + b.toString() + " " + describe(*c)).toStdString());
        if (auto* tb = dynamic_cast<juce::TextButton*>(c)) {
            const auto text = tb->getButtonText();
            if (text.isNotEmpty()) {
                juce::GlyphArrangement ga;
                const auto font = tb->getLookAndFeel().getTextButtonFont(*tb, tb->getHeight());
                ga.addLineOfText(font, text, 0.0f, 0.0f);
                const float w = ga.getBoundingBox(0, -1, true).getWidth();
                // JUCE fits button text on up to two lines when the button is tall enough for them (nav rail "10 Diagnostics")
                const bool twoLines = static_cast<float>(tb->getHeight()) >= font.getHeight() * 2.0f + 4.0f && text.containsChar(' ');
                const float avail = (static_cast<float>(tb->getWidth()) - 4.0f) * (twoLines ? 1.8f : 1.0f);
                if (w > avail && tb->getWidth() > 0)
                    f.layout.insert((where + ": caption clipped (" + juce::String(w, 0) + "px in " + juce::String(tb->getWidth()) + ") " + describe(*c)).toStdString());
            }
        }
    }
    // pairwise overlap of interactive siblings
    std::vector<juce::Component*> inter;
    for (auto* c : all) {
        if (isInteractive(c) || dynamic_cast<StatusBadge*>(c) != nullptr) inter.push_back(c);
        else if (auto* l = dynamic_cast<juce::Label*>(c); l != nullptr && l->getText().isNotEmpty()) inter.push_back(c);
    }
    for (size_t i = 0; i < inter.size(); ++i)
        for (size_t j = i + 1; j < inter.size(); ++j) {
            if (inter[i]->getParentComponent() != inter[j]->getParentComponent()) continue;
            const auto x = inter[i]->getBounds().getIntersection(inter[j]->getBounds());
            if (x.getWidth() > 2 && x.getHeight() > 2)
                f.layout.insert((where + ": overlap " + describe(*inter[i]) + " " + inter[i]->getBounds().toString() + "  x  " + describe(*inter[j]) + " " + inter[j]->getBounds().toString()).toStdString());
        }
}

void reportSet(const char* what, const std::set<std::string>& s) {
    for (const auto& line : s) { std::cerr << "  " << what << ": " << line << '\n'; }
}

// Button::setState is protected; this is the standard pointer-to-member access trick (never instantiated).
struct ButtonStateAccess : juce::Button {
    static void set(juce::Button& b, juce::Button::ButtonState s) { (b.*&ButtonStateAccess::setState)(s); }
};

struct Combo { int w, h; bool compact, contrast; float scale; };
}

CM_TEST(ui_audit_every_visible_control_is_wired_and_named) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(3);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(1600, 900);
    Findings f;
    const Selection sels[] = { { Selection::Kind::None, 0 }, { Selection::Kind::Input, 0 }, { Selection::Kind::Group, 0 }, { Selection::Kind::Output, 0 },
                               { Selection::Kind::Master, 0 }, { Selection::Kind::Fx, 5 }, { Selection::Kind::Fx, 6 }, { Selection::Kind::Fx, 0 } };
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        const auto id = static_cast<PageId>(i);
        editor->showPage(id);
        for (const auto& s : sels) {
            editor->getContext().select(s);
            Tickable::tickAllForTesting();
            auditWiring(juce::String(pageName(id)) + "/" + juce::String(static_cast<int>(s.kind)), *editor, f);
            auditAccess(juce::String(pageName(id)) + "/" + juce::String(static_cast<int>(s.kind)), *editor, f);
        }
    }
    reportSet("WIRING", f.wiring);
    reportSet("ACCESS", f.access);
    CM_EXPECT(f.wiring.empty());
    CM_EXPECT(f.access.empty());
}

CM_TEST(ui_audit_layout_at_supported_sizes_and_modes) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(3);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    const char* snapRoot = std::getenv("CM_SNAPSHOT_DIR");
    const Combo combos[] = { { 1100, 650, false, false, 1.0f }, { 1280, 720, false, false, 1.0f }, { 1600, 900, false, false, 1.0f },
                             { 2560, 1440, false, false, 1.0f }, { 1600, 900, true, false, 1.0f }, { 1100, 650, true, true, 1.0f },
                             { 1600, 900, false, true, 1.0f }, { 1280, 720, false, false, 1.25f }, { 1100, 650, false, false, 1.5f } };
    Findings f;
    for (const auto& c : combos) {
        auto& ui = p.getShow().ui;
        ui.compact = c.compact; ui.highContrast = c.contrast; ui.scale = c.scale;
        editor->getContext().showModelChanged();
        // a scaled editor keeps its logical size; the physical window is c.w x c.h, so the logical size is smaller
        editor->setSize(juce::jmax(1100, juce::roundToInt(c.w / c.scale)), juce::jmax(650, juce::roundToInt(c.h / c.scale)));
        const juce::String tag = juce::String(c.w) + "x" + juce::String(c.h) + (c.compact ? "-compact" : "") + (c.contrast ? "-hc" : "") + (c.scale != 1.0f ? "-s" + juce::String(c.scale, 2) : "");
        for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
            const auto id = static_cast<PageId>(i);
            editor->showPage(id);
            Tickable::tickAllForTesting();
            auditLayout(tag + "/" + pageName(id), *editor, f);
            if (snapRoot != nullptr) {
                juce::File dir(snapRoot);
                dir.createDirectory();
                auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
                auto file = dir.getChildFile(tag + "_" + pageName(id) + ".png");
                file.deleteFile();
                juce::FileOutputStream out(file);
                if (out.openedOk()) juce::PNGImageFormat().writeImageToStream(img, out);
            }
        }
    }
    auto& ui = p.getShow().ui; ui.compact = false; ui.highContrast = false; ui.scale = 1.0f;
    reportSet("LAYOUT", f.layout);
    CM_EXPECT(f.layout.empty());
}

CM_TEST(ui_audit_strip_and_report_controls_have_effects) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(1600, 900);
    // Show Report: the Tracking toggle edits ShowModel::report.enabled
    editor->showPage(PageId::Record);
    auto* tracking = dynamic_cast<juce::Button*>(uitest::findById(*editor, "report.tracking"));
    CM_EXPECT(tracking != nullptr);
    if (tracking != nullptr) {
        CM_EXPECT(p.getShow().report.enabled);
        tracking->setToggleState(false, juce::sendNotificationSync);
        CM_EXPECT(!p.getShow().report.enabled);
        tracking->setToggleState(true, juce::sendNotificationSync);
        CM_EXPECT(p.getShow().report.enabled);
    }
    // Performance strip TAIL KILL drives BOTH the echo and the stadium reverb tail-kill parameters; REC follows the recorder
    juce::Button* tail = nullptr;
    std::vector<juce::Component*> all;
    gather(*editor, all);
    for (auto* c : all)
        if (auto* b = dynamic_cast<MomentaryButton*>(c); b != nullptr && b->getButtonText() == "TAIL KILL" && b->getParentComponent() != nullptr
            && dynamic_cast<PerformanceStrip*>(b->getParentComponent()) != nullptr) tail = b;
    CM_EXPECT(tail != nullptr);
    if (tail != nullptr) {
        auto* echo = p.getParameters().getParameter("fx.echo.tailKill");
        auto* reverb = p.getParameters().getParameter("fx.reverb.tailKill");
        CM_EXPECT(echo != nullptr && reverb != nullptr);
        ButtonStateAccess::set(*tail, juce::Button::buttonDown);
        CM_EXPECT(echo->getValue() > 0.5f && reverb->getValue() > 0.5f);
        ButtonStateAccess::set(*tail, juce::Button::buttonNormal);
        CM_EXPECT(echo->getValue() < 0.5f && reverb->getValue() < 0.5f);
    }
}

// Every host parameter must be reachable from some control on some page / inspector selection (no parameter that can only be
// changed by host automation, MIDI or OSC). Checked per (module, parameter enum) over instance 0 of each module.
CM_TEST(ui_audit_every_parameter_has_a_control) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(3);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(1600, 900);
    std::set<int> shown;
    auto collectFrom = [&](juce::Component& root) {
        std::vector<juce::Component*> all;
        gather(root, all);
        for (auto* c : all) {
            if (auto* x = dynamic_cast<ParamSlider*>(c)) shown.insert(x->getParamIndex());
            else if (auto* t = dynamic_cast<ParamToggle*>(c)) shown.insert(t->getParamIndex());
            else if (auto* ch = dynamic_cast<ParamChoice*>(c)) shown.insert(ch->getParamIndex());
            else if (auto* m = dynamic_cast<MomentaryButton*>(c)) (void) m;
        }
    };
    const Selection sels[] = { { Selection::Kind::None, 0 }, { Selection::Kind::Input, 0 }, { Selection::Kind::Group, 0 }, { Selection::Kind::Output, 0 },
                               { Selection::Kind::Master, 0 }, { Selection::Kind::Fx, 0 }, { Selection::Kind::Fx, 1 }, { Selection::Kind::Fx, 2 },
                               { Selection::Kind::Fx, 3 }, { Selection::Kind::Fx, 4 }, { Selection::Kind::Fx, 5 }, { Selection::Kind::Fx, 6 } };
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        editor->showPage(static_cast<PageId>(i));
        for (const auto& s : sels) { editor->getContext().select(s); Tickable::tickAllForTesting(); collectFrom(*editor); }
    }
    // MomentaryButton exposes no index accessor: the trigger parameters they drive are covered by name below.
    // Action parameters that are driven by dedicated buttons (not parameter-bound widgets): momentary buttons, record-all (REC button /
    // Ctrl+R), scene next/previous (status bar < >), tempo resync (Settings button), and the soundcheck switch (a ParamToggle inside the
    // Playback page's "Virtual soundcheck" tab, which is not visible while another tab is selected).
    const std::set<std::string> momentary { "fx.echo.trigger", "fx.echo.tailKill", "fx.reverse.trigger", "fx.reverb.tailKill", "tempo.tap",
                                            "recordAll", "sceneNext", "scenePrev", "tempo.resync", "soundcheck" };
    std::set<std::string> missing;
    for (int m = 0; m < static_cast<int>(Mod::Count); ++m) {
        const auto mod = static_cast<Mod>(m);
        const auto& info = ParamBank::info(mod);
        for (int e = 0; e < info.defCount; ++e) {
            bool found = false;
            for (int inst = 0; inst < info.instances && !found; ++inst) found = shown.count(ParamBank::index(mod, inst, e)) > 0;
            const auto id = ParamBank::idForIndex(ParamBank::index(mod, 0, e));
            if (!found && momentary.count(id.toStdString()) == 0) missing.insert(id.toStdString());
        }
    }
    for (const auto& id : missing) std::cerr << "  NO CONTROL: " << id << '\n';
    CM_EXPECT(missing.empty());
}

// Regression: the Tempo / Beat Sync section was laid out but never parented to the Settings page (invisible); every Settings
// section must be a visible descendant of the page, and the Global section must host the safety parameters.
CM_TEST(ui_audit_settings_sections_are_all_visible) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(1600, 900);
    editor->showPage(PageId::Settings);
    auto* tempo = uitest::findFirst<TempoSettingsSection>(*editor);
    auto* global = uitest::findFirst<GlobalSettingsSection>(*editor);
    CM_EXPECT(tempo != nullptr && tempo->isVisible() && tempo->getParentComponent() != nullptr && tempo->getHeight() > 100);
    CM_EXPECT(global != nullptr && global->isVisible() && global->getNumCells() == 9);
    // the safety controls really drive the parameters (rehearsal switch)
    if (global != nullptr) {
        auto* rehearsal = p.getParameters().getParameter("rehearsal");
        CM_EXPECT(rehearsal != nullptr);
        auto* toggle = uitest::findFirst<ParamToggle>(*global);
        CM_EXPECT(toggle != nullptr);
        if (toggle != nullptr && rehearsal != nullptr) {
            toggle->setToggleState(true, juce::sendNotificationSync);
            CM_EXPECT(rehearsal->getValue() > 0.5f);
        }
    }
}
