#include "TestHarness.h"
#include "PluginProcessor.h"
#include <cmath>

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
constexpr double kSr = 48000.0;

void setParam(CrowdMikeAudioProcessor& p, const char* id, float actual) {
    auto* q = p.getParameters().getParameter(id);
    q->setValueNotifyingHost(q->convertTo0to1(actual));
}
bool layout(CrowdMikeAudioProcessor& p, int in, int out) {
    auto l = p.getBusesLayout();
    l.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(in);
    l.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(out);
    return p.setBusesLayout(l);
}
// Runs `blocks` blocks; fills channel c with value[c] (DC) and returns the last block.
juce::AudioBuffer<float> run(CrowdMikeAudioProcessor& p, int channels, int blockSize, int blocks, const std::vector<float>& dc) {
    juce::AudioBuffer<float> b(channels, blockSize);
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k) {
        for (int c = 0; c < channels; ++c)
            for (int s = 0; s < blockSize; ++s) b.setSample(c, s, c < static_cast<int>(dc.size()) ? dc[static_cast<size_t>(c)] : 0.0f);
        p.processBlock(b, midi);
    }
    return b;
}
bool allFinite(const juce::AudioBuffer<float>& b) {
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int s = 0; s < b.getNumSamples(); ++s) if (!std::isfinite(b.getSample(c, s))) return false;
    return true;
}
}

CM_TEST(engine_default_is_transparent_passthrough) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 256);
    const auto out = run(p, 2, 256, 20, { 0.1f, -0.2f });
    CM_EXPECT_NEAR(out.getSample(0, 200), 0.1, 1e-4);
    CM_EXPECT_NEAR(out.getSample(1, 200), -0.2, 1e-4);
}

CM_TEST(engine_reports_constant_latency_and_delays_impulse_exactly) {
    CrowdMikeAudioProcessor q;
    q.prepareToPlay(kSr, 128);
    const int latency = q.getLatencySamples();
    CM_EXPECT(latency > 0 && latency < 200);
    juce::MidiBuffer midi;
    std::vector<float> stream;
    for (int blk = 0; blk < 4; ++blk) {
        juce::AudioBuffer<float> in(2, 128);
        in.clear();
        if (blk == 0) in.setSample(0, 0, 0.5f);
        q.processBlock(in, midi);
        for (int s = 0; s < 128; ++s) stream.push_back(in.getSample(0, s));
    }
    int first = -1;
    for (size_t i = 0; i < stream.size() && first < 0; ++i) if (std::abs(stream[i]) > 0.1f) first = static_cast<int>(i);
    CM_EXPECT(first == latency);
    CM_EXPECT_NEAR(stream[static_cast<size_t>(first)], 0.5, 0.01);
}

CM_TEST(engine_panic_silences_outputs_quickly_but_recording_taps_continue) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 128);
    run(p, 2, 128, 10, { 0.3f, 0.3f });
    setParam(p, "panic", 1.0f);
    const auto out = run(p, 2, 128, 4, { 0.3f, 0.3f });   // 4*128 = 10.6 ms
    CM_EXPECT(out.getMagnitude(0, 0, 128) < 1e-5f);
    CM_EXPECT(p.getMeters().panicActive.load());
    setParam(p, "panic", 0.0f);
    const auto back = run(p, 2, 128, 8, { 0.3f, 0.3f });
    CM_EXPECT_NEAR(back.getSample(0, 100), 0.3, 1e-3);
}

CM_TEST(engine_bypass_is_latency_aligned_dry_signal) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 128);
    setParam(p, "input1.trimDb", -30.0f);        // would be obvious if processing leaked through
    setParam(p, "bypass", 1.0f);
    const auto out = run(p, 2, 128, 20, { 0.25f, 0.25f });
    CM_EXPECT_NEAR(out.getSample(0, 100), 0.25, 1e-3);
}

