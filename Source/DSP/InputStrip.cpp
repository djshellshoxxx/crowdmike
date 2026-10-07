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
    reset();
}

void InputStrip::reset()
{
    highPass.reset();
    lowPass.reset();
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
    buffer.applyGain(gain * polarity);
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    if (highPassEnabled) highPass.process(context);
    if (lowPassEnabled) lowPass.process(context);
}
}
