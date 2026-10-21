#pragma once
// SlopeFilter: cascaded Butterworth high-pass or low-pass, slope index 0..3 = 12/24/36/48 dB/oct (2/4/6/8 poles).
// Invariants:
//  * Section Qs are the exact Butterworth values Q_k = 1 / (2 cos(pi (2k+1) / (2N))); the cascade is -3.01 dB at `hz`.
//  * Cutoff glides in the log-frequency domain over 50 ms; coefficients are recomputed every kControlInterval samples
//    and only while gliding. Control ticks are counted across blocks, so output is block-size independent.
//  * Slope changes crossfade (30 ms) between two filter banks; enable/disable crossfades over 20 ms.
//  * When disabled and the fade has settled, process() is a bit-exact pass-through (non-finite samples -> 0) and the
//    filter state is cleared. The very first call after prepare()/reset() snaps to the requested state (no fade-in).
//  * hz is clamped to [10 Hz, min(0.45 sr, 30 kHz)]; slopeIndex clamped to 0..3. Channels >= prepared count are untouched.
#include "Biquad.h"
#include "DspCommon.h"

namespace crowdmike::dsp {
class SlopeFilter {
public:
    static constexpr int kControlInterval = 16;
    void prepare(double sampleRate, int maxBlock, int channels, bool highpass);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, bool enabled, float hz, int slopeIndex) noexcept;
    bool isSettledOff() const noexcept { return settledOff; }
    static double butterworthQ(int poles, int section) noexcept;   // section 0..poles/2-1

private:
    void designBank(int b, int slope, float hz) noexcept;
    void controlTick() noexcept;

    double sr = 48000.0;
    bool isHighpass = true;
    int numChannels = 0;
    BiquadCascade bank[2];
    int bankSlope[2] { 0, 0 };
    int cur = 0;
    bool xfading = false, first = true, settledOff = false;
    int targetSlope = 0, countdown = 0;
    float appliedHz = 1000.0f;
    Smoother enableGain, logHz, xfade;
};
}
