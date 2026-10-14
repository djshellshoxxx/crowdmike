// Live / Settings / Diagnostics pages: drive controls programmatically and check processor / model state.
// Also renders those pages at 1100x650 and 2560x1440 (CM_SNAPSHOT_DIR) for visual review.
#include "TestHarness.h"
#include "PluginEditor.h"
#include "Engine/TapMap.h"
#include "UI/FeedbackView.h"
#include "UI/LiveStrips.h"
#include "UI/SettingsSections.h"
#include "UI/ShowFiles.h"
#include "UI/Ticker.h"
#include <cstdlib>

using namespace crowdmike;
using namespace crowdmike::ui;

namespace {
template <typename T>
void collect(juce::Component& root, std::vector<T*>& out) {
    for (auto* c : root.getChildren()) {
        if (auto* t = dynamic_cast<T*>(c)) out.push_back(t);
        collect(*c, out);
    }
}
template <typename T>
std::vector<T*> findAll(juce::Component& root) { std::vector<T*> v; collect(root, v); return v; }

struct Rig {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> base;
    CrowdMikeAudioProcessorEditor* editor = nullptr;
    explicit Rig(HostServices* hs = nullptr) {
        p.setHostServices(hs);
        p.prepareToPlay(48000.0, 256);
        base.reset(p.createEditor());
        editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
        editor->setSize(1600, 900);
    }
    void audio(int blocks = 30) {
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < blocks; ++blk) {
            for (int s = 0; s < 256; ++s) { const float v = 0.4f * std::sin(0.11f * static_cast<float>(blk * 256 + s)); b.setSample(0, s, v); b.setSample(1, s, 0.5f * v); }
            p.processBlock(b, midi);
        }
    }
    Page& show(PageId id) { editor->showPage(id); Tickable::tickAllForTesting(); return *editor->getPage(id); }
};

struct FakeHost final : HostServices {
    std::vector<DeviceBinding> in { { 0, "Mic A", 0, true }, { 1, "Mic Old", -1, false } }, out { { 0, "Out 1", 0, true }, { 1, "", -1, false } };
    int setCalls = 0;
    juce::String lastSet;
    DeviceStatus getDeviceStatus() override { DeviceStatus s; s.typeName = "ALSA"; s.deviceName = "Test card"; s.sampleRate = 48000; s.bufferSize = 256; s.deviceOpen = true; s.xruns = 2; s.inputLatencySamples = 32; s.outputLatencySamples = 48; return s; }
    void showDeviceSettings() override {}
    juce::StringArray getInputChannelNames() override { return { "Mic A", "Mic B" }; }
    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    std::vector<DeviceBinding> getInputBindings() override { return in; }
    std::vector<DeviceBinding> getOutputBindings() override { return out; }
    void setInputBinding(int ch, const juce::String& n) override { ++setCalls; lastSet = n; in[static_cast<size_t>(ch)] = { ch, n, n.isEmpty() ? -1 : 1, n.isNotEmpty() }; }
    void setOutputBinding(int ch, const juce::String& n) override { out[static_cast<size_t>(ch)] = { ch, n, 0, true }; }
    void saveSessionNow() override {}
    bool restoredAfterCrash() override { return false; }
    juce::File getAutosaveFile() override { return {}; }
    juce::StringArray getMidiInputNames() override { return { "Pad controller" }; }
    void setMidiInputEnabled(const juce::String&, bool e) override { midiOn = e; }
    bool isMidiInputEnabled(const juce::String&) override { return midiOn; }
    bool midiOn = false;
};

void snapshot(Rig& r, PageId id, int w, int h) {
    const char* d = std::getenv("CM_SNAPSHOT_DIR");
    if (d == nullptr) return;
    r.editor->setSize(w, h);
    r.show(id);
    auto img = r.editor->createComponentSnapshot(r.editor->getLocalBounds(), true, 1.0f);
    auto f = juce::File(d).getChildFile(juce::String(pageName(id)) + "_" + juce::String(w) + "x" + juce::String(h) + ".png");
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (os.openedOk()) juce::PNGImageFormat().writeImageToStream(img, os);
}
}

