// Record / Playback / Scenes pages driven through their controls (component IDs), plus snapshots at three sizes.
#include "TestHarness.h"
#include "PluginEditor.h"
#include "Engine/TapMap.h"
#include "UI/PlaybackWidgets.h"
#include "UI/Ticker.h"
#include "UiTestUtil.h"
#include <cstdlib>

using namespace crowdmike;
using namespace crowdmike::ui;
using uitest::click;
using uitest::findById;

namespace {
struct Rig {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> base;
    CrowdMikeAudioProcessorEditor* editor = nullptr;
    Rig() {
        p.prepareToPlay(48000.0, 256);
        p.applyWiringTemplate(2);
        base.reset(p.createEditor());
        editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
        if (editor != nullptr) editor->setSize(1600, 900);
    }
    void process(int blocks) {
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < blocks; ++blk) {
            for (int s = 0; s < 256; ++s) { const float v = 0.3f * std::sin(0.05f * static_cast<float>(blk * 256 + s)); b.setSample(0, s, v); b.setSample(1, s, -v); }
            p.processBlock(b, midi);
        }
    }
};
juce::File snapshotDir() {
    if (const char* d = std::getenv("CM_SNAPSHOT_DIR")) { juce::File f(d); f.createDirectory(); return f; }
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_snapshots");
}
void snap(CrowdMikeAudioProcessorEditor& e, const juce::String& name) {
    Tickable::tickAllForTesting();
    auto img = e.createComponentSnapshot(e.getLocalBounds(), true, 1.0f);
    auto f = snapshotDir().getChildFile(name + ".png");
    f.deleteFile();
    juce::FileOutputStream out(f);
    CM_EXPECT(out.openedOk() && juce::PNGImageFormat().writeImageToStream(img, out));
}
}

CM_TEST(ui_record_page_tracks_auto_assign_and_markers) {
    Rig rig;
    CM_EXPECT(rig.editor != nullptr);
    if (rig.editor == nullptr) return;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_ui_rec");
    dir.deleteRecursively();
    rig.p.getShow().recorder.folder = dir.getFullPathName();
    rig.editor->showPage(PageId::Record);
    click(findById(*rig.editor, "rec.autoAssign"));
    const auto& tracks = rig.p.getShow().recorder.tracks;
    int raw = 0, master = 0;
    for (const auto& t : tracks) { raw += t.sourceId >= TapMap::Raw && t.sourceId < TapMap::Proc; master += t.sourceId == TapMap::Master; }
    CM_EXPECT(raw == rig.p.getActiveInputChannelCount());
    CM_EXPECT(master == 1);
    // source combo of an unused row assigns a group (stereo)
    auto* combo = dynamic_cast<juce::ComboBox*>(findById(*rig.editor, "rec.track10.source"));
    CM_EXPECT(combo != nullptr);
    if (combo != nullptr) combo->setSelectedId(TapMap::forGroup(1) + 2, juce::sendNotificationSync);
    CM_EXPECT(tracks[9].sourceId == TapMap::forGroup(1));
    CM_EXPECT(tracks[9].numChannels == 2);
    click(findById(*rig.editor, "rec.recordAll"));
    CM_EXPECT(tracks[9].armed);
    CM_EXPECT(rig.p.getRecorder().getTrack(9).armed);
    // pre-roll slider reaches the recorder through applyShowChanges
    auto* pre = dynamic_cast<juce::Slider*>(findById(*rig.editor, "rec.preRoll"));
    CM_EXPECT(pre != nullptr);
    if (pre != nullptr) pre->setValue(5.0, juce::sendNotificationSync);
    CM_EXPECT(rig.p.getShow().recorder.preRollSeconds == 5);
    // record via the big button, drop a marker, stop
    click(findById(*rig.editor, "rec.record"));
    CM_EXPECT(rig.p.isRecording());
    rig.process(20);
    click(findById(*rig.editor, "rec.marker0"));
    CM_EXPECT(rig.p.getRecorder().getMarkers().size() == 1);
    snap(*rig.editor, "RecPlay_Record_recording");
    rig.p.getRecorder().stopRecordingNow();
    CM_EXPECT(dir.findChildFiles(juce::File::findFiles, true, "*.wav").size() >= 3);
    for (auto size : { juce::Point<int>(1100, 650), juce::Point<int>(1600, 900), juce::Point<int>(2560, 1440) }) {
        rig.editor->setSize(size.x, size.y);
        snap(*rig.editor, "RecPlay_Record_" + juce::String(size.x));
    }
    dir.deleteRecursively();
}

