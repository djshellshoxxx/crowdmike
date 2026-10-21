#pragma once
// StreamSource: random-access, engine-rate view of an audio file for the streaming playback path
// (RECORDING_SPEC 9.1). WORKER THREADS ONLY (opens files, reads from disk, allocates scratch). Not thread-safe:
// every thread that needs one opens its own instance.
//
// Output contract: read(start, n, L, R) yields engine-rate frames [start, start + n) of the file, always 2 output
// channels (a mono file is duplicated), samples outside the file are silence and non-finite samples are 0. When the
// file rate differs from the engine rate the data is resampled with the same 4-point Lagrange interpolator and
// zero-phase anti-alias filter as AudioFileLoader::resample(), evaluated per request with a guard margin, so the
// result matches the offline resampler to ~1e-6 away from the file edges.
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace crowdmike::rec {

class StreamSource {
public:
    struct Info {
        double sourceRate = 0.0, engineRate = 0.0;
        int sourceChannels = 0;          // channels in the file
        int channels = 1;                // 1 (mono file) or 2 (output is always L/R)
        juce::int64 sourceFrames = 0;
        juce::int64 frames = 0;          // length at the engine rate = llround(sourceFrames * engineRate / sourceRate)
    };

    /** Opens `file`; false + error text for missing / unreadable / unsupported / empty files. */
    bool open(const juce::File& file, double engineRate, juce::String& error);
    bool isOpen() const noexcept { return reader != nullptr; }
    const Info& info() const noexcept { return inf; }

    /** Reads engine-rate frames [start, start + n) into L and R. Returns false on a read error (output is then
        silence for the failed part). */
    bool read(juce::int64 start, int n, float* L, float* R);

    /** Plays the mapping of a playback head: frames `seq .. seq + n` of the sequence
        region [regionStart, regionStart + regionLen) read forwards or backwards, wrapping when `loop`.
        Frames at/after regionLen are not produced when !loop (returns the number produced). Shared by the head
        builder (loader thread) and the ring refill (streamer thread). */
    int readSequence(juce::int64 regionStart, juce::int64 regionLen, bool reverse, bool loop, juce::uint64 seq,
                     int n, float* L, float* R);

private:
    bool readRaw(juce::int64 start, int n, float* const* dst, int numDst);
    juce::AudioFormatManager fm;
    std::unique_ptr<juce::AudioFormatReader> reader;
    Info inf;
    std::vector<float> raw[2];
public:
    juce::uint64 failedReads = 0;   // read() calls that hit a decode error (worker-thread counter)
};

} // namespace crowdmike::rec
