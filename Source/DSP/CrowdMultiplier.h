#pragma once
// CrowdMultiplier (params: crowdmike::params::MultiP) -- stereo send in -> stereo WET return. docs/EFFECTS_SPEC.md section 20.
//
// Makes a small crowd sound larger: an ensemble of N (2..16) decorrelated "extra people". All voices read the same pair of
// short stereo delay lines (written once per sample); every voice has its own
//   * base delay 2..26 ms (golden-ratio spread, so any prefix of the voice list covers the range evenly),
//   * smoothed random LFO (random targets joined by smoothstep, per-voice rate ratio 0.7..1.3 of `Rate`, +/- `Depth` ms),
//   * static detune of up to +/- `Detune` cents (two-tap crossfaded delay ramp, 60 ms window, alternating up/down),
//   * pan (equal-power balance, scattered by `Scatter`), gain jitter (+/-30 % * Scatter), small L/R delay offset
//     (<= 0.5 ms * Scatter) and a tone tilt (+/-0.5 around 1.2 kHz, scaled by `Tone`).
// The voices are summed and scaled by 1/sqrt(N) (incoherent power sum ~ unity), then by `Level`.
//
// Invariants / contract:
// * Send/return topology: wet only (the dry crowd is the group path). `Return` (returnDb) is applied by FxRack.
// * Determinism: every per-voice constant is a pure function of (Seed, voice index); voice v is identical for any Voices
//   count. After reset() the same input and parameters give a bit-identical output. Changing Seed glides (delays slew at
//   <= 2 % pitch, gains/pan are smoothed) and keeps the LFO position.
// * Voices changes fade the affected voices in/out over 30 ms and the 1/sqrt(N) scale glides with them. Dormant voices cost
//   nothing; the cost is linear in the active voice count and independent of everything else.
// * Boundedness: |out| <= Level * 2.47 * sqrt(N) * max|in| (fully coherent worst case: pan 1.414 * jitter 1.3 * tone 1.34); every delay read is
//   clamped inside the buffer, the tone filter has |H| <= 1.34, no feedback exists, so the effect is unconditionally stable.
//   Non-finite input is treated as 0, finite input is clamped to +/-64; non-finite parameters fall back to defaults and are clamped.
// * Mono compatibility: pan uses positive balance gains, so the mono sum never cancels; L/R offsets are < 0.5 ms.
// * Bypass (On = 0): input and return fade out over 10 ms, then buffers are cleared (O(1)) and voice state re-initialised.
// * Real-time: process() is noexcept, allocation/lock free and block-size independent (per-sample smoothing, control-rate
//   updates on a persistent 16-sample counter).
#include "FxContext.h"
#include "FxUtil.h"

namespace crowdmike::dsp {
class CrowdMultiplier {
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kMinVoices = 2;
    static constexpr float kMaxDepthMs = 20.0f;
    static constexpr float kMaxDetuneCents = 50.0f;
    static constexpr float kMinBaseMs = 2.0f, kBaseSpanMs = 24.0f;
    static constexpr float kMaxStereoOffsetMs = 0.5f;
    static constexpr float kRampWindowMs = 60.0f;
    static constexpr float kInputClamp = 64.0f;   // inputs are clamped to +/-64 (+36 dBFS)

    // Static per-voice character derived from (seed, voice). Public for tests and the UI.
    struct VoiceTraits {
        float baseMs = 2.0f;         // 2 .. 26 ms
        float pan = 0.0f;            // -1 .. 1 (times Scatter at run time)
        float gainJitter = 0.0f;     // -1 .. 1 (times 0.3 * Scatter)
        float tone = 0.0f;           // -1 .. 1 (times 0.5 * Tone)
        float detune = 0.0f;         // -1 .. 1 (times Detune cents); sign = direction
        float stereoOffset = 0.0f;   // -1 .. 1 (times 0.5 ms * Scatter), added to the right channel delay
        float rateRatio = 1.0f;      // 0.7 .. 1.3
    };
    static VoiceTraits traits(int seed, int voice) noexcept;
    static float normalisation(int voices) noexcept { return 1.0f / std::sqrt(static_cast<float>(std::clamp(voices, kMinVoices, kMaxVoices))); }

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                 const FxContext& ctx) noexcept;

private:
    struct Voice {
        VoiceTraits t;
        fx::FxRng rng;
        float lfoA = 0.0f, lfoB = 0.0f, lfoPhase = 0.0f;
        float base = 0.0f;               // current base delay (samples), slews to the target
        float slow = 0.0f, slowStep = 0.0f;   // base + depth * (1 + lfo)  (samples), interpolated per tick
        float ramp = 0.0f;               // detune window phase 0..1
        float offR = 0.0f, offRStep = 0.0f;   // right-channel delay offset (samples)
        float dmax = 0.0f;               // detune ramp amplitude (samples)
        bool up = false;                 // detune direction: true = pitch up (delay shrinks)
        float tilt = 0.0f;               // tone tilt -0.5 .. 0.5
        float lpL = 0.0f, lpR = 0.0f;    // tone one-poles
        float gL = 0.0f, gR = 0.0f;      // current channel gains (per tick, linear interpolation)
        float gLStep = 0.0f, gRStep = 0.0f;
        bool snap = true;                // next tick sets the interpolated values directly
        Smoother act;                    // voice fade-in/out
        bool dormant = true;
    };
    void initVoice(int v, int seed) noexcept;
    void clearState() noexcept;
    void tick() noexcept;

    double sr = 48000.0;
    fx::FxDelayBuffer bufL, bufR;
    Voice voices[kMaxVoices];
    Smoother depthSm, rateSm, detuneSm, scatterSm, toneSm, levelSm, normSm, bypassSm;
    float lpK = 0.1f, maxDelay = 0.0f;
    float windowSamples = 2880.0f;
    float depthSamples = 0.0f, rateInc = 0.0f, detuneCents = 0.0f, scatter = 0.0f, tone = 0.0f;
    int appliedSeed = -1;
    int ctrlCounter = 0;
    bool fresh = true, cleared = true;
};
}
