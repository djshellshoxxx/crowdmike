#pragma once
// Shared helpers for the recording / playback / soundcheck tests.
#include "TestHarness.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>

namespace rectest {

/** Unique temp folder, removed (recursively) on destruction. */
struct TempDir {
    juce::File dir;
    explicit TempDir(const juce::String& tag = "rec") {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("crowdmike_test_" + tag + "_" + juce::Uuid().toString().substring(0, 12));
        dir.createDirectory();
    }
    ~TempDir() {
        if (dir.exists()) { dir.setReadOnly(false, true); dir.deleteRecursively(); }
    }
};

/** Deterministic test signal in [-1, 1): distinct per track/channel, exact in float. */
inline float ramp(int track, std::int64_t frame, int ch) {
    const auto v = (frame * 7 + track * 1013 + ch * 509) % 65536;
    return (float) v / 32768.0f - 1.0f;
}

inline bool readWav(const juce::File& f, juce::AudioBuffer<float>& out, double* sr = nullptr) {
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) return false;
    out.setSize((int) r->numChannels, (int) r->lengthInSamples);
    r->read(&out, 0, (int) r->lengthInSamples, 0, true, true);
    if (sr != nullptr) *sr = r->sampleRate;
    return true;
}

inline bool writeWav(const juce::File& f, const juce::AudioBuffer<float>& b, double sr, int bits = 32) {
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(f);
    using SF = juce::AudioFormatWriterOptions::SampleFormat;
    auto w = juce::WavAudioFormat().createWriterFor(os, juce::AudioFormatWriterOptions {}
                                                            .withSampleRate(sr)
                                                            .withNumChannels(b.getNumChannels())
                                                            .withBitsPerSample(bits)
                                                            .withSampleFormat(bits == 32 ? SF::floatingPoint : SF::integral));
    if (w == nullptr) return false;
    return w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

inline double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace rectest
