#include "SafetyLimiter.h"

namespace crowdmike
{
void SafetyLimiter::prepare(double sampleRate, int maximumBlockSize, int channels)
{
    limiter.prepare({ sampleRate, static_cast<juce::uint32>(maximumBlockSize),
                      static_cast<juce::uint32>(channels) });
    limiter.setThreshold(-0.5f);
    limiter.setRelease(100.0f);
}

void SafetyLimiter::process(juce::AudioBuffer<float>& buffer) noexcept
{
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    limiter.process(context);
}
}
