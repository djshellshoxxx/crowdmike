#pragma once
// TapeDelay (params: crowdmike::params::TapeP) -- modulated tape-style echo. Stereo send in -> stereo WET return.
//
// Invariants / contract:
// * Delay time glides like a tape transport (one-pole slew, ~150 ms): time changes bend pitch, never click.
// * Read position = glided time + Heads offset (right head up to +20 ms, wow phase offset) + wow (0.6 Hz sine,
//   +/-2.5 ms * Wow) + flutter (7.3 Hz + 11.9 Hz, up to +/-0.12 ms * Flutter) + age drift (filtered random walk,
//   +/-1.5 ms * Age). Linear interpolation (|H| <= 1).
// * Playback head tone: HPF(LowCut) and LPF(HighCut * (1 - 0.55 Age)), 2nd-order Butterworth (|H| <= 1).
// * Record head: written = tanh(g * in + fb * toneOut), g = 1 + 3 * Drive, fb = min(Feedback, 0.919). Because
//   |d tanh| <= 1 the loop is a contraction (gain <= 0.919 < 0.92) and |written| <= 1 for ANY input/parameters.
//   Wet output = toneOut * (1 + Drive) / g  =>  |wet| <= ~1.1 * Mix plus hiss.
// * Hiss: independent L/R white noise through a 7 kHz one-pole, level 0.003 * Noise + 0.0008 * Age, added to the wet
//   return (not the loop). Deterministic RNG re-seeded in reset().
// * Mix scales the wet return; `Return` is applied by the engine. FxContext is unused (no tempo sync, no freeze).
// * Bypass: 10 ms fade, then deterministic clear (buffers O(1), LFO phases, RNG) and zero output.
// * Real-time safe, non-finite input treated as 0, block-size independent.
#include "FxContext.h"
#include "FxUtil.h"

namespace crowdmike::dsp {
class TapeDelay {
public:
    static constexpr float kMaxTimeMs = 2000.0f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                 const FxContext& ctx) noexcept;

    // Current (glided, unmodulated) delay in samples. For tests/UI.
    float getCurrentDelaySamples() const noexcept { return delayS; }

private:
    void clearState() noexcept;

    double sr = 48000.0;
    fx::FxDelayBuffer bufL, bufR;
    fx::FxBiquad hpL, hpR, lpL, lpR;
    fx::FxOnePole hissFL, hissFR, driftF1, driftF2;
    fx::FxRng rng, rngDrift;
    Smoother fbAmt, drive, wowAmt, flutAmt, ageAmt, noiseAmt, mixAmt, headsAmt, bypassGain, lowCut, highCut;
    float delayS = 0.0f, slewCoef = 0.0f, driftTarget = 0.0f;
    int driftCount = 0;
    double wowPh = 0.0, flPh1 = 0.0, flPh2 = 0.0;
    float appliedLc = -1.0f, appliedHc = -1.0f;
    int ctrlCounter = 0, maxDelaySamples = 1;
    bool fresh = true, cleared = true;
};
}
