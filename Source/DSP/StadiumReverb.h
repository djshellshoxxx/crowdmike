#pragma once
// StadiumReverb (params: crowdmike::params::ReverbP) -- stereo send in -> stereo WET return. docs/EFFECTS_SPEC.md section 19.
//
// Topology: send -> pre-delay -> 4+4 input allpass diffusers -> 8-line feedback delay network -> decorrelated stereo
// taps -> width -> low-cut -> wet level. The network is  w_i = u_i + H( g_i * damp_i( y_i ) )_i  with H an orthogonal
// 8x8 Hadamard matrix (scaled 1/sqrt 8), y_i the (slowly modulated, linearly interpolated) delay-line outputs,
// damp_i a one-pole low-pass with unity DC gain (transparent at HF Damping = 20 kHz) and g_i = 10^(-3 d_i / (RT60 * sr)) the per-line decay gain
// (d_i = nominal line length in samples), so every mode of the network decays at exactly the RT60 set by `Decay`.
//
// Invariants / contract:
// * Send/return topology: the output is the wet return only. `Wet` scales it; `Return` (returnDb) is applied by FxRack.
// * Unconditional stability: loop gain <= max(g_i) < 1 for every parameter value (Decay is clamped to [0.2, 20] s,
//   the freeze gain is 0.99998), H is orthogonal, damp_i / interpolation / allpasses have |H| <= 1. In addition every value
//   written to a line passes fxSoftLimit(knee 4) and a hard clamp to +/-8, so the state is bounded by construction
//   even under time-varying delays and NaN/Inf parameters (sanitised) or inputs (non-finite = 0, finite clamped to +/-64).
// * Level: the output is scaled by sqrt(min(4, 8 (1 - mean g_i^2))) so the impulse-response energy per output channel is
//   about 1 for any Decay/Size (a noise-like input comes back at about the same level at Wet = 0 dB). The scale
//   uses the un-frozen gains so Freeze never changes the level.
// * Freeze (param or FxContext.freeze): input gated closed (50 ms), line gain -> 0.99998, damping faded out; the tail
//   holds (loop gain 0.99998; only the slight cubic-interpolation HF loss remains) and stays bounded. TailKill (held): input and loop gain and output fade to 0 in ~40 ms,
//   the lines drain with zeros; releasing it fades the effect back in from silence. Both are per-sample smoothed.
// * Bypass (On = 0): input and return fade out over 10 ms; once silent all state is cleared deterministically (O(1) for
//   the lines, tiny fixed cost for the allpasses) and process() just writes zeros.
// * Real-time: process() is noexcept, allocation/lock free, per-sample cost independent of the parameters; all state is
//   advanced per sample and control-rate updates run on a persistent 16-sample counter, so the output is block-size
//   independent (bit-identical for any chunking of the same input and constant parameters).
#include "FxContext.h"
#include "FxUtil.h"

namespace crowdmike::dsp {
class StadiumReverb {
public:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;
    static constexpr float kMaxPreDelayMs = 250.0f;
    static constexpr float kMinDecaySec = 0.2f;
    static constexpr float kMaxDecaySec = 20.0f;
    static constexpr float kFreezeLoopGain = 0.99998f;
    static constexpr float kStateKnee = 4.0f;
    static constexpr float kStateClamp = 8.0f;
    static constexpr float kInputClamp = 64.0f;   // inputs are clamped to +/-64 (+36 dBFS) so finite input can never overflow

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                 const FxContext& ctx) noexcept;

    // Nominal (unmodulated) delay of line i at Size = `size`, in milliseconds. For tests / UI.
    static float lineMs(int line, float size) noexcept;
    // Time (seconds) until the wet tail is below -60 dB after the input stops, for a Decay setting (pre-delay excluded).
    static float tailSeconds(float decaySec) noexcept { return clampf(decaySec, kMinDecaySec, kMaxDecaySec); }

private:
    struct Line {
        fx::FxDelayBuffer buf;
        float lp = 0.0f;          // damping one-pole state
        float len = 0.0f;         // current (smoothed, modulated) delay in samples
        float lenStep = 0.0f;
        float gain = 0.0f;        // current loop gain incl. freeze / kill
        float phase = 0.0f;       // LFO phase 0..1
    };
    void clearState() noexcept;
    void controlTick() noexcept;

    double sr = 48000.0;
    Line lines[kLines];
    fx::FxDelayBuffer preL, preR;
    fx::FxAllpass apL[kDiffusers], apR[kDiffusers];
    fx::FxBiquad lowCutL, lowCutR;
    Smoother sizeSm, decaySm, preSm, hfSm, lowCutSm, diffSm, modDepthSm, modRateSm, widthSm, wetSm, freezeSm, killSm, bypassSm;
    float appliedLowCut = -1.0f;
    float dampA = 0.0f;           // one-pole feedback coefficient (0 = transparent)
    float diffG = 0.0f;
    float outNorm = 1.0f;
    float modDepthSamples = 0.0f, modRateInc = 0.0f;
    int ctrlCounter = 0;
    bool fresh = true, cleared = true, lensDirty = true;
};
}
