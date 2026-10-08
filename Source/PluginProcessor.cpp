#include "PluginProcessor.h"

CrowdMikeAudioProcessor::CrowdMikeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void CrowdMikeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int ins = juce::jlimit(1, crowdmike::RoutingMatrix::maxChannels,
                                 getTotalNumInputChannels());
    const int outs = juce::jlimit(1, crowdmike::RoutingMatrix::maxChannels,
                                  getTotalNumOutputChannels());
    preparedInputCount = ins;
    inputScratch.setSize(ins, juce::jmax(1, samplesPerBlock), false, true, false);
    for (int inputIndex = 0; inputIndex < preparedInputCount; ++inputIndex)
        inputs[static_cast<size_t>(inputIndex)].prepare(sampleRate, samplesPerBlock, 1);
    routing.resetToIdentity(ins, outs);
    limiter.prepare(sampleRate, samplesPerBlock, outs);
}

bool CrowdMikeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return ! in.isDisabled() && ! out.isDisabled()
        && in.size() >= 1 && in.size() <= crowdmike::RoutingMatrix::maxChannels
        && out.size() >= 1 && out.size() <= crowdmike::RoutingMatrix::maxChannels;
}

void CrowdMikeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int samples = buffer.getNumSamples();
    const int availableInputs = juce::jmin(getTotalNumInputChannels(), inputScratch.getNumChannels());
    const int ins = juce::jmin(availableInputs, preparedInputCount);
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
