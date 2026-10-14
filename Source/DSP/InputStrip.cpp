#include "InputStrip.h"

namespace crowdmike
{
void InputStrip::prepare(double sampleRate, int maximumBlockSize, int channels)
{
    preparedChannels = juce::jmax(1, channels);
    highPass.prepare(sampleRate, maximumBlockSize, preparedChannels, true);
    lowPass.prepare(sampleRate, maximumBlockSize, preparedChannels, false);
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

void InputStrip::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const float muteTarget = muted.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    if (muteGain.getTargetValue() != muteTarget)
        muteGain.setTargetValue(muteTarget);

    const int samples = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (trimGain.isSmoothing() || muteGain.isSmoothing()) {
        for (int sample = 0; sample < samples; ++sample)
        {
            const float currentGain = trimGain.getNextValue() * polarity * muteGain.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
                buffer.getWritePointer(channel)[sample] *= currentGain;
        }
    } else {
        const float gain = trimGain.getCurrentValue() * polarity * muteGain.getCurrentValue();
        if (gain != 1.0f)
            for (int channel = 0; channel < channels; ++channel)
                juce::FloatVectorOperations::multiply(buffer.getWritePointer(channel), gain, samples);
    }

    float* const* ptrs = buffer.getArrayOfWritePointers();
    highPass.process(ptrs, juce::jmin(channels, preparedChannels), samples, highPassEnabled, highPassHz, highPassSlope);
    lowPass.process(ptrs, juce::jmin(channels, preparedChannels), samples, lowPassEnabled, lowPassHz, lowPassSlope);

    const float blockPeak = buffer.getMagnitude(0, samples);
    float previousPeak = peakLevel.load(std::memory_order_relaxed);
    while (blockPeak > previousPeak
           && !peakLevel.compare_exchange_weak(previousPeak, blockPeak,
                                               std::memory_order_relaxed,
                                               std::memory_order_relaxed))
    {
    }
}
}
