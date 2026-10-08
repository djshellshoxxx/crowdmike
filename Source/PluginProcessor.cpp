#include "PluginProcessor.h"

CrowdMikeAudioProcessor::CrowdMikeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CrowdMikeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int ins = juce::jmax(1, getTotalNumInputChannels());
    const int outs = juce::jmax(1, getTotalNumOutputChannels());
    inputScratch.setSize(ins, juce::jmax(1, samplesPerBlock), false, true, false);
    inputs.resize(static_cast<size_t>(ins));
    for (auto& input : inputs)
        input.prepare(sampleRate, samplesPerBlock, 1);
    routing.resetToIdentity(ins, outs);
    limiter.prepare(sampleRate, samplesPerBlock, outs);
}

bool CrowdMikeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return ! in.isDisabled() && ! out.isDisabled()
        && in.size() >= 1 && in.size() <= 16
        && out.size() >= 1 && out.size() <= 16;
}

void CrowdMikeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int samples = buffer.getNumSamples();
    const int ins = juce::jmin(getTotalNumInputChannels(), inputScratch.getNumChannels());
    if (samples > inputScratch.getNumSamples()) {
        // Host supplied more frames than negotiated. Fail silent, never allocate on the audio thread.
        buffer.clear();
        return;
    }
    inputScratch.clear();
    for (int ch = 0; ch < ins && ch < buffer.getNumChannels(); ++ch) {
        inputScratch.copyFrom(ch, 0, buffer, ch, 0, samples);
        float* channel = inputScratch.getWritePointer(ch);
        juce::AudioBuffer<float> view(&channel, 1, samples);
        inputs[static_cast<size_t>(ch)].process(view);
    }
    juce::AudioBuffer<float> inputView(inputScratch.getArrayOfWritePointers(), ins, samples);
    routing.process(inputView, buffer);
    limiter.process(buffer);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrowdMikeAudioProcessor();
}
