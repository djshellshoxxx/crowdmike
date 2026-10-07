#include <JuceHeader.h>
#include "DSP/InputStrip.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char* message)
{
    if (! condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main()
{
    crowdmike::InputStrip strip;
    strip.prepare(48000.0, 64, 1);

    juce::AudioBuffer<float> buffer(1, 64);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);

    strip.setTrimDb(-6.0f);
    strip.setPolarityInverted(true);
    strip.process(buffer);

    const auto expected = -juce::Decibels::decibelsToGain(-6.0f);
    require(std::abs(buffer.getSample(0, 0) - expected) < 0.02f,
            "trim + polarity should produce the expected signed gain");

    juce::AudioBuffer<float> dc(1, 2048);
    for (int i = 0; i < dc.getNumSamples(); ++i) dc.setSample(0, i, 1.0f);
    strip.reset();
    strip.setPolarityInverted(false);
    strip.setTrimDb(0.0f);
    strip.setHighPassHz(120.0f);
    strip.process(dc);
    require(std::abs(dc.getSample(0, dc.getNumSamples() - 1)) < 0.1f,
            "high-pass filter should strongly attenuate DC");

    std::cout << "CrowdMike DSP smoke tests passed\n";
}