CM_TEST(ui_live_strips_drive_model) {
    Rig r;
    r.audio();
    auto& page = r.show(PageId::Live);
    auto strips = findAll<LiveInputStrip>(page);
    CM_EXPECT(strips.size() == 2);
    if (strips.size() < 2) return;
    auto& s0 = *strips[0];
    // mute toggle -> parameter
    s0.muteToggle().setToggleState(true, juce::sendNotificationSync);
    CM_EXPECT(r.p.getBank().rawAt(ParamBank::index(Mod::Input, 0, params::InputP::Mute)) >= 0.5f);
    // rename
    s0.nameLabel().setText("Kick crowd", juce::sendNotificationSync);
    CM_EXPECT(r.p.getShow().input(0).name == "Kick crowd");
    // record arm adds / removes a raw-input recorder track
    s0.recArmButton().onClick();
    CM_EXPECT(isInputRecordArmed(r.p.getShow(), 0));
    bool found = false;
    for (const auto& t : r.p.getShow().recorder.tracks) found = found || (t.sourceId == TapMap::forRawInput(0) && t.armed);
    CM_EXPECT(found);
    s0.recArmButton().onClick();
    CM_EXPECT(!isInputRecordArmed(r.p.getShow(), 0));
    // destinations + delay + risk readouts
    r.p.getRouting().clearAll();
    r.p.getRouting().setAssign(0, 0, 1.0f);
    r.p.getRouting().setAssign(0, 2, 1.0f);
    r.p.getHub().setParamActual(ParamBank::index(Mod::Input, 0, params::InputP::AlignMs), 12.5f);
    r.p.getMeters().inRisk[0].store(0.9f);
    Tickable::tickAllForTesting();
    CM_EXPECT(s0.destinationText().contains("G1 G3"));
    CM_EXPECT(s0.delayText().contains("12.5"));
    CM_EXPECT(s0.riskBadge().text.startsWith("FB HIGH"));
    CM_EXPECT(s0.riskBadge().level == StatusBadge::Level::Danger);
    // ribbon: Master selects master, Shape navigates to Groups
    auto ribbons = findAll<FlowRibbon>(page);
    CM_EXPECT(ribbons.size() == 1);
    if (ribbons.empty()) return;
    ribbons[0]->stage(5).onClick();
    CM_EXPECT(r.editor->getContext().getSelection().kind == Selection::Kind::Master);
    ribbons[0]->stage(3).onClick();
    CM_EXPECT(r.editor->currentPage() == PageId::Groups);
    ribbons[0]->stage(6).onClick();
    CM_EXPECT(r.editor->currentPage() == PageId::Matrix);
}

CM_TEST(ui_settings_bindings_ui_prefs_and_show_files) {
    FakeHost host;
    Rig r(&host);
    auto& page = r.show(PageId::Settings);
    auto audio = findAll<AudioSettingsSection>(page);
    CM_EXPECT(audio.size() == 1);
    if (audio.empty()) return;
    auto& a = *audio[0];
    CM_EXPECT(a.getNumRows() == 4);
    if (a.getNumRows() < 2) return;
    CM_EXPECT(a.row(1).state.getText().contains("UNRESOLVED - disconnected (safe)"));
    CM_EXPECT(a.row(0).state.getText().contains("linked"));
    a.row(1).choice.setSelectedId(3, juce::sendNotificationSync);       // "Mic B"
    CM_EXPECT(host.setCalls == 1 && host.lastSet == "Mic B");
    CM_EXPECT(!a.row(1).state.getText().contains("UNRESOLVED"));

    auto ui = findAll<UiSettingsSection>(page);
    CM_EXPECT(ui.size() == 1);
    if (ui.empty()) return;
    ui[0]->compactToggle().setToggleState(true, juce::sendNotificationSync);
    CM_EXPECT(r.p.getShow().ui.compact);
    ui[0]->tooltipsToggle().setToggleState(false, juce::sendNotificationSync);
    CM_EXPECT(!r.p.getShow().ui.tooltips);
    ui[0]->scaleBox().setSelectedId(5, juce::sendNotificationSync);       // 125 %
    CM_EXPECT_NEAR(r.p.getShow().ui.scale, 1.25, 1e-6);
    ui[0]->scaleBox().setSelectedId(3, juce::sendNotificationSync);
    ui[0]->rampSlider().setValue(250.0, juce::sendNotificationSync);
    CM_EXPECT_NEAR(r.p.getShow().ui.recallRampMs, 250.0, 0.5);

    // MIDI: binding list with remove, learn indicator + cancel
    auto midi = findAll<MidiSettingsSection>(page);
    CM_EXPECT(midi.size() == 1);
    if (midi.empty()) return;
    ControlBinding b; b.number = 7; b.targetIndex = ParamBank::index(Mod::Master, 0, params::MasterP::Level);
    r.p.getShow().bindings.push_back(b);
    midi[0]->refresh();
    CM_EXPECT(midi[0]->getNumBindingRows() == 1);
    CM_EXPECT(MidiSettingsSection::describeBinding(r.p, b).contains("CC 7"));
    midi[0]->removeButton(0).onClick();
    CM_EXPECT(r.p.getShow().bindings.empty());
    r.p.getHub().startLearn({ BindTarget::Panic, 0, BindMode::Toggle });
    midi[0]->tick();
    CM_EXPECT(midi[0]->learnBadge().text.contains("LEARN ACTIVE"));
    midi[0]->cancelLearnButton().onClick();
    CM_EXPECT(!r.p.getHub().isLearning());

    // OSC widgets -> ShowModel
    auto osc = findAll<OscSettingsSection>(page);
    CM_EXPECT(osc.size() == 1);
    if (osc.empty()) return;
    osc[0]->portEditor().setText("9123", false);
    osc[0]->localhostToggle().setToggleState(false, juce::sendNotificationSync);
    CM_EXPECT(r.p.getShow().osc.port == 9123);
    CM_EXPECT(!r.p.getShow().osc.localhostOnly);

    // Show files round trip + reset
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_ui_test.crowdshow");
    r.p.getShow().input(0).name = "Saved name";
    juce::String err;
    CM_EXPECT(saveShowFile(r.p, file, err));
    resetShowToDefaults(r.p);
    CM_EXPECT(r.p.getShow().input(0).name.isEmpty());
    CM_EXPECT(loadShowFile(r.p, file, err));
    CM_EXPECT(r.p.getShow().input(0).name == "Saved name");
    const auto junk = file.getSiblingFile("cm_ui_junk.crowdshow");
    junk.replaceWithText("not a show");
    CM_EXPECT(!loadShowFile(r.p, junk, err));
    file.deleteFile();
    junk.deleteFile();
}

