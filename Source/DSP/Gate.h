#pragma once
// Gate: downward expander / gate driven by GateP (Source/Core/ParamTables.h).
// Invariants:
//  * Detector: per-channel sidechain HPF (ScHpf) + LPF (ScLpf), 2nd-order Butterworth, linked = max over channels,
//    peak follower (instant attack, 30 ms release) -> level in dBFS (a steady sine reads its peak level).
//  * State machine with hysteresis: opens when level >= Thr; while open it stays open as long as level >= Thr - Hyst;
//    once below Thr - Hyst the Hold time (ms) counts down, then the gate closes.
//  * Target gain: open -> 0 dB; closed -> max(Range, (level - Thr) * (Ratio - 1)) dB (Ratio 1 => no reduction,
//    Ratio 20 => gate-like). The gain (dB) moves towards the target with one-pole Attack (opening) / Release (closing).
//  * On crossfades over 10 ms; once On == 0 has settled the buffer is untouched (bit-exact; non-finite -> 0).
//  * p == nullptr -> treated as On == 0. Channels beyond prepare()'s count are untouched.
//  * getGainReductionDb(): smoothed gain (<= 0 dB) at the end of the last block. isOpen(): gate state.
#include "Biquad.h"
#include "DspCommon.h"
#include <atomic>
#include <vector>

namespace crowdmike::dsp {
class Gate {
public:
    void prepare(double sampleRate, int maxBlock, int channels);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, const float* p /* GateP */) noexcept;
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }
    bool isOpen() const noexcept { return openMeter.load(std::memory_order_relaxed); }

private:
    double sr = 48000.0;
    int numChannels = 0;
    bool first = true, settledOff = true, open = false;
    std::vector<Biquad> scHp, scLp;
    float appliedHp = -1.0f, appliedLp = -1.0f, attackMs = -1.0f, releaseMs = -1.0f;
    float attCoef = 0.0f, relCoef = 0.0f, detCoef = 0.0f;
    float det = 0.0f, gainDb = 0.0f;
    int holdLeft = 0;
    Smoother sOn;
    std::atomic<float> grMeter { 0.0f };
    std::atomic<bool> openMeter { false };
};
}
