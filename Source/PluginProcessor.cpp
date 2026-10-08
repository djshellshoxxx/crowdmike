#include "PluginProcessor.h"

namespace {
constexpr auto stateType = "CrowdMikeState";
constexpr float defaultHighPassHz = 80.0f;
constexpr float defaultLowPassHz = 18000.0f;
}

CrowdMikeAudioProcessor::CrowdMikeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
    for (int inputIndex = 0; inputIndex < crowdmike::RoutingMatrix::maxChannels; ++inputIndex) {
        auto& pointers = inputParameters[static_cast<size_t>(inputIndex)];
        pointers.trimDb = parameters.getRawParameterValue(inputParameterId(inputIndex, "trimDb"));
        pointers.muted = parameters.getRawParameterValue(inputParameterId(inputIndex, "mute"));
        pointers.polarityInverted = parameters.getRawParameterValue(inputParameterId(inputIndex, "polarity"));
        pointers.highPassEnabled = parameters.getRawParameterValue(inputParameterId(inputIndex, "highPassEnabled"));
        pointers.highPassHz = parameters.getRawParameterValue(inputParameterId(inputIndex, "highPassHz"));
        pointers.lowPassEnabled = parameters.getRawParameterValue(inputParameterId(inputIndex, "lowPassEnabled"));
        pointers.lowPassHz = parameters.getRawParameterValue(inputParameterId(inputIndex, "lowPassHz"));
    }
    limiterCeilingDb = parameters.getRawParameterValue("limiterCeilingDb");
}

juce::String CrowdMikeAudioProcessor::inputParameterId(int inputIndex, const juce::String& suffix)
{
    return "input" + juce::String(inputIndex + 1) + "." + suffix;
}

juce::AudioProcessorValueTreeState::ParameterLayout CrowdMikeAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int inputIndex = 0; inputIndex < crowdmike::RoutingMatrix::maxChannels; ++inputIndex) {
        const auto channelName = "Input " + juce::String(inputIndex + 1);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { inputParameterId(inputIndex, "trimDb"), 1 },
            channelName + " Trim", juce::NormalisableRange<float> { -60.0f, 24.0f, 0.01f }, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { inputParameterId(inputIndex, "mute"), 1 },
            channelName + " Mute", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { inputParameterId(inputIndex, "polarity"), 1 },
            channelName + " Polarity Invert", false));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { inputParameterId(inputIndex, "highPassEnabled"), 1 },
            channelName + " High Pass", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { inputParameterId(inputIndex, "highPassHz"), 1 },
            channelName + " High Pass Frequency",
            juce::NormalisableRange<float> { 20.0f, 500.0f, 1.0f }, defaultHighPassHz));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { inputParameterId(inputIndex, "lowPassEnabled"), 1 },
            channelName + " Low Pass", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { inputParameterId(inputIndex, "lowPassHz"), 1 },
            channelName + " Low Pass Frequency",
            juce::NormalisableRange<float> { 2000.0f, 20000.0f, 1.0f }, defaultLowPassHz));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "limiterCeilingDb", 1 }, "Limiter Ceiling",
        juce::NormalisableRange<float> { -12.0f, 0.0f, 0.1f }, -0.5f));
    return layout;
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

    for (int inputIndex = 0; inputIndex < ins; ++inputIndex) {
        auto& strip = inputs[static_cast<size_t>(inputIndex)];
        const auto& controls = inputParameters[static_cast<size_t>(inputIndex)];
        strip.setTrimDb(controls.trimDb->load(std::memory_order_relaxed));
        strip.setMuted(controls.muted->load(std::memory_order_relaxed) >= 0.5f);
        strip.setPolarityInverted(controls.polarityInverted->load(std::memory_order_relaxed) >= 0.5f);
        strip.setHighPassEnabled(controls.highPassEnabled->load(std::memory_order_relaxed) >= 0.5f);
        strip.setHighPassHz(controls.highPassHz->load(std::memory_order_relaxed));
        strip.setLowPassEnabled(controls.lowPassEnabled->load(std::memory_order_relaxed) >= 0.5f);
        strip.setLowPassHz(controls.lowPassHz->load(std::memory_order_relaxed));
    }
    limiter.setCeilingDb(limiterCeilingDb->load(std::memory_order_relaxed));

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

void CrowdMikeAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const auto state = parameters.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void CrowdMikeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateType))
        return;
    parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrowdMikeAudioProcessor();
}
