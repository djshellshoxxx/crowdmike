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

    /** 4-point, 3rd-order Lagrange interpolation between x0 and x1 at fraction f in [0,1). */
    static float lagrange4(double xm1, double x0, double x1, double x2, double f) noexcept {
        const double c0 = -f * (f - 1.0) * (f - 2.0) / 6.0;
        const double c1 = (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
        const double c2 = -(f + 1.0) * f * (f - 2.0) / 2.0;
        const double c3 = (f + 1.0) * f * (f - 1.0) / 6.0;
        return (float) (c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2);
    }
    /** In-place zero-phase anti-alias low-pass used before down-sampling inRate -> outRate (outRate < inRate). */
    static void antiAlias(float* x, int n, double inRate, double outRate);

    /** Offline resampler: 4-point cubic Lagrange interpolation with a zero-phase anti-alias low-pass when
        down-sampling. Output length = round(inFrames * outRate / inRate). Exact copy when the rates match. */
    static void resample(const juce::AudioBuffer<float>& in, double inRate, double outRate,
                         juce::AudioBuffer<float>& out);
};

} // namespace crowdmike::rec
