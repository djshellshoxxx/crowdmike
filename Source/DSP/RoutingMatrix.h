#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace crowdmike {
// Fixed-capacity, allocation-free channel matrix. Audio-thread ownership applies to processing changes.
class RoutingMatrix final {
public:
    static constexpr int maxChannels = 16;
    static constexpr int routeCount = maxChannels * maxChannels;

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
        for (size_t route = 0; route < gains.size(); ++route)
            smoothedGains[route].setCurrentAndTargetValue(gains[route]);
    }

    void prepare(double sampleRate) noexcept {
        for (size_t route = 0; route < gains.size(); ++route) {
            smoothedGains[route].reset(sampleRate, 0.01);
            smoothedGains[route].setCurrentAndTargetValue(gains[route]);
        }
    }

    float getRouteGain(int input, int output) const noexcept {
        if (!isValidRoute(input, output))
            return 0.0f;
        return gains[routeIndex(input, output)];
    }

    bool setRouteGain(int input, int output, float linearGain) noexcept {
        if (!isValidGain(input, output, linearGain))
            return false;
        const auto index = routeIndex(input, output);
        gains[index] = juce::jlimit(-2.0f, 2.0f, linearGain);
        smoothedGains[index].setCurrentAndTargetValue(gains[index]);
        return true;
    }

    bool setTargetRouteGain(int input, int output, float linearGain) noexcept {
        if (!isValidGain(input, output, linearGain))
            return false;
        const auto index = routeIndex(input, output);
        gains[index] = juce::jlimit(-2.0f, 2.0f, linearGain);
        smoothedGains[index].setTargetValue(gains[index]);
        return true;
    }

    // Source and destination must not alias; all output channels are overwritten.
    void process(const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& destination) noexcept {
        const int samples = juce::jmin(source.getNumSamples(), destination.getNumSamples());
        const int ins = juce::jmin(inputCount, source.getNumChannels());
        const int outs = juce::jmin(outputCount, destination.getNumChannels());
        destination.clear();
        for (int out = 0; out < outs; ++out) {
            float* dest = destination.getWritePointer(out);
            for (int in = 0; in < ins; ++in) {
                const auto index = routeIndex(in, out);
                auto& gain = smoothedGains[index];
                const float* src = source.getReadPointer(in);
                if (gain.isSmoothing()) {
                    for (int sample = 0; sample < samples; ++sample)
                        dest[sample] += src[sample] * gain.getNextValue();
                } else {
                    const float currentGain = gain.getCurrentValue();
                    if (currentGain != 0.0f)
                        juce::FloatVectorOperations::addWithMultiply(dest, src, currentGain, samples);
                }
            }
        }
    }

private:
    static constexpr size_t routeIndex(int input, int output) noexcept {
        return static_cast<size_t>(output * maxChannels + input);
    }
    bool isValidRoute(int input, int output) const noexcept {
        return input >= 0 && input < inputCount && output >= 0 && output < outputCount;
    }
    bool isValidGain(int input, int output, float gain) const noexcept {
        return isValidRoute(input, output) && std::isfinite(gain);
    }

    std::array<float, routeCount> gains {};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, routeCount> smoothedGains {};
    int inputCount = 0;
    int outputCount = 0;
};
}
