#include <JuceHeader.h>
#include "DSP/InputStrip.h"
#include "DSP/SafetyLimiter.h"
#include <cmath>
#include <iostream>

namespace {
int failures = 0;
void expect(bool ok, const char* name) {
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
void fill(juce::AudioBuffer<float>& b, float value) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int n = 0; n < b.getNumSamples(); ++n)
            b.setSample(ch, n, value);
}
void testTrimAndPolarity() {
    crowdmike::InputStrip strip;
    strip.prepare(48000.0, 256, 1);
    juce::AudioBuffer<float> b(1, 256);
    fill(b, 0.25f);
    strip.setTrimDb(6.0206f);
    strip.setPolarityInverted(true);
    strip.process(b);
    expect(std::abs(b.getSample(0, 20) + 0.5f) < 0.002f, "trim and polarity");
    strip.reset();
    strip.setPolarityInverted(false);
    fill(b, 0.25f);
    strip.process(b);
    expect(std::abs(b.getSample(0, 20) - 0.5f) < 0.002f, "polarity restore");
}
void testFilters() {
    crowdmike::InputStrip strip;
    strip.prepare(48000.0, 256, 1);
    strip.setHighPassHz(200.0f);
    strip.setLowPassHz(4000.0f);
    juce::AudioBuffer<float> b(1, 256);
    fill(b, 0.1f);
    for (int i = 0; i < 12; ++i) strip.process(b);
    expect(std::abs(b.getSample(0, 255)) < 0.02f, "HPF rejects DC");
    for (int i = 0; i < b.getNumSamples(); ++i)
        expect(std::isfinite(b.getSample(0, i)), "filter finite");
}
void testLimiter() {
    crowdmike::SafetyLimiter limiter;
    limiter.prepare(48000.0, 256, 2);
    juce::AudioBuffer<float> b(2, 256);
    for (int i = 0; i < 24; ++i) {
        fill(b, 4.0f);
        limiter.process(b);
    }
    expect(b.getMagnitude(0, 256) <= 1.01f, "limiter caps sustained loud signal");
}
}
int main() {
    testTrimAndPolarity();
    testFilters();
    testLimiter();
    if (failures) { std::cerr << failures << " failures\n"; return 1; }
    std::cout << "CrowdMike DSP smoke tests passed\n";
    return 0;
}
