// Quantised Echo/Reverse triggers through the whole processor (BEAT_SYNC_SPEC section 7).
#include "TestHarness.h"
#include "PluginProcessor.h"

using namespace crowdmike;

namespace {
void setActual(CrowdMikeAudioProcessor& p, const char* id, float v) {
    auto* q = p.getParameters().getParameter(id);
    q->setValueNotifyingHost(q->convertTo0to1(v));
}
void runBlocks(CrowdMikeAudioProcessor& p, int blocks, int n, float amp) {
    juce::AudioBuffer<float> b(2, n);
    juce::MidiBuffer midi;
    static int phase = 0;
    for (int k = 0; k < blocks; ++k) {
        for (int s = 0; s < n; ++s, ++phase) { const float v = amp * std::sin(0.05f * static_cast<float>(phase)); b.setSample(0, s, v); b.setSample(1, s, v); }
        p.processBlock(b, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < n; ++s) CM_EXPECT(std::isfinite(b.getSample(ch, s)));
    }
}
}

CM_TEST(quantised_reverse_trigger_waits_for_the_beat_grid) {
    CrowdMikeAudioProcessor p;
    constexpr int n = 256;
    p.prepareToPlay(48000.0, n);
    setActual(p, "tempo.source", 2.0f);         // Manual 120 BPM => 24000 samples per beat
    setActual(p, "tempo.manualBpm", 120.0f);
    setActual(p, "tempo.quantise", 1.0f);       // 1/4
    setActual(p, "fx.reverse.mix", 1.0f);
    runBlocks(p, 40, n, 0.3f);                  // fill the capture buffer, land mid-beat
    const auto idle = p.getEngine().getReverseViz();
    CM_EXPECT(idle.state == 0 && !idle.pending);
    setActual(p, "fx.reverse.trigger", 1.0f);
    int firedBlock = -1;
    double startPpq = 0.0;
    for (int blk = 0; blk < 200 && firedBlock < 0; ++blk) {
        runBlocks(p, 1, n, 0.3f);
        const auto v = p.getEngine().getReverseViz();
        if (v.state != 0 || v.pending) { firedBlock = blk; startPpq = p.getTempo().snapshot().ppq; }
    }
    CM_EXPECT(firedBlock > 0);                  // not immediate: it waited for the grid
    // the snapshot holds the position at the start of the block in which it fired: that block contains a beat line
    const double endPpq = startPpq + n * 120.0 / 60.0 / 48000.0;
    CM_EXPECT(std::floor(endPpq) > std::floor(startPpq) || std::abs(startPpq - std::round(startPpq)) < 1e-9);
}

CM_TEST(unquantised_reverse_trigger_fires_immediately) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    setActual(p, "fx.reverse.mix", 1.0f);
    runBlocks(p, 40, 256, 0.3f);
    setActual(p, "fx.reverse.trigger", 1.0f);
    runBlocks(p, 1, 256, 0.3f);
    const auto v = p.getEngine().getReverseViz();
    CM_EXPECT(v.state != 0 || v.pending);
}
