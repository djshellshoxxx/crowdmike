#pragma once
// EchoThrow (params: crowdmike::params::EchoP) -- performance "throw" echo. Stereo send in -> stereo WET return.
//
// Invariants / contract:
// * Trigger (bool) rising edge = throw. Held high does not retrigger. The first process() after reset() only latches
//   the current Trigger/TailKill state (a restored "on" value never fires).
// * A throw opens the capture for CaptureMs (5 ms raised-cosine gate edges, scaled by Send) into its own preallocated
//   slot (no feedback loop). Repeat k (k = 1..Repeats) of every captured sample is played k*TimeMs later with gain
//   Decay^(k-1). TimeMs, CaptureMs and Repeats are latched at the throw; Decay and Send are live (smoothed).
//   After Repeats the throw ends and the return decays to exact digital silence (tone filters are reset after idle).
// * FxContext.freeze: the repeat count is suspended (repeats continue at the gain of repeat `Repeats`); when freeze is
//   released the repeats already sounding finish and the throw ends.
// * Up to 3 throws overlap (retriggering while one is still repeating keeps it ringing); if a 4th arrives the oldest is
//   faded out over 10 ms, or replaced outright when all 3 slots are busy (pathological, < 1 crossfade apart).
// * TailKill rising edge fades every throw to silence in 40 ms and frees the slots.
// * Output tone: HPF (Hpf) and LPF (Lpf), 2nd-order Butterworth, on the wet return. `Return` is applied by the engine.
// * Bypass: return fades out over 10 ms, then all throws are cleared and process() writes zeros.
// * Peak bound: |out| <= max|in| * Send * sum_k Decay^(k-1) * filter overshoot (finite for every parameter value).
// * Real-time safe, deterministic after reset(), non-finite input treated as 0.
#include "FxContext.h"
#include "FxUtil.h"
#include <atomic>

namespace crowdmike::dsp {
class EchoThrow {
public:
    static constexpr int kSlots = 3;
    static constexpr float kMaxCaptureMs = 4000.0f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                 const FxContext& ctx) noexcept;

    int getActiveThrows() const noexcept { return activeThrows.load(std::memory_order_relaxed); }
    bool isCapturing() const noexcept { return capturing.load(std::memory_order_relaxed); }

private:
    struct Slot {
        bool active = false, killing = false;
        int64_t t = 0;          // samples since the throw
        int capLen = 0, T = 1, R = 1, Rend = 1, capK = 1;
        float kill = 1.0f, killStep = 0.0f;
        uint32_t order = 0;
    };
    void startThrow(int timeSamples, int capSamples, int repeats) noexcept;
    void killAll(float seconds) noexcept;

    double sr = 48000.0;
    int capCapacity = 1;
    std::vector<float> capL[kSlots], capR[kSlots];
    Slot slots[kSlots];
    fx::FxBiquad hpL, hpR, lpL, lpR;
    Smoother sendAmt, decayAmt, bypassGain, hpHz, lpHz;
    float appliedHp = -1.0f, appliedLp = -1.0f;
    int ctrlCounter = 0, idleCount = 0;
    uint32_t orderCounter = 0;
    bool prevTrig = false, prevKill = false, prevFreeze = false, fresh = true, cleared = true;
    std::atomic<int> activeThrows { 0 };
    std::atomic<bool> capturing { false };
};
}
