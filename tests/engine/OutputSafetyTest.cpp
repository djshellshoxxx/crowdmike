#include "TestHarness.h"
#include "PluginProcessor.h"
#include <memory>

namespace {
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

struct SafetyRig {
    std::unique_ptr<CrowdMikeAudioProcessor> processor = std::make_unique<CrowdMikeAudioProcessor>();
    juce::AudioBuffer<float> audio;
    juce::MidiBuffer midi;
    int inputs;

    SafetyRig(int ins, int outs) : audio(std::max(ins, outs), blockSize), inputs(ins) {
        auto layout = processor->getBusesLayout();
        layout.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(ins);
        layout.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(outs);
        CM_EXPECT(processor->setBusesLayout(layout));
        processor->prepareToPlay(sampleRate, blockSize);
        processor->getRouting().clearAll(); // A dry signal here must come from bypass.
        processor->getRouting().setHasSavedDirect(true);
        set("bypass", true);
        render(20);
    }

    void set(const char* id, bool on) {
        auto* parameter = processor->getParameters().getParameter(id);
        CM_EXPECT(parameter != nullptr);
        if (parameter != nullptr) parameter->setValueNotifyingHost(on ? 1.0f : 0.0f);
    }

    float inputLevel(int channel) const { return 0.2f * static_cast<float>(channel + 1) / static_cast<float>(inputs); }

    void render(int blocks) {
        for (int block = 0; block < blocks; ++block) {
            audio.clear();
            for (int channel = 0; channel < inputs; ++channel)
                juce::FloatVectorOperations::fill(audio.getWritePointer(channel), inputLevel(channel), blockSize);
            processor->processBlock(audio, midi);
        }
    }

    void expectDryOutput() const {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
            const float expected = channel < inputs ? inputLevel(channel) : 0.0f;
            for (int sample = 0; sample < blockSize; ++sample)
                CM_EXPECT_NEAR(audio.getSample(channel, sample), expected, 1.0e-6f);
        }
    }
};
}

CM_TEST(output_safety_bypass_preserves_the_dry_signal) {
    for (const auto& channels : { std::pair { 1, 1 }, { 1, 2 }, { 2, 2 }, { 8, 8 }, { 16, 16 } }) {
        SafetyRig rig(channels.first, channels.second);
        rig.expectDryOutput();
    }
}

CM_TEST(output_safety_panic_mutes_settled_bypass_in_mono_and_multichannel_layouts) {
    for (const auto& channels : { std::pair { 1, 1 }, { 1, 2 }, { 2, 2 }, { 8, 8 }, { 16, 16 } }) {
        SafetyRig rig(channels.first, channels.second);
        rig.expectDryOutput();
        rig.set("panic", true);
        rig.render(10);
        CM_EXPECT(rig.audio.getMagnitude(0, blockSize) < 1.0e-7f);
        CM_EXPECT(rig.processor->getMeters().panicActive.load());
        rig.set("panic", false);
        rig.render(20);
        rig.expectDryOutput();
    }
}

CM_TEST(output_safety_rehearsal_mutes_settled_bypass) {
    SafetyRig rig(8, 8);
    rig.set("rehearsal", true);
    rig.render(20);
    CM_EXPECT(rig.audio.getMagnitude(0, blockSize) < 1.0e-7f);
    CM_EXPECT(rig.processor->getMeters().rehearsalActive.load());
    rig.set("rehearsal", false);
    rig.render(20);
    rig.expectDryOutput();
}
