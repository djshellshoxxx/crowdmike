#pragma once
// Ducker: voice-activated gain curve for the performer ducker (docs/DUCKER_SPEC.md). Driven by DuckP (Core/ParamTables.h).
// Invariants:
//  * The key (already trimmed by the caller) goes through a 2nd-order Butterworth HPF + LPF (KeyHpf/KeyLpf), then a peak
//    follower (instant attack, fixed 5 ms release) -> level in dBFS.
//  * State machine: opens at level >= Thr; while open the hold counter is reloaded whenever level >= Thr - 3 dB (fixed
//    hysteresis); below that it counts down Hold ms and then closes. `enabled == false` closes immediately.
//  * The reduction g (dB, <= 0) moves LINEARLY IN dB towards max(Depth, Range) (open) or 0 (closed). Attack / Release are the
//    times to travel the whole depth (span = max(|target|, |g|, 0.01) dB), so a 10..90 % traverse takes 0.8 * time exactly.
//  * process() writes one linear gain per sample (exactly 1.0f where g == 0), never > 1, always finite. noexcept, no allocation.
//  * reset() is deterministic. Block size independent (bit-identical output for any block split).
#include <atomic>
#include "Biquad.h"
#include "DspCommon.h"

namespace crowdmike::dsp {
class Ducker {
public:
    void prepare(double sampleRate, int maxBlock);
    void reset() noexcept;
    // key: n post-trim key samples. p: DuckP block (nullptr -> disabled). gain: n output gains.
    // Returns true when any gain differs from 1.0f.
    bool process(const float* key, int n, const float* p, bool enabled, float* gain) noexcept;
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }   // >= 0
    float getKeyLevelDb() const noexcept { return levelMeter.load(std::memory_order_relaxed); }
    float getKeyPeak() const noexcept { return peakMeter.load(std::memory_order_relaxed); }          // linear, detector output
    bool isOpen() const noexcept { return openMeter.load(std::memory_order_relaxed); }

    static constexpr float kHysteresisDb = 3.0f;
    static constexpr float kDetectorReleaseMs = 5.0f;

private:
    void updateFilters(float hpf, float lpf) noexcept;

    double sr = 48000.0;
    Biquad hp, lp;
    float appliedHpf = -1.0f, appliedLpf = -1.0f;
    float detCoef = 0.0f, env = 0.0f;
    double grDb = 0.0;               // double: a float accumulator drifts measurably over multi-second ramps
    bool open = false;
    long holdLeft = 0;
    std::atomic<float> grMeter { 0.0f }, levelMeter { -120.0f }, peakMeter { 0.0f };
    std::atomic<bool> openMeter { false };
};
}
