#pragma once
#include <JuceHeader.h>
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
    // Returns the largest post-processing sample magnitude since the previous read.
    float getAndResetPeak() noexcept { return peakLevel.exchange(0.0f, std::memory_order_relaxed); }
    void setHighPassHz(float hz);
    void setLowPassHz(float hz);
    void process(juce::AudioBuffer<float>& buffer) noexcept;

private:
    juce::dsp::StateVariableTPTFilter<float> highPass, lowPass;
    std::atomic<float> peakLevel { 0.0f };
    std::atomic<bool> muted { false };
    float gain = 1.0f, polarity = 1.0f;
    bool highPassEnabled = false, lowPassEnabled = false;
};
}
