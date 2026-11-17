#include "RecTestUtil.h"
#include "Recording/PlaybackEngine.h"
#include <algorithm>

using namespace crowdmike::rec;

namespace {
constexpr int kBlock = 256;
struct Block {
    float l[kBlock] {}, r[kBlock] {};
    float* left[PlaybackEngine::kNumDestinations] { l };
    float* right[PlaybackEngine::kNumDestinations] { r };
    void render(PlaybackEngine& player) {
        std::fill_n(l, kBlock, 0.0f);
        std::fill_n(r, kBlock, 0.0f);
        player.process(left, right, kBlock, false);
    }
};
}

CM_TEST(Streaming_TrimTriggerUsesRingWhenPublishedHeadIsStale) {
    rectest::TempDir tmp("stream-trim-safety");
    juce::AudioBuffer<float> audio(1, 48000 * 4);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample(0, i, i < 48000 * 2 ? 0.25f : 0.75f);
    const auto file = tmp.dir.getChildFile("clip.wav");
    CM_EXPECT(rectest::writeWav(file, audio, 48000.0));
    PlaybackEngine player;
    player.prepare(48000.0, kBlock, false);
    player.setResidentLimitSeconds(0.0);
    CM_EXPECT(player.loadClip(0, file));
    CM_EXPECT(player.waitForLoads(10000));
    Block block;
    block.render(player); // adopt the original head
    CM_EXPECT(player.trigger(0));
    block.render(player);

    PlaybackEngine::ClipSettings settings;
    settings.startFrac = 0.25;
    CM_EXPECT(player.setSettings(0, settings));
    CM_EXPECT(player.waitForLoads(10000));
    block.render(player); // adopt the first edit while the original voice retains the old head

    settings.startFrac = 0.5;
    CM_EXPECT(player.setSettings(0, settings));
    CM_EXPECT(player.waitForLoads(10000));
    CM_EXPECT(player.trigger(0));
    block.render(player); // the retained old voice delays adoption of the second edit
    player.pumpStreamsForTesting();
    block.render(player); // the fresh ring is ready; its first frame is at 2 seconds (0.75)
    CM_EXPECT_NEAR(block.l[kBlock - 1], 0.75, 1.0e-6);
    CM_EXPECT_NEAR(block.r[kBlock - 1], 0.75, 1.0e-6);
}

CM_TEST(Streaming_SyncedRestartRefreshesHeadAfterRestoringTrim) {
    rectest::TempDir tmp("stream-sync-head-safety");
    juce::AudioBuffer<float> audio(1, 48000 * 4);
    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample(0, i, i < 48000 * 2 ? 0.25f : 0.75f);
    const auto file = tmp.dir.getChildFile("clip.wav");
    CM_EXPECT(rectest::writeWav(file, audio, 48000.0));
    PlaybackEngine player;
    player.prepare(48000.0, kBlock, false);
    player.setResidentLimitSeconds(0.0);
    CM_EXPECT(player.loadClip(0, file));
    CM_EXPECT(player.waitForLoads(10000));
    Block block;
    block.render(player);
    CM_EXPECT(player.trigger(0));
    block.render(player);

    PlaybackEngine::ClipSettings settings;
    settings.startFrac = 0.25;
    settings.mode = PlaybackEngine::ClipSettings::Loop;
    settings.tempoSync = true;
    settings.syncLoopBeats = 1.0;
    CM_EXPECT(player.setSettings(0, settings));
    CM_EXPECT(player.waitForLoads(10000));
    block.render(player);

    settings.startFrac = 0.5;
    CM_EXPECT(player.setSettings(0, settings));
    CM_EXPECT(player.waitForLoads(10000));
    CM_EXPECT(player.triggerNow(0));
    player.setTempo({ 120.0, 0.0, true, true });
    block.render(player); // start a voice whose cached head does not match this trim
    player.pumpStreamsForTesting();
    player.setTempo({ 120.0, 0.01, true, true });
    block.render(player);
    CM_EXPECT_NEAR(block.l[kBlock - 1], 0.75, 1.0e-6);

    settings.startFrac = 0.25; // restore the playing voice's cached-head key
    CM_EXPECT(player.setSettings(0, settings));
    CM_EXPECT(player.waitForLoads(10000));
    player.setTempo({ 120.0, 0.995, true, true });
    block.render(player); // the beat restart falls at frame 120 inside this block
    // The restored 0.25 head has advanced 135 samples into its 240-sample declick ramp.
    CM_EXPECT_NEAR(block.l[kBlock - 1], 0.140625, 1.0e-6);
    CM_EXPECT_NEAR(block.r[kBlock - 1], 0.140625, 1.0e-6);
}

CM_TEST(Streaming_BedStoppedBeforeInitialFillReturnsItsRing) {
    rectest::TempDir tmp("stream-bed-safety");
    juce::AudioBuffer<float> audio(1, 48000 * 4);
    audio.clear();
    const auto file = tmp.dir.getChildFile("bed.wav");
    CM_EXPECT(rectest::writeWav(file, audio, 48000.0));
    for (bool unload : { false, true }) {
        PlaybackEngine player;
        player.prepare(48000.0, kBlock, false);
        player.setResidentLimitSeconds(0.0);
        CM_EXPECT(player.loadClip(0, file));
        CM_EXPECT(player.waitForLoads(10000));
        Block block;
        block.render(player);
        PlaybackEngine::BedSettings bed;
        bed.enabled = true;
        CM_EXPECT(player.setBed(bed));
        block.render(player); // request a ring, but do not let the streamer fill it yet
        CM_EXPECT(player.getStreamStats().activeRings == 1);
        if (unload) player.unloadClip(0);
        else {
            bed.enabled = false;
            CM_EXPECT(player.setBed(bed));
        }
        block.render(player); // no audible bed tail exists to fade
        player.pumpStreamsForTesting();
        CM_EXPECT(player.getStreamStats().activeRings == 0);
    }
}
