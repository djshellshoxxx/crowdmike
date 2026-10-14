#pragma once
// NoiseEnhancer (params: crowdmike::params::NoiseP) -- controlled, shaped noise "under" the crowd. Output = WET return.
//
// Signal chain (EFFECTS_SPEC section 11):
//   3 deterministic xorshift streams (common C, independent L, R; seeded from Seed so a saved Seed reproduces the
//   exact stream after reset()) -> stereo decorrelation: ch = cos(W*pi/2) * C + sin(W*pi/2) * ch_indep
//   (Width 0 = identical L/R, 1 = fully independent, equal power) -> colour (White / Pink (Kellet) / Brown
//   (leaky integrator ~40 Hz) / Bright (first difference), each normalised to ~unit RMS, colour changes crossfade
//   20 ms) -> band shaping: HPF + LPF (2nd-order Butterworth) and Tilt (+/-6 dB shelf pair around 1 kHz)
//   -> level: Amount * 0.012 * [ (1 - Follow) + Follow * clamp(env * 4, 0, 1) + TransFollow * transient ]
//   where env = peak envelope of the send input (Attack / Release ms) and transient = clamp(0.5 (fast - slow) / (slow + 0.01), 0, 1)
//   (fast: 1/30 ms peak follower, slow: 80/300 ms; the 0.01 floor keeps very quiet onsets from bursting)
//   -> safety: soft ceiling c * tanh(x / c) and a hard clamp, c = -24 dBFS (0.0631): |out| < 0.0631 ALWAYS.
// * Amount == 0 => output exactly 0. Silent input with Follow == 1 and TransFollow == 0 => output exactly 0.
// * `Return` is applied by the engine. FxContext is unused.
// * Bypass: 10 ms fade then zeros (generators keep their seed position reset on settle => deterministic).
// * Real-time safe, deterministic after reset(), non-finite input treated as 0, block-size independent.
#include "FxContext.h"
#include "FxUtil.h"

namespace crowdmike::dsp {
class NoiseEnhancer {
public:
    static constexpr float kCeiling = 0.0630957f;   // -24 dBFS
    static constexpr float kBaseLevel = 0.012f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* sendL, const float* sendR, float* outL, float* outR, int n, const float* p,
                 const FxContext& ctx) noexcept;

private:
    struct Colour {
        float b0 = 0, b1 = 0, b2 = 0, brown = 0, prev = 0;
        void reset() noexcept { *this = Colour {}; }
    };
    void reseed(uint32_t seedValue) noexcept;
    void clearState() noexcept;

    double sr = 48000.0;
    fx::FxRng rngC, rngL, rngR;
    Colour colL, colR;
    fx::FxBiquad hpL, hpR, lpL, lpR;
    fx::FxOnePole tiltL, tiltR;
    Smoother amount, follow, transFollow, width, tiltLo, tiltHi, bypassGain, hpHz, lpHz;
    Smoother colourGain[4];
    float env = 0.0f, fastEnv = 0.0f, slowEnv = 0.0f, brownCoef = 0.995f, brownNorm = 0.1f;
    float fastAtk = 0.0f, fastRel = 0.0f, slowAtk = 0.0f, slowRel = 0.0f;
    float appliedHp = -1.0f, appliedLp = -1.0f;
    int ctrlCounter = 0;
    uint32_t currentSeed = 0;
    bool fresh = true, cleared = true;
};
}