CM_TEST(ui_playback_page_clip_settings_and_missing_file) {
    Rig rig;
    if (rig.editor == nullptr) { CM_EXPECT(false); return; }
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_ui_play");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto wav = dir.getChildFile("applause.wav");
    CM_EXPECT(uitest::writeSineWav(wav, 48000.0, 48000, 2));
    CM_EXPECT(rig.p.loadClipFile(0, wav));
    rig.p.loadClipFile(1, dir.getChildFile("does_not_exist.wav"));
    CM_EXPECT(rig.p.getPlayback().waitForLoads(5000));
    rig.editor->showPage(PageId::Playback);
    Tickable::tickAllForTesting();
    Tickable::tickAllForTesting();
    Tickable::tickAllForTesting();
    auto* missingTile = dynamic_cast<ClipTile*>(findById(*rig.editor, "play.tile2"));
    CM_EXPECT(missingTile != nullptr);
    if (missingTile != nullptr) CM_EXPECT(missingTile->stateText() == "MISSING");
    auto* okTile = dynamic_cast<ClipTile*>(findById(*rig.editor, "play.tile1"));
    if (okTile != nullptr) CM_EXPECT(okTile->stateText().contains("READY"));
    // deck edits for slot 1 (selected by default)
    auto* gain = dynamic_cast<juce::Slider*>(findById(*rig.editor, "play.gain"));
    CM_EXPECT(gain != nullptr);
    if (gain != nullptr) gain->setValue(-9.0, juce::sendNotificationSync);
    auto* mode = dynamic_cast<juce::ComboBox*>(findById(*rig.editor, "play.mode"));
    if (mode != nullptr) mode->setSelectedId(2, juce::sendNotificationSync);
    click(findById(*rig.editor, "play.reverse"));
    const auto s = rig.p.getPlayback().getSettings(0);
    CM_EXPECT_NEAR(s.gainDb, -9.0, 0.05);
    CM_EXPECT(s.mode == rec::PlaybackEngine::ClipSettings::Loop);
    CM_EXPECT(s.reverse);
    // persisted in the show model
    juce::MemoryBlock state;
    rig.p.getStateInformation(state);
    CrowdMikeAudioProcessor q;
    q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CM_EXPECT_NEAR(q.getPlayback().getSettings(0).gainDb, -9.0, 0.05);
    CM_EXPECT(q.isClipMissing(1));
    click(findById(*rig.editor, "play.trigger"));
    rig.process(10);
    for (auto size : { juce::Point<int>(1100, 650), juce::Point<int>(1600, 900), juce::Point<int>(2560, 1440) }) {
        rig.editor->setSize(size.x, size.y);
        snap(*rig.editor, "RecPlay_Playback_" + juce::String(size.x));
    }
    if (auto* tabs = uitest::findFirst<juce::TabbedComponent>(*rig.editor)) {
        tabs->setCurrentTabIndex(1);
        snap(*rig.editor, "RecPlay_Playback_bed");
        tabs->setCurrentTabIndex(2);
        snap(*rig.editor, "RecPlay_Playback_soundcheck");
    }
    dir.deleteRecursively();
}

