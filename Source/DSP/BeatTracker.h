#pragma once
// BeatTracker -- onset-envelope tempo and beat-phase estimator for the "Auto" tempo source (docs/BEAT_SYNC_SPEC.md).
//
// Threading / invariants:
//  * pushSamples() is the ONLY audio-thread call: noexcept, lock-free, allocation-free, O(n) with ~10 flops per sample.
//    It turns the mono input into one onset-strength frame every `hop` samples (~4 ms) and writes it to an SPSC FIFO
//    (>= 16 s). When analyse() falls behind the oldest frames are dropped (never blocks) and the analysis restarts.
//  * analyse() runs on ONE worker/message thread at a time (serialised by the caller; AnalysisThread or tests). It drains
//    the FIFO, keeps an 8 s onset history and every 0.5 s runs the tempo estimate (autocorrelation + 4-harmonic comb,
//    octave resolution, hysteresis) and the beat-phase comb. Cost ~2 M multiply-adds per estimate.
//  * getEstimate() is lock-free (seqlock) and callable from any thread including the audio thread.
//  * reset() may be called from any non-audio thread: it only bumps a generation counter that both sides observe.
//  * Fully deterministic: identical input samples give bit-identical estimates regardless of block sizes or of how often
//    analyse() runs (frames, not blocks, drive the analysis).
//  * Outputs are always finite; bpm is within [kMinBpm, kMaxBpm] whenever valid; a silent/aperiodic input never changes a
//    locked tempo (it is held) but drops the confidence.
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace crowdmike::dsp {

struct BeatEstimate {
    bool valid = false;            // a tempo has been acquired (held afterwards, even when the signal goes away)
    double bpm = 0.0;              // locked tempo, kMinBpm..kMaxBpm when valid
    float confidence = 0.0f;       // 0..1, live (decays when the input is silent / aperiodic)
    double periodSamples = 0.0;    // 60 * sampleRate / bpm
    std::int64_t lastBeatSample = 0;   // position (in pushed-sample units) of a detected beat
    std::int64_t nowSample = 0;        // pushed-sample count the estimate was made at (analysis time)
};

class BeatTracker {
public:
    static constexpr double kMinBpm = 60.0;
    static constexpr double kMaxBpm = 200.0;

    BeatTracker() = default;
    BeatTracker(const BeatTracker&) = delete;
    BeatTracker& operator=(const BeatTracker&) = delete;

    void prepare(double sampleRate);                      // allocates; also resets
    void reset() noexcept;                                // any non-audio thread
    void pushSamples(const float* mono, int n) noexcept;  // audio thread
    void analyse();                                       // worker / message thread
    BeatEstimate getEstimate() const noexcept;            // any thread, lock-free
    // Samples consumed by pushSamples() since the last reset (audio-thread clock the estimate refers to).
    std::int64_t getPushedSamples() const noexcept { return pushed.load(std::memory_order_acquire); }
    double getSampleRate() const noexcept { return sr; }
    int getHop() const noexcept { return hop; }
    double getFrameRate() const noexcept { return frameRate; }
    // Test hook: number of analysis steps that produced an estimate.
    int getNumEstimates() const noexcept { return numEstimates.load(std::memory_order_relaxed); }

private:
    struct Features {   // audio-thread state
        float lp1 = 0.0f, lp2 = 0.0f;
        double e0 = 0.0, e1 = 0.0, e2 = 0.0;
        int count = 0;
        float prev[3] = { 0.0f, 0.0f, 0.0f };
        bool havePrev = false;
    };

    void analysisReset() noexcept;
    void onFrame(float flux, float level) noexcept;
    void estimate();
    void publish(bool valid, double bpm, float conf, std::int64_t lastBeat, std::int64_t nowSample) noexcept;

    double sr = 48000.0, frameRate = 250.0;
    int hop = 192;
    float a1 = 0.0f, a2 = 0.0f;   // band-split one-pole coefficients
    std::int64_t localPushed = 0;  // audio-thread copy of `pushed`
    Features feat;
    std::uint32_t seenGenAudio = 0, seenGenWorker = 0;
    std::atomic<std::uint32_t> generation { 0 };
    std::atomic<std::int64_t> pushed { 0 };

    // frame FIFO: 2 floats per frame (flux, level)
    static constexpr int kFifoFrames = 4096;
    std::unique_ptr<std::atomic<float>[]> fifo;
    std::atomic<std::uint64_t> writeFrames { 0 };
    std::uint64_t readFrames = 0;
    std::uint64_t absFrame = 0;   // analysis-side count of frames consumed since reset

    // analysis state (worker thread only)
    static constexpr int kHistory = 2560;       // frames kept (> 10 s at 250 fps)
    std::vector<float> envRing, levelRing;      // size kHistory
    std::vector<float> win, work, ac, smoothed; // scratch
    std::vector<double> prefix;
    std::vector<float> scoreGrid;
    int sinceEstimate = 0;
    bool have = false;
    double lockedBpm = 0.0, candBpm = 0.0;
    int candCount = 0;
    float confSmooth = 0.0f;
    std::int64_t lastBeat = 0;

    // published (seqlock)
    mutable std::atomic<std::uint32_t> seq { 0 };
    std::atomic<bool> pubValid { false };
    std::atomic<double> pubBpm { 0.0 };
    std::atomic<float> pubConf { 0.0f };
    std::atomic<std::int64_t> pubLastBeat { 0 }, pubNow { 0 };
    std::atomic<int> numEstimates { 0 };
};
}
