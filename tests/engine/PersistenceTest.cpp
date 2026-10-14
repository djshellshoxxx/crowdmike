// Recorder track -> engine tap wiring and clip deck / bed / soundcheck persistence (ShowModel::playback).
#include "TestHarness.h"
#include "PluginProcessor.h"
#include "Engine/TapMap.h"
#include "UiTestUtil.h"
#include <thread>

using namespace crowdmike;

namespace {
juce::File freshDir(const char* name) {
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    d.deleteRecursively();
    d.createDirectory();
    return d;
}
void runBlocks(CrowdMikeAudioProcessor& p, int blocks, float amp = 0.3f) {
    juce::AudioBuffer<float> b(2, 256);
    juce::MidiBuffer midi;
    static int phase = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        for (int s = 0; s < 256; ++s, ++phase) { const float v = amp * std::sin(0.05f * static_cast<float>(phase)); b.setSample(0, s, v); b.setSample(1, s, v); }
        p.processBlock(b, midi);
    }
}
float filePeak(const juce::File& f, int* channels = nullptr) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) return -1.0f;
    if (channels != nullptr) *channels = static_cast<int>(r->numChannels);
    juce::AudioBuffer<float> b(static_cast<int>(r->numChannels), static_cast<int>(r->lengthInSamples));
    r->read(&b, 0, b.getNumSamples(), 0, true, true);
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch) peak = juce::jmax(peak, b.getMagnitude(ch, 0, b.getNumSamples()));
    return peak;
}
}

CM_TEST(persistence_recorder_tracks_reach_engine_and_record_files) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = freshDir("cm_persist_rec");
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(2);   // two mics -> stereo PA (master carries signal)
    auto& r = p.getShow().recorder;
    r.folder = dir.getFullPathName();
    r.pattern = "{source}_{take}";
    r.tracks[0].sourceId = TapMap::Master;
    r.tracks[0].numChannels = 2;
    r.tracks[0].armed = true;
    r.tracks[1].sourceId = TapMap::forRawInput(0);
    r.tracks[1].numChannels = 1;
    r.tracks[1].armed = true;
    r.tracks[2].sourceId = TapMap::forRawInput(1);
    r.tracks[2].muted = true;            // configured but muted -> not recorded
    r.tracks[2].armed = true;
    p.applyShowChanges();
    CM_EXPECT(p.getRecorder().getTrack(0).armed);
    CM_EXPECT(p.getRecorder().getTrack(0).numChannels == 2);
    CM_EXPECT(!p.getRecorder().getTrack(2).armed);
    CM_EXPECT(p.startRecording());
    runBlocks(p, 200);
    p.getRecorder().stopRecordingNow();
    CM_EXPECT(!p.isRecording());
    const auto files = dir.findChildFiles(juce::File::findFiles, true, "*.wav");
    CM_EXPECT(files.size() == 2);
    bool sawMaster = false, sawRaw = false;
    for (const auto& f : files) {
        int ch = 0;
        const float peak = filePeak(f, &ch);
        if (f.getFileName().startsWith("Master")) { sawMaster = true; CM_EXPECT(ch == 2); CM_EXPECT(peak > 0.01f); }
        if (f.getFileName().startsWith("Raw")) { sawRaw = true; CM_EXPECT(ch == 1); CM_EXPECT(peak > 0.1f); }
    }
    CM_EXPECT(sawMaster);
    CM_EXPECT(sawRaw);
    // after a re-prepare (sample-rate change) the engine still knows the taps
    p.prepareToPlay(44100.0, 128);
    CM_EXPECT(p.getRecorder().getTrack(1).armed);
    dir.deleteRecursively();
}

