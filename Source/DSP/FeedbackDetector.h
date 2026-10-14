#pragma once
// FeedbackDetector -- advisory acoustic-feedback (howl) candidate finder (ENGINE_SPEC section 8, GUI_SPEC section 11).
//
// Threading / invariants:
//  * pushSamples() is the ONLY audio-thread call: noexcept, lock-free, allocation-free. It writes into an SPSC FIFO
//    holding >= 1 s; when analyse() falls behind the oldest samples are dropped (never blocks).
//  * analyse() runs on one worker/message thread at a time (serialised by an internal mutex). It drains the FIFO,
//    runs a 4096-point Hann FFT with 50 % overlap, and updates the peak tracks.
//  * getCandidates()/getSpectrumDb() copy a snapshot under a separate short-held mutex and may be called from the GUI
//    thread while analyse() runs elsewhere. reset() may be called from any non-audio thread.
//  * Results are independent of the sample rate (persistence/time constants are in seconds; 44.1..192 kHz). Above
//    50 kHz the worker decimates by 2 (or 4) with a linear-phase FIR before the FFT, so the 4096-point frame keeps
//    ~11 Hz resolution and ~90 ms length at every rate (analysis band up to 18 kHz).
//
// Algorithm (per analysis frame, hop 2048 samples):
//  1. magnitude spectrum in dBFS (a full-scale sine reads 0 dBFS), smoothed over frames (attack ~60 ms, release
//     ~20 ms so that sounds that stop vanish quickly);
//  2. narrow peaks: local maxima with prominence >= 12 dB (>= 9 dB to keep an existing track alive) above the
//     median of neighbouring bins (+-4..+-19 bins, i.e. excluding +-3 bins), -6 dB width <= 3.5 bins (broad
//     musical/noisy peaks are rejected), level above -90 dBFS, 40 Hz .. min(18 kHz, 0.45 fs);
//  3. frequency by parabolic interpolation of the log spectrum; tracks match peaks within +-1 bin; a track that
//     drifts more than clamp(2 % of f, 0.35, 1) bins from where it was anchored is re-anchored (persistence restarts) so glides and
//     vibrato never accumulate persistence; tracks unseen for > max(70 ms, 1.6 hops) are dropped (transients,
//     alternating notes); persistence = accumulated time the peak was actually detected; a level dip > 15 dB below the track's
//     maximum restarts persistence (a howl does not come and go);
//  4. severity 0..1 = 0.40 prominence + 0.30 persistence + 0.15 growth (rising level) + 0.15 absolute level;
//  5. candidates are reported once persistence >= 0.5 s, ranked by severity. Suggested notch: Q 10..30 (narrower
//     measured peaks => higher Q), depth -3 dB .. -18 dB scaled by severity.
#include <juce_dsp/juce_dsp.h>
#include "SpectralFifo.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace crowdmike::dsp {

struct FeedbackCandidate {
    float frequencyHz = 0.0f, prominenceDb = 0.0f, persistenceSec = 0.0f, severity = 0.0f /*0..1*/, suggestedQ = 0.0f,
          suggestedDepthDb = 0.0f;
};

class FeedbackDetector {
public:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder; // 4096
    static constexpr int kHop = kFftSize / 2;       // 50 % overlap
    static constexpr int kMaxCandidates = 16;
    static constexpr float kMinPersistenceSec = 0.5f;
    static constexpr float kFloorDb = -120.0f;

    FeedbackDetector() = default;
    FeedbackDetector(const FeedbackDetector&) = delete;
    FeedbackDetector& operator=(const FeedbackDetector&) = delete;

    void prepare(double sampleRate);                    // allocates; also resets
    void pushSamples(const float* mono, int n) noexcept; // audio thread
    void analyse();                                     // worker/message thread
    int getCandidates(FeedbackCandidate* out, int maxCount) const; // ranked by severity, persistence-gated
    // Display spectrum: numBins log-spaced bins 20 Hz..20 kHz, dBFS (>= kFloorDb).
    void getSpectrumDb(float* out, int numBins) const;
    void reset();

    double getSampleRate() const noexcept { return sampleRate; }
    // Rate the FFT runs at: the input rate, decimated by 2/4 above 50 kHz (88.2/96 kHz -> 44.1/48 kHz).
    double getAnalysisSampleRate() const noexcept { return analysisRate; }
    float getBinHz() const noexcept { return static_cast<float>(analysisRate / kFftSize); }
    // Samples discarded because analyse() fell more than the FIFO capacity behind.
    std::uint64_t getDroppedSamples() const noexcept { return fifo.getDroppedSamples(); }
    int getFifoCapacitySamples() const noexcept { return fifo.getUsableCapacity(); }

private:
    struct Peak { float bin, db, promDb, widthBins; bool used; };
    struct Track {
        float bin, anchorBin, levelDb, promDb, widthBins, growthDbPerSec;
        float seenSec;      // accumulated time the peak was actually detected (persistence)
        float maxLevelDb;   // loudest level since (re)anchoring
        double lastSeenSec;
        float severity;
    };

    int decimate(float* x, int n) noexcept;
    void processFrame();
    void findPeaks();
    void updateTracks();
    void publishSnapshot();
    void resetAnalysisState();
    float computeSeverity(const Track& t) const noexcept;

    double sampleRate = 48000.0, analysisRate = 48000.0;
    int decimation = 1, decimPhase = 0, decimPos = 0;
    std::vector<float> decimTaps, decimHist;
    float hopSec = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f, growthCoef = 0.0f;
    int numBins = kFftSize / 2 + 1, loBin = 4, hiBin = 100;

    detail::SampleFifo fifo;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, accum, fftBuf, smoothPow, specDb, drainBuf, medianScratch;
    float ampScale = 1.0f;
    int accumFill = 0;
    bool havePrevFrame = false;
    double nowSec = 0.0, maxMissSec = 0.07;
    std::vector<Peak> peaks;
    std::vector<Track> tracks;

    std::mutex workMutex;            // serialises analyse()/reset()
    mutable std::mutex snapMutex;    // guards the published snapshot only (never touched by the audio thread)
    std::array<FeedbackCandidate, kMaxCandidates> snapCandidates {};
    int snapCount = 0;
    std::vector<float> snapDb;

    static constexpr int kMaxTracks = 64;
    static constexpr int kMaxPeaks = 128;
};

} // namespace crowdmike::dsp
