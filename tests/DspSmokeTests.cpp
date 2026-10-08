#include <JuceHeader.h>
#include "DSP/InputStrip.h"
#include "DSP/SafetyLimiter.h"
#include "DSP/RoutingMatrix.h"
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
void testMuteAndPeakMeter() {
    crowdmike::InputStrip strip;
    strip.prepare(48000.0, 256, 1);
    juce::AudioBuffer<float> b(1, 256);

    fill(b, 0.4f);
    strip.process(b);
    expect(std::abs(strip.getAndResetPeak() - 0.4f) < 0.0001f,
           "peak meter returns block peak");
    expect(strip.getAndResetPeak() == 0.0f, "peak meter resets after read");

    fill(b, 0.7f);
    strip.process(b);
    fill(b, 0.2f);
    strip.process(b);
    expect(std::abs(strip.getAndResetPeak() - 0.7f) < 0.0001f,
           "peak meter holds maximum until read");

    strip.getAndResetPeak();
    strip.setMuted(true);
    expect(strip.isMuted(), "mute state is readable");
    fill(b, 0.8f);
    strip.process(b);
    expect(b.getMagnitude(0, b.getNumSamples()) > 0.0f, "mute ramps instead of cutting abruptly");
    for (int i = 0; i < 4; ++i) {
        fill(b, 0.8f);
        strip.process(b);
    }
    expect(b.getMagnitude(0, b.getNumSamples()) == 0.0f, "mute reaches silence after ramp");
    strip.getAndResetPeak();
    fill(b, 0.8f);
    strip.process(b);
    expect(strip.getAndResetPeak() == 0.0f, "muted signal does not reach peak meter");
}
void testRouting() {
    crowdmike::RoutingMatrix matrix;
    matrix.resetToIdentity(1, 2);
    juce::AudioBuffer<float> input(1, 32), output(2, 32);
    fill(input, 0.25f);
    fill(output, 8.0f);
    matrix.process(input, output);
    expect(std::abs(output.getSample(0, 0) - 0.25f) < 0.0001f, "mono to left");
    expect(std::abs(output.getSample(1, 0) - 0.25f) < 0.0001f, "mono to right");
    expect(!matrix.setRouteGain(8, 0, 1.0f), "reject invalid input route");
    expect(!matrix.setRouteGain(0, 8, 1.0f), "reject invalid output route");
    matrix.resetToIdentity(2, 2);
    juce::AudioBuffer<float> stereo(2, 32);
    fill(stereo, 0.0f);
    stereo.setSample(0, 0, 1.0f);
    stereo.setSample(1, 0, 0.5f);
    expect(matrix.setRouteGain(0, 1, 0.25f), "accept cross-route");
    matrix.process(stereo, output);
    expect(std::abs(output.getSample(0, 0) - 1.0f) < 0.0001f, "left direct");
    expect(std::abs(output.getSample(1, 0) - 0.75f) < 0.0001f, "right plus cross-feed");
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
    testMuteAndPeakMeter();
    testLimiter();
    testRouting();
    if (failures) { std::cerr << failures << " failures\n"; return 1; }
    std::cout << "CrowdMike DSP smoke tests passed\n";
    return 0;
}
