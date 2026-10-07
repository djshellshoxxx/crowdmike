#include "PluginProcessor.h"

CrowdMikeAudioProcessor::CrowdMikeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CrowdMikeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    inputs.resize(static_cast<size_t>(getTotalNumInputChannels()));
    for (auto& input : inputs)
        input.prepare(sampleRate, samplesPerBlock, 1);
    limiter.prepare(sampleRate, samplesPerBlock, juce::jmax(1, getTotalNumOutputChannels()));
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
    const auto ins = getTotalNumInputChannels();
    const auto outs = getTotalNumOutputChannels();

    for (int ch = 0; ch < ins && ch < buffer.getNumChannels(); ++ch)
    {
        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers() + ch, 1, buffer.getNumSamples());
        inputs[static_cast<size_t>(ch)].process(view);
    }

    for (int ch = ins; ch < outs && ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    limiter.process(buffer);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrowdMikeAudioProcessor();
}
