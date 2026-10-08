#pragma once
#include <JuceHeader.h>
#include <array>

namespace crowdmike {
// Fixed-capacity, allocation-free channel matrix. Configure off the audio thread.
class RoutingMatrix final {
public:
    static constexpr int maxChannels = 16;
    void resetToIdentity(int inputChannels, int outputChannels) noexcept {
        inputCount = juce::jlimit(0, maxChannels, inputChannels);
        outputCount = juce::jlimit(0, maxChannels, outputChannels);
        gains.fill(0.0f);
        for (int out = 0; out < outputCount; ++out) {
            const int in = inputCount == 1 ? 0 : out;
            if (in < inputCount) gains[static_cast<size_t>(out * maxChannels + in)] = 1.0f;
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