CM_TEST(ui_settings_hosted_note) {
    Rig r;
    auto& page = r.show(PageId::Settings);
    auto audio = findAll<AudioSettingsSection>(page);
    CM_EXPECT(audio.size() == 1 && audio[0]->getNumRows() == 0);
    if (!audio.empty()) CM_EXPECT(audio[0]->noteLabel().getText().contains("Host-controlled I/O"));
}

CM_TEST(ui_diagnostics_feedback_view_apply_notch) {
    Rig r;
    CM_EXPECT(!r.p.getAnalysisThread().isActive());
    auto& page = r.show(PageId::Diagnostics);
    CM_EXPECT(r.p.getAnalysisThread().isActive());              // worker runs only while the page is shown
    auto views = findAll<FeedbackView>(page);
    CM_EXPECT(views.size() == 1);
    if (views.empty()) return;
    auto& v = *views[0];
    v.tapBox().setSelectedId(3, juce::sendNotificationSync);    // input 2
    CM_EXPECT(r.p.getEngine().getFeedbackTap() == 2);
    dsp::FeedbackCandidate c; c.frequencyHz = 2500.0f; c.severity = 0.8f; c.persistenceSec = 1.5f; c.suggestedQ = 20.0f; c.suggestedDepthDb = -9.0f;
    v.setCandidatesForTesting(&c, 1);
    CM_EXPECT(v.rowTarget(0).getSelectedId() == 2);            // defaults to the tapped input
    using namespace params;
    CM_EXPECT(r.p.getBank().rawAt(ParamBank::index(Mod::Input, 1, InputP::NotchOn)) < 0.5f);   // never automatic
    v.rowApply(0).onClick();
    CM_EXPECT(r.p.getBank().rawAt(ParamBank::index(Mod::Input, 1, InputP::NotchOn)) >= 0.5f);
    CM_EXPECT_NEAR(r.p.getBank().rawAt(ParamBank::index(Mod::Input, 1, InputP::NotchHz)), 2500.0, 1.0);
    CM_EXPECT_NEAR(r.p.getBank().rawAt(ParamBank::index(Mod::Input, 1, InputP::NotchDepth)), -9.0, 0.1);
    CM_EXPECT(!v.applyRow(3));                                  // no such candidate
    r.show(PageId::Live);
    CM_EXPECT(!r.p.getAnalysisThread().isActive());
}

CM_TEST(ui_live_settings_diag_render_sizes) {
    FakeHost host;
    Rig r(&host);
    r.audio(60);
    for (auto size : { std::pair<int, int> { 1100, 650 }, { 1600, 900 }, { 2560, 1440 } })
        for (auto id : { PageId::Live, PageId::Settings, PageId::Diagnostics })
            snapshot(r, id, size.first, size.second);
    CM_EXPECT(true);
}
