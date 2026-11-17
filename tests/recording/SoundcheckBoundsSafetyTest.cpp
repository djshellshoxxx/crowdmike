#include "RecTestUtil.h"
#include "Recording/SoundcheckPlayer.h"

namespace {
juce::File timelineSession(const juce::File& folder, juce::int64 offset, int inputs) {
    juce::AudioBuffer<float> audio(1, 64);
    juce::FloatVectorOperations::fill(audio.getWritePointer(0), 0.25f, audio.getNumSamples());
    const auto wav = folder.getChildFile("input.wav");
    CM_EXPECT(rectest::writeWav(wav, audio, 48000.0));
    crowdmike::rec::SessionInfo info;
    info.sampleRate = 48000.0;
    info.totalFrames = offset + audio.getNumSamples();
    for (int input = 0; input < inputs; ++input) {
        info.tracks.push_back({ input, input, "Input " + juce::String(input + 1), 1 });
        crowdmike::rec::SessionInfo::FileEntry file;
        file.fileName = wav.getFileName();
        file.file = wav;
        file.track = input;
        file.sourceId = input;
        file.startFrame = offset;
        file.numFrames = audio.getNumSamples();
        info.files.push_back(file);
    }
    const auto json = folder.getChildFile("session.json");
    juce::String error;
    CM_EXPECT(crowdmike::rec::SessionMetadata::write(json, info, error));
    return json;
}
void expectTimelineRejected(juce::int64 offset, int inputs) {
    rectest::TempDir temp("soundcheck_bounds");
    crowdmike::rec::SoundcheckPlayer soundcheck;
    soundcheck.prepare(48000.0, 64);
    CM_EXPECT(soundcheck.loadSession(timelineSession(temp.dir, offset, inputs)));
    CM_EXPECT(soundcheck.waitForLoad(10000));
    const auto state = soundcheck.getLoadState();
    CM_EXPECT(!state.loading && !state.loaded);
    CM_EXPECT(state.error.containsIgnoreCase("timeline"));
    CM_EXPECT(soundcheck.getInputMapping().empty());
    CM_EXPECT(soundcheck.getDurationSeconds() == 0.0);
}
}

CM_TEST(soundcheck_bounds_reject_huge_positive_part_offset_before_allocation) {
    expectTimelineRejected(juce::int64 { 1 } << 40, 1);
}

CM_TEST(soundcheck_bounds_reject_sparse_multichannel_timeline_over_memory_budget) {
    // Each channel separately fits the frame budget; their assembled two-channel timeline does not.
    expectTimelineRejected(450000000, 2);
}

CM_TEST(soundcheck_bounds_accept_ordinary_sparse_session) {
    rectest::TempDir temp("soundcheck_bounds_valid");
    crowdmike::rec::SoundcheckPlayer soundcheck;
    soundcheck.prepare(48000.0, 64);
    CM_EXPECT(soundcheck.loadSession(timelineSession(temp.dir, 100, 2)));
    CM_EXPECT(soundcheck.waitForLoad(10000));
    const auto state = soundcheck.getLoadState();
    CM_EXPECT(state.loaded && state.error.isEmpty() && state.numInputs == 2);
    CM_EXPECT_NEAR(state.durationSeconds, 164.0 / 48000.0, 1.0e-12);
}
