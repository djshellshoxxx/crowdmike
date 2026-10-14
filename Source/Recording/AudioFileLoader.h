#pragma once
// Off-audio-thread audio file decoding + sample-rate conversion shared by PlaybackEngine and SoundcheckPlayer.
// NEVER call from the audio thread (allocates, does file I/O).
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace crowdmike::rec {

class AudioFileLoader {
public:
    struct Result {
        juce::AudioBuffer<float> audio;   // planar, at targetSampleRate
        double sourceSampleRate = 0.0;
        int sourceChannels = 0;
        juce::int64 sourceFrames = 0;
    };

    /** Decodes `file` (any juce basic format: WAV/AIFF/FLAC/Ogg...) into memory, keeps at most `maxChannels`
        channels, and converts it to `targetSampleRate`. Fails (returns false + error text) for missing, unreadable,
        corrupt or unsupported files, files longer than `maxSeconds` (0 = no limit), files whose decoded size exceeds
        `maxBytes` (0 = no limit) or when `cancel` becomes true. */
    static bool load(const juce::File& file, double targetSampleRate, int maxChannels, double maxSeconds,
                     juce::int64 maxBytes, Result& out, juce::String& error,
                     const std::atomic<bool>* cancel = nullptr);

    /** Offline resampler: 4-point cubic Lagrange interpolation with a zero-phase anti-alias low-pass when
        down-sampling. Output length = round(inFrames * outRate / inRate). Exact copy when the rates match. */
    static void resample(const juce::AudioBuffer<float>& in, double inRate, double outRate,
                         juce::AudioBuffer<float>& out);
};

} // namespace crowdmike::rec
