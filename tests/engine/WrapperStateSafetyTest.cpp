#include "TestHarness.h"
#include "PluginProcessor.h"
#include "UiTestUtil.h"

namespace {
struct WrapperSoundcheckFixture {
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("cm_wrapper_state_" + juce::Uuid().toString());
    juce::File session = folder.getChildFile("session.json");
    WrapperSoundcheckFixture() {
        folder.createDirectory();
        const auto wav = folder.getChildFile("input.wav");
        CM_EXPECT(uitest::writeSineWav(wav, 48000.0, 4800));
        crowdmike::rec::SessionInfo info;
        info.sampleRate = 48000.0;
        info.totalFrames = 4800;
        info.tracks.push_back({ 0, 0, "Input", 1 });
        crowdmike::rec::SessionInfo::FileEntry entry;
        entry.fileName = wav.getFileName();
        entry.file = wav;
        entry.track = 0;
        entry.numFrames = 4800;
        info.files.push_back(entry);
        juce::String error;
        CM_EXPECT(crowdmike::rec::SessionMetadata::write(session, info, error));
    }
    ~WrapperSoundcheckFixture() { folder.deleteRecursively(); }
};
void processSoundcheck(crowdmike::rec::SoundcheckPlayer& soundcheck) {
    float audio[64] {};
    float* channels[] { audio };
    soundcheck.process(channels, 1, 64);
}
void loadAndArm(CrowdMikeAudioProcessor& processor, const juce::File& session) {
    auto& soundcheck = processor.getSoundcheck();
    CM_EXPECT(processor.loadSoundcheckSession(session));
    CM_EXPECT(soundcheck.waitForLoad(10000));
    CM_EXPECT(soundcheck.getLoadState().loaded);
    soundcheck.setLocked(false);
    CM_EXPECT(soundcheck.arm());
    soundcheck.play();
    processSoundcheck(soundcheck);
    CM_EXPECT(soundcheck.isReplacingInputs());
}
void expectSoundcheckCleared(crowdmike::rec::SoundcheckPlayer& soundcheck) {
    CM_EXPECT(!soundcheck.getLoadState().loaded);
    CM_EXPECT(!soundcheck.getLoadState().loading);
    CM_EXPECT(soundcheck.getInputMapping().empty());
    CM_EXPECT(soundcheck.isLocked());
    CM_EXPECT(!soundcheck.isArmed());
    processSoundcheck(soundcheck);
    CM_EXPECT(!soundcheck.isReplacingInputs());
    CM_EXPECT(!soundcheck.isPlaying());
    soundcheck.setLocked(false);
    CM_EXPECT(!soundcheck.arm());
    soundcheck.play();
    processSoundcheck(soundcheck);
    CM_EXPECT(!soundcheck.isReplacingInputs());
}
}

CM_TEST(wrapper_state_missing_playback_resets_bed_and_soundcheck) {
    WrapperSoundcheckFixture files;
    CrowdMikeAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);
    juce::MemoryBlock defaults;
    processor.getStateInformation(defaults);
    auto bed = processor.getPlayback().getBed();
    bed.enabled = true;
    bed.gainDb = -3.0f;
    processor.setBedSettings(bed);
    loadAndArm(processor, files.session);

    processor.setStateInformation(defaults.getData(), static_cast<int>(defaults.getSize()));
    CM_EXPECT(!processor.getPlayback().getBed().enabled);
    CM_EXPECT_NEAR(processor.getPlayback().getBed().gainDb,
                   crowdmike::rec::PlaybackEngine::BedSettings {}.gainDb, 1.0e-6);
    expectSoundcheckCleared(processor.getSoundcheck());
    processor.prepareToPlay(44100.0, 64);
    CM_EXPECT(processor.getSoundcheck().waitForLoad(10000));
    CM_EXPECT(!processor.getSoundcheck().getLoadState().loaded);
}

CM_TEST(wrapper_state_missing_soundcheck_file_cannot_keep_previous_session) {
    WrapperSoundcheckFixture files;
    CrowdMikeAudioProcessor processor;
    processor.prepareToPlay(48000.0, 64);
    loadAndArm(processor, files.session);
    const auto missing = files.folder.getChildFile("missing.json");
    CM_EXPECT(!processor.loadSoundcheckSession(missing));
    CM_EXPECT(processor.isSoundcheckSessionMissing());
    expectSoundcheckCleared(processor.getSoundcheck());
}
