#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
    resetRequestedRouting(2, 2);
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
    activeInputChannels.store(ins, std::memory_order_relaxed);
    activeOutputChannels.store(outs, std::memory_order_relaxed);
    if (!hasSavedRouting.load(std::memory_order_relaxed))
        resetRequestedRouting(ins, outs);
    inputScratch.setSize(ins, juce::jmax(1, samplesPerBlock), false, true, false);
    for (int inputIndex = 0; inputIndex < preparedInputCount; ++inputIndex)
        inputs[static_cast<size_t>(inputIndex)].prepare(sampleRate, samplesPerBlock, 1);
    routing.resetToIdentity(ins, outs);
    routing.prepare(sampleRate);
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
    const int outs = juce::jmin(getTotalNumOutputChannels(), crowdmike::RoutingMatrix::maxChannels);
    for (int output = 0; output < outs; ++output)
        for (int input = 0; input < ins; ++input) {
            const float target = requestedRouteGains[static_cast<size_t>(output * crowdmike::RoutingMatrix::maxChannels + input)]
                                     .load(std::memory_order_relaxed);
            if (target != routing.getRouteGain(input, output))
                routing.setTargetRouteGain(input, output, target);
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

void CrowdMikeAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty("schemaVersion", 1, nullptr);
    if (auto xml = state.createXml()) {
        if (hasSavedRouting.load(std::memory_order_relaxed)) {
            auto routes = std::make_unique<juce::XmlElement>("RoutingMatrix");
            for (int output = 0; output < crowdmike::RoutingMatrix::maxChannels; ++output)
                for (int input = 0; input < crowdmike::RoutingMatrix::maxChannels; ++input) {
                    const auto key = "r" + juce::String(input + 1) + "_" + juce::String(output + 1);
                    const auto index = static_cast<size_t>(output * crowdmike::RoutingMatrix::maxChannels + input);
                    routes->setAttribute(key, requestedRouteGains[index].load(std::memory_order_relaxed));
                }
            xml->addChildElement(routes.release());
        }
        copyXmlToBinary(*xml, destination);
    }
}

void CrowdMikeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(stateType)
        || xml->getIntAttribute("schemaVersion", -1) != 1)
        return;
    if (auto* routes = xml->getChildByName("RoutingMatrix")) {
        for (int output = 0; output < crowdmike::RoutingMatrix::maxChannels; ++output)
            for (int input = 0; input < crowdmike::RoutingMatrix::maxChannels; ++input) {
                const auto key = "r" + juce::String(input + 1) + "_" + juce::String(output + 1);
                const double rawGain = routes->getDoubleAttribute(key, 0.0);
                const float gain = std::isfinite(rawGain)
                    ? juce::jlimit(-2.0f, 2.0f, static_cast<float>(rawGain)) : 0.0f;
                const auto index = static_cast<size_t>(output * crowdmike::RoutingMatrix::maxChannels + input);
                requestedRouteGains[index].store(gain, std::memory_order_relaxed);
            }
        hasSavedRouting.store(true, std::memory_order_relaxed);
    } else {
        hasSavedRouting.store(false, std::memory_order_relaxed);
        resetRequestedRouting(activeInputChannels.load(std::memory_order_relaxed),
                              activeOutputChannels.load(std::memory_order_relaxed));
    }

    auto parameterState = juce::ValueTree::fromXml(*xml);
    for (int childIndex = parameterState.getNumChildren() - 1; childIndex >= 0; --childIndex)
        if (parameterState.getChild(childIndex).getType().toString() == "RoutingMatrix")
            parameterState.removeChild(childIndex, nullptr);
    parameters.replaceState(parameterState);
}

void CrowdMikeAudioProcessor::resetRequestedRouting(int inputChannels, int outputChannels) noexcept
{
    crowdmike::RoutingMatrix defaults;
    defaults.resetToIdentity(inputChannels, outputChannels);
    for (int output = 0; output < crowdmike::RoutingMatrix::maxChannels; ++output)
        for (int input = 0; input < crowdmike::RoutingMatrix::maxChannels; ++input) {
            const float gain = (input < inputChannels && output < outputChannels)
                ? defaults.getRouteGain(input, output) : 0.0f;
            requestedRouteGains[static_cast<size_t>(output * crowdmike::RoutingMatrix::maxChannels + input)]
                .store(gain, std::memory_order_relaxed);
        }
}

float CrowdMikeAudioProcessor::getRequestedRouteGain(int inputIndex, int outputIndex) const noexcept
{
    if (inputIndex < 0 || inputIndex >= crowdmike::RoutingMatrix::maxChannels
        || outputIndex < 0 || outputIndex >= crowdmike::RoutingMatrix::maxChannels)
        return 0.0f;
    return requestedRouteGains[static_cast<size_t>(outputIndex * crowdmike::RoutingMatrix::maxChannels + inputIndex)]
        .load(std::memory_order_relaxed);
}

bool CrowdMikeAudioProcessor::setRequestedRouteGain(int inputIndex, int outputIndex, float linearGain) noexcept
{
    if (inputIndex < 0 || inputIndex >= crowdmike::RoutingMatrix::maxChannels
        || outputIndex < 0 || outputIndex >= crowdmike::RoutingMatrix::maxChannels
        || !std::isfinite(linearGain))
        return false;
    requestedRouteGains[static_cast<size_t>(outputIndex * crowdmike::RoutingMatrix::maxChannels + inputIndex)]
        .store(juce::jlimit(-2.0f, 2.0f, linearGain), std::memory_order_relaxed);
    hasSavedRouting.store(true, std::memory_order_relaxed);
    return true;
}

float CrowdMikeAudioProcessor::getAndResetInputPeak(int inputIndex) noexcept
{
    if (inputIndex < 0 || inputIndex >= crowdmike::RoutingMatrix::maxChannels)
        return 0.0f;
    return inputs[static_cast<size_t>(inputIndex)].getAndResetPeak();
}

juce::AudioProcessorEditor* CrowdMikeAudioProcessor::createEditor()
{
    return new CrowdMikeAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrowdMikeAudioProcessor();
}
