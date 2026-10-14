#pragma once
// AllpassPhase: 0..4 cascaded 2nd-order allpass sections (all at the same frequency, Q = 0.7071) for phase rotation.
// Each stage contributes -180 degrees of phase at `hz` and unity magnitude at every frequency.
// Signature (natural reading of docs/MODULES.md): process(ch, nch, n, stages, hz).
// Invariants:
//  * Stage-count changes crossfade (30 ms) between two cascades; hz glides in the log domain (50 ms) with coefficient
//    updates every kControlInterval samples, only while gliding (block-size independent).
//  * stages == 0 and settled: bit-exact pass-through (non-finite samples -> 0), state cleared.
//  * hz clamped to [10 Hz, min(0.45 sr, 30 kHz)], stages to 0..4. Channels >= prepared count are untouched.
#include "Biquad.h"
#include "DspCommon.h"

namespace crowdmike::dsp {
class AllpassPhase {
public:
    static constexpr int kControlInterval = 16;
    static constexpr int kMaxStages = 4;
    void prepare(double sampleRate, int maxBlock, int channels);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, int stages, float hz) noexcept;

private:
    void designBank(int b, float hz) noexcept;
    void controlTick() noexcept;

    double sr = 48000.0;
    int numChannels = 0;
    BiquadCascade bank[2];
    int cur = 0, targetStages = 0, countdown = 0;
    bool xfading = false, first = true;
    float appliedHz = 500.0f;
    Smoother logHz, xfade;
};
}