CM_TEST(engine_group_chain_routes_master_to_outputs_via_template) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 256);
    p.applyWiringTemplate(2);                    // two mic stereo crowd -> stereo PA (HPF 100 Hz on, so use a tone)
    auto tone = [&](bool in1, bool in2) {
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer midi;
        float peakL = 0.0f, peakR = 0.0f;
        for (int blk = 0; blk < 60; ++blk) {
            for (int s = 0; s < 256; ++s) {
                const float v = 0.2f * std::sin(6.2831853f * 1000.0f * static_cast<float>(blk * 256 + s) / 48000.0f);
                b.setSample(0, s, in1 ? v : 0.0f);
                b.setSample(1, s, in2 ? v : 0.0f);
            }
            p.processBlock(b, midi);
            if (blk > 40) { peakL = std::max(peakL, b.getMagnitude(0, 0, 256)); peakR = std::max(peakR, b.getMagnitude(1, 0, 256)); }
        }
        return std::make_pair(peakL, peakR);
    };
    auto left = tone(true, false);
    CM_EXPECT(left.first > 0.05f);
    CM_EXPECT(left.second < 1e-3f);
    auto both = tone(true, true);
    CM_EXPECT(both.second > 0.05f);
}

CM_TEST(engine_output_never_exceeds_ceiling_even_with_unity_overload) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 128);
    setParam(p, "input1.trimDb", 24.0f);
    float peak = 0.0f;
    juce::AudioBuffer<float> b(2, 128);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < 200; ++blk) {
        for (int s = 0; s < 128; ++s) { const float v = 0.9f * std::sin(6.2831853f * 1000.0f * static_cast<float>(blk * 128 + s) / 48000.0f); b.setSample(0, s, v); b.setSample(1, s, v); }
        p.processBlock(b, midi);
        if (blk > 20) peak = std::max(peak, b.getMagnitude(0, 128));
    }
    CM_EXPECT(peak <= std::pow(10.0f, -0.5f / 20.0f) + 0.01f);
    CM_EXPECT(peak > 0.5f);
}

CM_TEST(engine_handles_oversized_blocks_and_bad_input) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 64);
    juce::AudioBuffer<float> b(2, 4096);
    juce::MidiBuffer midi;
    for (int s = 0; s < 4096; ++s) { b.setSample(0, s, 0.1f); b.setSample(1, s, (s % 7 == 0) ? NAN : INFINITY); }
    p.processBlock(b, midi);
    CM_EXPECT(allFinite(b));
    CM_EXPECT_NEAR(b.getSample(0, 4000), 0.1, 1e-3);
}

CM_TEST(engine_clears_unused_outputs_and_supports_many_layouts) {
    for (const auto& io : std::vector<std::pair<int, int>> { { 1, 1 }, { 1, 2 }, { 2, 2 }, { 4, 2 }, { 4, 4 }, { 8, 2 }, { 8, 8 }, { 16, 16 } }) {
        CrowdMikeAudioProcessor p;
        CM_EXPECT(layout(p, io.first, io.second));
        p.prepareToPlay(kSr, 128);
        const int ch = std::max(io.first, io.second);
        std::vector<float> dc(static_cast<size_t>(ch), 0.1f);
        auto out = run(p, ch, 128, 20, dc);
        CM_EXPECT(allFinite(out));
        for (int c = io.second; c < ch; ++c) CM_EXPECT(out.getMagnitude(c, 0, 128) == 0.0f);
        CM_EXPECT(out.getMagnitude(0, 0, 128) > 0.01f);
    }
}

CM_TEST(engine_mute_solo_and_polarity) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 128);
    setParam(p, "input1.solo", 1.0f);
    auto out = run(p, 2, 128, 20, { 0.2f, 0.2f });
    CM_EXPECT(out.getMagnitude(0, 0, 128) > 0.1f);
    CM_EXPECT(out.getMagnitude(1, 0, 128) < 1e-5f);       // input 2 muted by solo
    setParam(p, "input1.solo", 0.0f);
    setParam(p, "input1.polarity", 1.0f);
    out = run(p, 2, 128, 20, { 0.2f, 0.2f });
    CM_EXPECT_NEAR(out.getSample(0, 100), -0.2, 1e-4);
    CM_EXPECT_NEAR(out.getSample(1, 100), 0.2, 1e-4);
}

