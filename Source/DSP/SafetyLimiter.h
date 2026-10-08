#pragma once
#include <juce_dsp/juce_dsp.h>

namespace crowdmike
{
class SafetyLimiter
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channels);
    void reset() { limiter.reset(); }
    void setCeilingDb(float db) { limiter.setThreshold(juce::jlimit(-12.0f, 0.0f, db)); }
    void process(juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::Limiter<float> limiter;
};
}
