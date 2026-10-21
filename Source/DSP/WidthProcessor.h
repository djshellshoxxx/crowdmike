#pragma once
// WidthProcessor (params: crowdmike::params::WidthP) -- stereo in-place M/S width tool (EFFECTS_SPEC section 15).
//
// Per sample:  M = (L+R)/2, S = (L-R)/2
//   LF mono   : S1 = S + a * (HPF4(S) - S); HPF4 = 4th-order Butterworth at LfMonoHz, a fades 0/1 (20 ms) with
//               a = 1 when LfMonoHz > 20 Hz (values <= 20 Hz switch LF-mono off) -> bass stays centred.
//   Decorr    : D = 4 Schroeder allpasses (g 0.5) of HPF2(M) at max(LfMonoHz, 150 Hz); injected into the side, so
//               decorrelation also works on near-mono crowd material and never moves the mid/mono sum or the bass.
//   Width     : S2 = Width * (S1 + 0.7 * Decorr * D)       (Width 0 => L == R == M exactly)
//   L1 = M + S2, R1 = M - S2
//   Haas      : the right channel's band above 1 kHz (complementary split R1 = lo + hi, lo = LPF2 1 kHz) is delayed
//               by HaasMs (0..20 ms, glided 50 ms, linear interp); below the split nothing is delayed.
//   MonoCompat: out = x + c * (mono - x), mono = (L+R)/2 of the result (c = MonoCompat 0..1).
// * Width == 1 with every other control at 0 is an identity (within float rounding, < 1e-6).
// * On == 0: 10 ms crossfade to the untouched input; once settled process() returns immediately (bit-exact
//   pass-through). Turning On again resets the internal state first (deterministic).
// * Real-time safe, deterministic after reset(), non-finite input samples become 0, block-size independent.
#include "FxUtil.h"

namespace crowdmike::dsp {
class WidthProcessor {
public:
    static constexpr float kMaxHaasMs = 20.0f;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* left, float* right, int n, const float* p) noexcept;

private:
    void clearState() noexcept;
    void updateFilters(bool force) noexcept;

    double sr = 48000.0;
    fx::FxBiquad lfHp1, lfHp2, decHp, haasLp;
    fx::FxAllpass ap[4];
    fx::FxDelayBuffer haasBuf;
    Smoother onGain, width, decorr, haasSamples, mono, lfAmt, lfHz;
    float appliedLf = -1.0f;
    int ctrlCounter = 0;
    bool fresh = true, idle = false;
};
}
