#pragma once
// DigitalDelay (params: crowdmike::params::DelayP) -- stereo send in -> stereo WET return.
//
// Invariants / contract:
// * Send/return topology: the output is the wet return only. `Mix` and `OutGain` scale that return (the dry signal is
//   the group path itself). `Return` (returnDb) is NOT applied here -- the engine's FX return fader applies it.
// * Modes: 0 Single (L->L, R->R), 1 Ping-pong (mono sum enters L, repeats alternate L,R,L...),
//   2 Multi-tap (`Taps` taps per channel at k*D/Taps, k=1..Taps, alternating pan; feedback from the k=Taps tap).
// * Delay time D = round(sr * TimeMs / 1000) samples (exact integer delay when static), or with Sync && ctx.hasTempo
//   D = round(beats(Division) * 60 / bpm * sr). Clamped to [1 sample, kMaxDelayMs]. Right channel adds `Offset` ms.
// * Time / offset changes never jump: the read taps crossfade (40 ms, linear) from the old to the new integer delay;
//   a change arriving during a crossfade is applied after it. Mode/Taps changes duck the output (8 ms) and switch.
// * Feedback HPF/LPF (2nd-order Butterworth, |H| <= 1) and optional diffusion (4 Schroeder allpasses, |H| == 1, mixed
//   with the dry loop signal) sit in the FEEDBACK path only, so the first repeat is unfiltered and sample exact.
// * Boundedness (any parameter combination, any input): loop gain <= min(Feedback, 0.949) * 1 < 0.95 (or the freeze
//   value 0.97) at every frequency, and every sample written to the delay line passes fxSoftLimit(knee 1) and a hard
//   clamp to +/-2, so |buffer| <= 2 and |output| <= 2 * sum(tap gains) * OutGain * Mix regardless of parameters.
// * FxContext.freeze: input gated closed (50 ms), feedback pinned at 0.97, loop filters faded out -> loop holds.
// * Bypass=1: input and return fade out over 10 ms; once silent the buffers are cleared deterministically (O(1)) and
//   process() just writes zeros. Leaving bypass fades the return in from empty buffers.
// * Real-time: process() is noexcept, allocation/lock free, non-finite input samples are treated as 0, block-size
//   independent (all smoothing per sample; control-rate filter updates on a persistent 16-sample counter).
#include "FxContext.h"
#include "FxUtil.h"

namespace crowdmike::dsp {
class DigitalDelay {
public:
    static constexpr float kMaxDelayMs = 4000.0f;   // also bounds tempo-synced times at slow tempi
    static constexpr float kMaxOffsetMs = 50.0f;
    static constexpr int kMaxTaps = 6;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                 const FxContext& ctx) noexcept;

    // Current target main delay in samples (after sync / clamping). For tests and UI.
    int getTargetDelaySamples() const noexcept { return targetD; }
    // Division index -> beats (docs/MODULES.md).
    static double divisionBeats(int index) noexcept;

private:
    struct Cfg { int dL = 1, dR = 1; int tapL[kMaxTaps] {}, tapR[kMaxTaps] {}; };
    void buildCfg(Cfg& c, int dL, int dR) const noexcept;
    void readCfg(const Cfg& c, float& yL, float& yR, float& fL, float& fR) const noexcept;
    void updateFilters(bool force) noexcept;
    void clearState() noexcept;

    double sr = 48000.0;
    fx::FxDelayBuffer bufL, bufR;
    fx::FxBiquad hpL, hpR, lpL, lpR;
    fx::FxAllpass apL[4], apR[4];
    Smoother inGain, outGain, fbAmt, diffAmt, freezeAmt, bypassGain, duck, hpHz, lpHz;
    Cfg cur, next;
    float xfPos = 0.0f, xfStep = 0.0f;
    bool xfActive = false;
    int activeMode = 0, activeTaps = 4, wantMode = 0, wantTaps = 4;
    int targetD = 1, targetDR = 1;
    int ctrlCounter = 0;
    float appliedHp = -1.0f, appliedLp = -1.0f;
    bool fresh = true, cleared = true;
};
}
