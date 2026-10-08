#include "InputStrip.h"

namespace crowdmike
{
void InputStrip::prepare(double sampleRate, int maximumBlockSize, int channels)
{
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(maximumBlockSize),
                                  static_cast<juce::uint32>(channels) };
    highPass.prepare(spec);
    lowPass.prepare(spec);
    highPass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    lowPass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    highPass.setCutoffFrequency(highPassHz);
    lowPass.setCutoffFrequency(lowPassHz);
    highPassCutoff.reset(sampleRate, 0.02);
    lowPassCutoff.reset(sampleRate, 0.02);
    highPassCutoff.setCurrentAndTargetValue(highPassHz);
    lowPassCutoff.setCurrentAndTargetValue(lowPassHz);
    muteGain.reset(sampleRate, 0.01);
    muteGain.setCurrentAndTargetValue(muted.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
    trimGain.reset(sampleRate, 0.01);
    trimGain.setCurrentAndTargetValue(targetGain);
    reset();
}

void InputStrip::reset()
{
    highPass.reset();
    lowPass.reset();
    peakLevel.store(0.0f, std::memory_order_relaxed);
}

void InputStrip::setTrimDb(float db) noexcept
{
    const float newGain = juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 24.0f, db));
    if (newGain != targetGain) {
        targetGain = newGain;
        trimGain.setTargetValue(targetGain);
    }
}

void InputStrip::setHighPassHz(float hz)
{
    const float boundedHz = juce::jlimit(20.0f, 500.0f, hz);
    if (boundedHz != highPassHz) {
        highPassHz = boundedHz;
        highPassCutoff.setTargetValue(highPassHz);
    }
}

void InputStrip::setLowPassHz(float hz)
{
    const float boundedHz = juce::jlimit(2000.0f, 20000.0f, hz);
    if (boundedHz != lowPassHz) {
        lowPassHz = boundedHz;
        lowPassCutoff.setTargetValue(lowPassHz);
    }
}

void InputStrip::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const float muteTarget = muted.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    if (muteGain.getTargetValue() != muteTarget)
        muteGain.setTargetValue(muteTarget);

    const int samples = buffer.getNumSamples();
    for (int sample = 0; sample < samples; ++sample)
    {
        const float currentGain = trimGain.getNextValue() * polarity * muteGain.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.getWritePointer(channel)[sample] *= currentGain;
    }
    juce::dsp::AudioBlock<float> block(buffer);
    constexpr int cutoffUpdateInterval = 16;
    for (int start = 0; start < samples; start += cutoffUpdateInterval)
    {
        const int count = juce::jmin(cutoffUpdateInterval, samples - start);
        const float hp = highPassCutoff.skip(count);
        const float lp = lowPassCutoff.skip(count);
        auto segment = block.getSubBlock(static_cast<size_t>(start), static_cast<size_t>(count));
        juce::dsp::ProcessContextReplacing<float> context(segment);
        if (highPassEnabled)
        {
            highPass.setCutoffFrequency(hp);
            highPass.process(context);
        }
        if (lowPassEnabled)
        {
            lowPass.setCutoffFrequency(lp);
            lowPass.process(context);
        }
    }

    const float blockPeak = buffer.getMagnitude(0, buffer.getNumSamples());
    float previousPeak = peakLevel.load(std::memory_order_relaxed);
    while (blockPeak > previousPeak
           && !peakLevel.compare_exchange_weak(previousPeak, blockPeak,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed))
    {
    }
}
}
