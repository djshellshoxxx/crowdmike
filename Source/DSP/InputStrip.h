#pragma once
#include <JuceHeader.h>

namespace crowdmike
{
class InputStrip
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channels);
    void reset();
    void setTrimDb(float db) noexcept;
    void setPolarityInverted(bool inverted) noexcept { polarity = inverted ? -1.0f : 1.0f; }
    void setHighPassHz(float hz);
    void setLowPassHz(float hz);
    void process(juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::StateVariableTPTFilter<float> highPass, lowPass;
    float gain = 1.0f, polarity = 1.0f;
    bool highPassEnabled = false, lowPassEnabled = false;
};
}
