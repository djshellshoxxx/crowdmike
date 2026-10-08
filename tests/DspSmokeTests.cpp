#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/InputStrip.h"
#include "DSP/SafetyLimiter.h"
#include "DSP/RoutingMatrix.h"
#include "PluginProcessor.h"
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
    strip.setHighPassEnabled(true);
    strip.setLowPassEnabled(true);
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
void testStereoMuteRampStaysSynchronized() {
    crowdmike::InputStrip strip;
    strip.prepare(48000.0, 256, 2);
    strip.setMuted(true);
    juce::AudioBuffer<float> b(2, 256);
    fill(b, 0.8f);
    strip.process(b);
    bool channelsMatch = true;
    for (int sample = 0; sample < b.getNumSamples(); ++sample)
        channelsMatch = channelsMatch
            && std::abs(b.getSample(0, sample) - b.getSample(1, sample)) < 0.000001f;
    expect(channelsMatch, "stereo mute ramp uses the same gain on both channels");
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
void testHostMuteParameterAffectsAudio() {
    CrowdMikeAudioProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    auto* mute = processor.getParameters().getParameter("input1.mute");
    expect(mute != nullptr, "input mute parameter exists");
    if (mute == nullptr)
        return;
    mute->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 5; ++block) {
        fill(audio, 0.1f);
        processor.processBlock(audio, midi);
    }
    expect(audio.getMagnitude(0, 256) == 0.0f, "host mute parameter silences its input");
    expect(audio.getMagnitude(1, 256) > 0.05f, "host mute parameter leaves other input active");
}
void testHostParametersAndStateRoundTrip() {
    CrowdMikeAudioProcessor source;
    auto& sourceParams = source.getParameters();
    auto* trim = sourceParams.getParameter("input1.trimDb");
    auto* mute = sourceParams.getParameter("input1.mute");
    auto* ceiling = sourceParams.getParameter("limiterCeilingDb");
    expect(trim != nullptr && mute != nullptr && ceiling != nullptr,
           "stable host parameters are registered");
    if (trim == nullptr || mute == nullptr || ceiling == nullptr)
        return;

    trim->setValueNotifyingHost(trim->convertTo0to1(7.5f));
    mute->setValueNotifyingHost(1.0f);
    ceiling->setValueNotifyingHost(ceiling->convertTo0to1(-1.2f));

    juce::MemoryBlock state;
    source.getStateInformation(state);
    const auto stateXml = juce::AudioProcessor::getXmlFromBinary(
        state.getData(), static_cast<int>(state.getSize()));
    expect(stateXml != nullptr && stateXml->getIntAttribute("schemaVersion", -1) == 1,
           "state includes a schema version");
    CrowdMikeAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    const auto* restoredTrim = restored.getParameters().getRawParameterValue("input1.trimDb");
    const auto* restoredMute = restored.getParameters().getRawParameterValue("input1.mute");
    const auto* restoredCeiling = restored.getParameters().getRawParameterValue("limiterCeilingDb");
    expect(restoredTrim != nullptr && std::abs(restoredTrim->load() - 7.5f) < 0.02f,
           "state restores input trim");
    expect(restoredMute != nullptr && restoredMute->load() > 0.5f,
           "state restores mute");
    expect(restoredCeiling != nullptr && std::abs(restoredCeiling->load() + 1.2f) < 0.06f,
           "state restores limiter ceiling");

    const float beforeInvalidRestore = restoredTrim != nullptr ? restoredTrim->load() : 0.0f;
    const char invalidState[] = "invalid";
    restored.setStateInformation(invalidState, static_cast<int>(sizeof(invalidState)));
    expect(restoredTrim != nullptr && std::abs(restoredTrim->load() - beforeInvalidRestore) < 0.001f,
           "invalid state is ignored");
}
}
int main() {
    testTrimAndPolarity();
    testFilters();
    testMuteAndPeakMeter();
    testStereoMuteRampStaysSynchronized();
    testLimiter();
    testRouting();
    testHostMuteParameterAffectsAudio();
    testHostParametersAndStateRoundTrip();
    if (failures) { std::cerr << failures << " failures\n"; return 1; }
    std::cout << "CrowdMike DSP smoke tests passed\n";
    return 0;
}
