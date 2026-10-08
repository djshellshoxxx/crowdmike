#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace crowdmike
{
class InputStrip
{
public:
    void prepare(double sampleRate, int maximumBlockSize, int channels);
    void reset();
    void setTrimDb(float db) noexcept;
    void setPolarityInverted(bool inverted) noexcept { polarity = inverted ? -1.0f : 1.0f; }
    void setMuted(bool shouldMute) noexcept { muted.store(shouldMute, std::memory_order_relaxed); }
    bool isMuted() const noexcept { return muted.load(std::memory_order_relaxed); }
    void setHighPassEnabled(bool enabled) noexcept { highPassEnabled = enabled; }
    void setLowPassEnabled(bool enabled) noexcept { lowPassEnabled = enabled; }
    // Returns the largest post-processing sample magnitude since the previous read.
    float getAndResetPeak() noexcept { return peakLevel.exchange(0.0f, std::memory_order_relaxed); }
    void setHighPassHz(float hz);
    void setLowPassHz(float hz);
    void process(juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::StateVariableTPTFilter<float> highPass, lowPass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> muteGain, trimGain, highPassCutoff, lowPassCutoff;
    std::atomic<float> peakLevel { 0.0f };
    std::atomic<bool> muted { false };
    float targetGain = 1.0f, polarity = 1.0f;
    float highPassHz = 80.0f, lowPassHz = 18000.0f;
    bool highPassEnabled = false, lowPassEnabled = false;
};
}
