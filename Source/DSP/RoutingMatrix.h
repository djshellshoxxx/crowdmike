#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace crowdmike {
// Fixed-capacity, allocation-free channel matrix. Configure off the audio thread.
class RoutingMatrix final {
public:
    static constexpr int maxChannels = 16;
    void resetToIdentity(int inputChannels, int outputChannels) noexcept {
        inputCount = juce::jlimit(0, maxChannels, inputChannels);
        outputCount = juce::jlimit(0, maxChannels, outputChannels);
        gains.fill(0.0f);
        if (inputCount == 1) {
            for (int out = 0; out < outputCount; ++out)
                gains[static_cast<size_t>(out * maxChannels)] = 1.0f;
        } else if (inputCount <= outputCount) {
            for (int channel = 0; channel < inputCount; ++channel)
                gains[static_cast<size_t>(channel * maxChannels + channel)] = 1.0f;
        } else {
            // Fold extra inputs into the available outputs with per-output averaging.
            for (int out = 0; out < outputCount; ++out) {
                const int sourceCount = (inputCount - 1 - out) / outputCount + 1;
                const float gain = 1.0f / static_cast<float>(sourceCount);
                for (int in = out; in < inputCount; in += outputCount)
                    gains[static_cast<size_t>(out * maxChannels + in)] = gain;
            }
        }
    }
    bool setRouteGain(int input, int output, float linearGain) noexcept {
        if (input < 0 || input >= inputCount || output < 0 || output >= outputCount
            || !std::isfinite(linearGain)) return false;
        gains[static_cast<size_t>(output * maxChannels + input)] =
            juce::jlimit(-2.0f, 2.0f, linearGain);
        return true;
    }
    // Source and destination must not alias; all output channels are overwritten.
    void process(const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& destination) const noexcept {
        const int samples = juce::jmin(source.getNumSamples(), destination.getNumSamples());
        const int ins = juce::jmin(inputCount, source.getNumChannels());
        const int outs = juce::jmin(outputCount, destination.getNumChannels());
        destination.clear();
        for (int out = 0; out < outs; ++out) {
            float* dest = destination.getWritePointer(out);
            for (int in = 0; in < ins; ++in) {
                const float gain = gains[static_cast<size_t>(out * maxChannels + in)];
                if (gain == 0.0f) continue;
                const float* src = source.getReadPointer(in);
                juce::FloatVectorOperations::addWithMultiply(dest, src, gain, samples);
            }
        }
    }
private:
    std::array<float, maxChannels * maxChannels> gains {};
    int inputCount = 0;
    int outputCount = 0;
};
}