CM_TEST(ui_scenes_page_capture_recall_and_macro_edit) {
    Rig rig;
    if (rig.editor == nullptr) { CM_EXPECT(false); return; }
    rig.editor->showPage(PageId::Scenes);
    const int trim = ParamBank::index(Mod::Input, 0, params::InputP::Trim);
    auto* param = rig.p.getBank().parameterAt(trim);
    param->setValueNotifyingHost(param->convertTo0to1(6.0f));
    auto* list = dynamic_cast<juce::ListBox*>(findById(*rig.editor, "scenes.list"));
    CM_EXPECT(list != nullptr);
    if (list == nullptr) return;
    list->selectRow(4);
    click(findById(*rig.editor, "scenes.capture"));
    CM_EXPECT(rig.p.getScenes().at(4).used);
    CM_EXPECT(rig.p.getScenes().at(4).name == "Scene 5");
    param->setValueNotifyingHost(param->convertTo0to1(-12.0f));
    click(findById(*rig.editor, "scenes.recall"));
    CM_EXPECT_NEAR(param->convertFrom0to1(param->getValue()), 6.0, 0.05);
    CM_EXPECT(rig.p.getScenes().current() == 4);
    // duplicate goes to the next empty slot
    click(findById(*rig.editor, "scenes.duplicate"));
    CM_EXPECT(rig.p.getScenes().at(5).used);
    // recall scope: with Inputs off a recall leaves inputs alone
    if (auto* inputsScope = dynamic_cast<juce::ToggleButton*>(findById(*rig.editor, "scenes.scope.Inputs"))) {
        inputsScope->setToggleState(false, juce::dontSendNotification);
        inputsScope->onClick();
        CM_EXPECT((rig.p.getShow().ui.recallScope & ScopeInputs) == 0);
        param->setValueNotifyingHost(param->convertTo0to1(-3.0f));
        list->selectRow(4);
        click(findById(*rig.editor, "scenes.recall"));
        CM_EXPECT_NEAR(param->convertFrom0to1(param->getValue()), -3.0, 0.05);
        rig.p.getShow().ui.recallScope = ScopeAll;
    } else CM_EXPECT(false);

    // macros: a destination added in the model shows up as a row; editing its max changes the effective value
    auto& map = rig.p.getShow().macros.macros[0];
    map.count = 1;
    map.dests[0] = MacroDest {};
    map.dests[0].paramIndex = trim;
    rig.editor->getContext().showModelChanged();
    auto* tabs = uitest::findFirst<juce::TabbedComponent>(*rig.editor);
    CM_EXPECT(tabs != nullptr);
    if (tabs == nullptr) return;
    tabs->setCurrentTabIndex(1);
    auto* maxSlider = dynamic_cast<juce::Slider*>(findById(*rig.editor, "macro.dest0.max"));
    CM_EXPECT(maxSlider != nullptr);
    if (maxSlider == nullptr) return;
    maxSlider->setValue(0.5, juce::sendNotificationSync);
    CM_EXPECT_NEAR(map.dests[0].max01, 0.5, 1e-4);
    auto* macroParam = rig.p.getBank().parameterAt(ParamBank::index(Mod::Macro, 0, 0));
    macroParam->setValueNotifyingHost(1.0f);
    rig.p.getBank().refresh();
    CM_EXPECT_NEAR(rig.p.getBank().effectiveAt(trim), param->convertFrom0to1(0.5f), 0.05);
    for (auto size : { juce::Point<int>(1100, 650), juce::Point<int>(1600, 900), juce::Point<int>(2560, 1440) }) {
        rig.editor->setSize(size.x, size.y);
        snap(*rig.editor, "RecPlay_Scenes_macros_" + juce::String(size.x));
    }
    tabs->setCurrentTabIndex(2);
    snap(*rig.editor, "RecPlay_Scenes_presets");
    tabs->setCurrentTabIndex(0);
    rig.p.getShow().setLocked(trim, true);
    rig.editor->getContext().showModelChanged();
    snap(*rig.editor, "RecPlay_Scenes_recall");
    tabs->setCurrentTabIndex(3);
    snap(*rig.editor, "RecPlay_Scenes_history");
}

CM_TEST(ui_clip_hotkey_triggers_clip) {
    Rig rig;
    if (rig.editor == nullptr) { CM_EXPECT(false); return; }
    auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_ui_hotkey.wav");
    CM_EXPECT(uitest::writeSineWav(wav, 48000.0, 48000));
    rig.p.loadClipFile(5, wav);
    CM_EXPECT(rig.p.getPlayback().waitForLoads(5000));
    const juce::KeyPress key('q');
    rig.p.setClipHotkey(5, key.getTextDescription());
    CM_EXPECT(rig.editor->keyPressed(key, rig.editor));
    rig.process(4);
    CM_EXPECT(rig.p.getPlayback().getClipInfo(5).playing);
    CM_EXPECT(!rig.editor->keyPressed(juce::KeyPress('w'), rig.editor));
    wav.deleteFile();
}