CM_TEST(persistence_clip_bed_soundcheck_survive_state_roundtrip) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = freshDir("cm_persist_clip");
    const auto wav = dir.getChildFile("cheer.wav");
    CM_EXPECT(uitest::writeSineWav(wav, 48000.0, 24000, 2));
    juce::MemoryBlock state;
    {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        CM_EXPECT(p.loadClipFile(3, wav));
        rec::PlaybackEngine::ClipSettings s;
        s.name = "Big Cheer";
        s.gainDb = -6.0f;
        s.fadeInMs = 120.0f;
        s.fadeOutMs = 340.0f;
        s.startFrac = 0.1;
        s.endFrac = 0.8;
        s.mode = rec::PlaybackEngine::ClipSettings::Loop;
        s.reverse = true;
        s.route = 2;
        s.tempoSync = true;
        p.setClipSettings(3, s);
        p.setClipHotkey(3, "Q");
        rec::PlaybackEngine::BedSettings bed;
        bed.enabled = true;
        bed.slot = 3;
        bed.gainDb = -20.0f;
        bed.minLoopSec = 5.0;
        bed.maxLoopSec = 9.0;
        bed.route = 9;
        p.setBedSettings(bed);
        CM_EXPECT(!p.loadSoundcheckSession(dir.getChildFile("missing_session.json")));
        p.setSoundcheckLoop(1.5, 4.0, true);
        p.getStateInformation(state);
    }
    CrowdMikeAudioProcessor q;
    q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));   // before prepare: loads deferred
    q.prepareToPlay(48000.0, 256);
    const auto s = q.getPlayback().getSettings(3);
    CM_EXPECT(s.name == "Big Cheer");
    CM_EXPECT_NEAR(s.gainDb, -6.0, 1e-3);
    CM_EXPECT_NEAR(s.fadeInMs, 120.0, 1e-3);
    CM_EXPECT_NEAR(s.fadeOutMs, 340.0, 1e-3);
    CM_EXPECT_NEAR(s.startFrac, 0.1, 1e-6);
    CM_EXPECT_NEAR(s.endFrac, 0.8, 1e-6);
    CM_EXPECT(s.mode == rec::PlaybackEngine::ClipSettings::Loop);
    CM_EXPECT(s.reverse);
    CM_EXPECT(s.route == 2);
    CM_EXPECT(s.tempoSync);
    CM_EXPECT(q.getClipPath(3) == wav.getFullPathName());
    CM_EXPECT(!q.isClipMissing(3));
    CM_EXPECT(q.getClipHotkey(3) == "Q");
    CM_EXPECT(q.findClipForHotkey("Q") == 3);
    CM_EXPECT(q.getPlayback().waitForLoads(5000));
    CM_EXPECT(q.getPlayback().getClipInfo(3).loaded);
    const auto bed = q.getPlayback().getBed();
    CM_EXPECT(bed.enabled);
    CM_EXPECT(bed.slot == 3);
    CM_EXPECT_NEAR(bed.gainDb, -20.0, 1e-3);
    CM_EXPECT_NEAR(bed.minLoopSec, 5.0, 1e-6);
    CM_EXPECT(bed.route == 9);
    CM_EXPECT(q.getSoundcheckSessionPath() == dir.getChildFile("missing_session.json").getFullPathName());
    CM_EXPECT(q.isSoundcheckSessionMissing());
    // re-applying the show (any edit elsewhere) must not reload the clip
    q.applyShowChanges();
    CM_EXPECT(!q.getPlayback().getClipInfo(3).loading);
    // unloading clears the slot from the show
    q.unloadClipFile(3);
    CM_EXPECT(q.getClipPath(3).isEmpty());
    dir.deleteRecursively();
}

CM_TEST(persistence_missing_clip_file_is_reported_not_fatal) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = freshDir("cm_persist_missing");
    const auto wav = dir.getChildFile("gone.wav");
    CM_EXPECT(uitest::writeSineWav(wav, 48000.0, 4800));
    juce::MemoryBlock state;
    {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.loadClipFile(0, wav);
        p.getStateInformation(state);
    }
    wav.deleteFile();
    CrowdMikeAudioProcessor q;
    q.prepareToPlay(48000.0, 256);
    q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CM_EXPECT(q.isClipMissing(0));
    CM_EXPECT(!q.getPlayback().getClipInfo(0).loaded);
    CM_EXPECT(q.getPlayback().trigger(0) || true);   // triggering an empty slot is harmless
    runBlocks(q, 4);
    // a relative / garbage path never asserts
    q.getShow().playback.getChildWithName("Clip").setProperty("file", "not/absolute.wav", nullptr);
    q.applyShowChanges();
    CM_EXPECT(q.isClipMissing(0));
    dir.deleteRecursively();
}
