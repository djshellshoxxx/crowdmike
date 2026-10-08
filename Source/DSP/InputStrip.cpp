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
    muteGain.reset(sampleRate, 0.01);
    muteGain.setCurrentAndTargetValue(muted.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
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
    gain = juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 24.0f, db));
}

void InputStrip::setHighPassHz(float hz)
{
    highPass.setCutoffFrequency(juce::jlimit(20.0f, 500.0f, hz));
    highPassEnabled = true;
}

void InputStrip::setLowPassHz(float hz)
{
    lowPass.setCutoffFrequency(juce::jlimit(2000.0f, 20000.0f, hz));
    lowPassEnabled = true;
}

void InputStrip::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const float muteTarget = muted.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
    if (muteGain.getTargetValue() != muteTarget)
        muteGain.setTargetValue(muteTarget);

    buffer.applyGain(gain * polarity);
    const int samples = buffer.getNumSamples();
    for (int sample = 0; sample < samples; ++sample)
    {
        const float currentMuteGain = muteGain.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.getWritePointer(channel)[sample] *= currentMuteGain;
    }
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    if (highPassEnabled) highPass.process(context);
    if (lowPassEnabled) lowPass.process(context);

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
