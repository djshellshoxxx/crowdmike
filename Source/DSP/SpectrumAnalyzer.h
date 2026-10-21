#pragma once
// SpectrumAnalyzer -- generic display spectrum for any mono tap (GUI_SPEC sections 4 and 11).
//
// Threading / invariants:
//  * pushSamples() is the audio-thread call: noexcept, lock-free, allocation-free (SPSC FIFO of ~0.5 s that drops the
//    oldest samples on overflow).
//  * analyse() is meant for a GUI timer (or analysis worker); it only processes the most recent frames (a backlog
//    older than kMaxFramesPerAnalyse hops is skipped). getSpectrumDb()/getPeakFrequencyHz() take a short mutex shared
//    only with analyse(), never with the audio thread.
//  * 2048-point FFT, periodic Hann window, 50 % overlap. Levels in dBFS (a full-scale sine reads 0 dBFS), smoothed:
//    instant attack, ~250 ms exponential release (in dB).
#include <juce_dsp/juce_dsp.h>
#include "SpectralFifo.h"
#include <memory>
#include <mutex>
#include <vector>

namespace crowdmike::dsp {

class SpectrumAnalyzer {
public:
    static constexpr int kFftOrder = 11;
    static constexpr int kFftSize = 1 << kFftOrder; // 2048
    static constexpr int kHop = kFftSize / 2;
    static constexpr int kMaxFramesPerAnalyse = 8;
    static constexpr float kFloorDb = -120.0f;

    SpectrumAnalyzer() = default;
    SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
    SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;

    void prepare(double sampleRate, float releaseMs = 250.0f); // allocates; also resets
    void pushSamples(const float* mono, int n) noexcept;      // audio thread
    void analyse();                                            // GUI timer / worker thread
    // numBins log-spaced display bins between minHz and maxHz (clamped to Nyquist), dBFS >= kFloorDb.
    void getSpectrumDb(float* out, int numBins, float minHz = 20.0f, float maxHz = 20000.0f) const;
    // Frequency of the strongest smoothed bin (>= 20 Hz), parabolic-interpolated; 0 if nothing above the floor.
    float getPeakFrequencyHz() const;
    void reset();

    double getSampleRate() const noexcept { return sampleRate; }
    int getNumFftBins() const noexcept { return kFftSize / 2 + 1; }

private:
    void processFrame();

    double sampleRate = 48000.0;
    float releaseCoefDb = 0.0f;
    detail::SampleFifo fifo;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, accum, fftBuf, drainBuf, workDb;
    float ampScale = 1.0f;
    int accumFill = 0;
    std::mutex workMutex;
    mutable std::mutex snapMutex;
    std::vector<float> snapDb;
};

} // namespace crowdmike::dsp
