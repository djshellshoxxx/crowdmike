#pragma once
// Line Check (docs/LINE_CHECK_SPEC.md): lock-free per-input probe (audio thread) + pure message-thread analyser.
// Standard-library only (no JUCE) so it can be tested with synthetic signals.
//  * ProbeBank::process() runs on the audio thread: disarmed it costs one relaxed atomic load; armed it accumulates fixed
//    100 ms frames per input (peak, sum, sum of squares, cross product with the next input, clip count, Goertzel bins at
//    50/60 Hz and harmonics) and pushes them into a per-input SPSC ring. No allocation, locks or IO.
//  * Analyser::analyse() turns the frames of one listening run into per-input verdicts, flags and proposals.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "Core/Limits.h"

namespace crowdmike::linecheck {
constexpr int kHumBins = 8;                                   // 50,100,150,200 | 60,120,180,240 Hz
constexpr int kHumFreqs[kHumBins] = { 50, 100, 150, 200, 60, 120, 180, 240 };
constexpr double kFrameSeconds = 0.1;
constexpr int kListenSeconds = 10;
constexpr int kFramesPerListen = 100;
constexpr int kRingFrames = 256;
constexpr int kMinFramesForEarlyStop = 20;

// Thresholds (LINE_CHECK_SPEC section 3).
constexpr float kDeadRmsDb = -75.0f, kDeadAcPeak = 0.001f;
constexpr float kClipLevel = 0.999f;
constexpr float kHotPeak = 0.7079458f;                        // -3 dBFS
constexpr float kHotRmsDb = -10.0f, kLowRmsDb = -34.0f;
constexpr float kDcThreshold = 0.01f;                         // -40 dBFS
constexpr float kHumRatio = 0.30f, kHumMinDb = -70.0f;
constexpr float kPolarityCorr = -0.5f, kPolarityMinRmsDb = -50.0f, kPolarityMaxLevelDiffDb = 12.0f;
constexpr float kDefaultTargetRmsDb = -20.0f, kTrimClampDb = 24.0f, kPeakCeilingDb = -1.0f, kTrimDeadbandDb = 0.5f;
constexpr float kHpfDefaultHz = 100.0f, kHpfHumHz = 150.0f;

struct Frame {
    int n = 0;
    float peak = 0.0f;
    double sum = 0.0, sumSq = 0.0, cross = 0.0;
    int clip = 0;
    float hum[kHumBins] {};          // squared amplitude A^2 of each bin
    std::uint32_t generation = 0;
};

class ProbeBank {
public:
    void prepare(double sampleRate, int numInputs);              // message thread; allocates; disarms and clears
    void start() noexcept;                                       // arm for a new listening run (message thread)
    void stop() noexcept { armed.store(false, std::memory_order_release); }
    bool isArmed() const noexcept { return armed.load(std::memory_order_relaxed); }
    std::uint32_t generation() const noexcept { return generationNow.load(std::memory_order_acquire); }
    int frameSamples() const noexcept { return frameLen; }
    std::uint64_t dropped() const noexcept { return droppedFrames.load(std::memory_order_relaxed); }

    // Audio thread. `in` holds `numIn` channels of `n` samples (raw host inputs).
    void process(const float* const* in, int numIn, int n) noexcept;
    // Consumer (message thread). Returns false when the input's ring is empty.
    bool pop(int input, Frame& out) noexcept;

private:
    struct Acc {
        float peak = 0.0f;
        double sum = 0.0, sumSq = 0.0, cross = 0.0;
        int clip = 0;
        double s1[kHumBins] {}, s2[kHumBins] {};
    };
    void resetAccumulators() noexcept;
    void finishFrame(int numIn) noexcept;
    bool push(int input, const Frame& f) noexcept;

    double sr = 48000.0;
    int frameLen = 4800, inputs = 0, frameCount = 0;
    double coef[kHumBins] {};
    std::array<Acc, kMaxInputs> acc {};
    std::uint32_t myGeneration = 0;
    std::atomic<bool> armed { false };
    std::atomic<std::uint32_t> generationNow { 0 };
    std::atomic<std::uint64_t> droppedFrames { 0 };
    std::vector<Frame> ring;                                     // [input][kRingFrames]
    std::array<std::atomic<std::uint32_t>, kMaxInputs> head {}, tail {};
};

enum class Verdict { Dead, Clipping, Hot, Low, Ok };
const char* verdictName(Verdict) noexcept;

struct Settings {
    float targetRmsDb = kDefaultTargetRmsDb;
    float trimClampDb = kTrimClampDb;
    float peakCeilingDb = kPeakCeilingDb;
};
struct InputContext {                                            // live state of the input at analysis time
    bool active = true;
    bool polarityInverted = false;
    float trimDb = 0.0f;
    bool hpOn = false;
    float hpHz = 80.0f;
};

struct Result {
    int input = 0;
    bool valid = false;
    Verdict verdict = Verdict::Dead;
    int frames = 0, clips = 0;
    float peakDb = -120.0f, programRmsDb = -120.0f, noiseFloorDb = -120.0f, meanDc = 0.0f, dcDb = -120.0f;
    bool dc = false;
    bool hum = false;
    int humHz = 0;                                               // 50 or 60 when hum is flagged
    float humDb = -120.0f, humRatio = 0.0f;
    bool polarityMismatch = false;
    int polarityPartner = -1;
    float correlationNext = 0.0f;                                // correlation with input+1 (0 when n/a)
    bool hasTrim = false;
    float proposedTrimDb = 0.0f;
    bool trimNoChange = true, limitedByHeadroom = false;
    bool suggestHpOn = false, hpNoChange = true;
    float suggestHpHz = kHpfDefaultHz;
};

// Pulls frames from a ProbeBank during a run and tracks progress.
class Collector {
public:
    void begin(ProbeBank&, int numInputs);
    void pull(ProbeBank&);                                       // message thread, cheap; call from a timer
    int framesCollected() const noexcept { return minFrames; }
    float progress() const noexcept { return std::min(1.0f, static_cast<float>(minFrames) / static_cast<float>(kFramesPerListen)); }
    bool isComplete() const noexcept { return minFrames >= kFramesPerListen; }
    const std::vector<std::vector<Frame>>& frames() const noexcept { return perInput; }
private:
    std::vector<std::vector<Frame>> perInput;
    std::uint32_t generation = 0;
    int minFrames = 0;
};

class Analyser {
public:
    static std::vector<Result> analyse(const std::vector<std::vector<Frame>>& frames, const std::vector<InputContext>& contexts,
                                       const Settings& settings = {});
};

// Plain-language one-line summary of a result ("OK  -21.3 dBFS, trim +1.3 dB") and the flag list ("DC offset, Hum 50 Hz").
std::string describeFlags(const Result&);
// Wiring template index (WiringTemplates::Id) for the detected channel counts (Quick start).
int chooseTemplate(int inputs, int outputs) noexcept;
}