CM_TEST(engine_fx_send_produces_delayed_return) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 256);
    p.applyWiringTemplate(1);                                     // 1 mic -> stereo PA
    setParam(p, "group1.fxSend1", 0.0f);                          // delay send open
    setParam(p, "fx.delay.timeMs", 100.0f);
    setParam(p, "fx.delay.feedback", 0.0f);
    setParam(p, "fx.delay.mix", 1.0f);
    juce::AudioBuffer<float> b(2, 256);
    juce::MidiBuffer midi;
    double earlyEnergy = 0.0, lateEnergy = 0.0;
    for (int blk = 0; blk < 60; ++blk) {   // 320 ms
        b.clear();
        if (blk == 5) b.setSample(0, 0, 0.8f);                    // single click on input 1
        p.processBlock(b, midi);
        for (int s = 0; s < 256; ++s) {
            const double e = static_cast<double>(b.getSample(0, s)) * b.getSample(0, s);
            const int t = blk * 256 + s - 5 * 256;
            if (t > 0 && t < 3000) earlyEnergy += e;               // direct click (+latency) region
            if (t > 4500 && t < 5200) lateEnergy += e;             // ~100 ms later
        }
    }
    CM_EXPECT(lateEnergy > 1e-6);
}

CM_TEST(engine_recorder_taps_write_files) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 128);
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_engine_rec_test");
    folder.deleteRecursively();
    auto& rs = p.getShow().recorder;
    rs.folder = folder.getFullPathName();
    rs.format = 2;
    rs.tracks[0] = { 0 /*raw in 1*/, 1, true, false, false, "raw1" };
    p.applyShowChanges();
    CM_EXPECT(p.startRecording());
    run(p, 2, 128, 40, { 0.25f, 0.0f });
    p.getRecorder().stopRecordingNow();
    int wavs = 0;
    for (auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.wav")) { ++wavs; CM_EXPECT(f.getSize() > 128 * 40 * 4 / 2); }
    CM_EXPECT(wavs == 1);
    folder.deleteRecursively();
}

CM_TEST(engine_stress_all_channels_all_effects_stays_finite_and_fast) {
    CrowdMikeAudioProcessor p;
    CM_EXPECT(layout(p, 16, 16));
    p.prepareToPlay(kSr, 128);
    p.applyWiringTemplate(8);                                     // multi-zone
    for (int i = 1; i <= 16; ++i) {
        const auto pre = "input" + juce::String(i) + ".";
        for (const char* s : { "compOn", "gateOn", "deessOn", "transOn" }) setParam(p, (pre + s).toRawUTF8(), 1.0f);
        setParam(p, (pre + "notchOn").toRawUTF8(), 1.0f);
        setParam(p, (pre + "fxSend1").toRawUTF8(), -6.0f);
        setParam(p, (pre + "fxSend3").toRawUTF8(), -6.0f);
        setParam(p, (pre + "fxSend5").toRawUTF8(), -6.0f);
    }
    setParam(p, "fx.reverse.trigger", 1.0f);
    juce::AudioBuffer<float> b(16, 128);
    juce::MidiBuffer midi;
    juce::Random rng(7);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int blk = 0; blk < 400; ++blk) {
        for (int c = 0; c < 16; ++c) for (int s = 0; s < 128; ++s) b.setSample(c, s, (rng.nextFloat() - 0.5f) * 0.4f);
        p.processBlock(b, midi);
        CM_EXPECT(allFinite(b));
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const double budgetMs = 400.0 * 128.0 / kSr * 1000.0;
    CM_EXPECT(ms < budgetMs);          // real-time capable even in a Release-less test build on a shared core (generous)
}
