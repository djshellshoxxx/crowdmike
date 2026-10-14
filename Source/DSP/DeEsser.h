#pragma once
// DeEsser: driven by DeEssP (Source/Core/ParamTables.h).
// Invariants:
//  * Detector band = RBJ constant-0-dB-peak bandpass at Freq (Q = 1.4), per channel; linked peak = max over channels.
//    The band frequency glides in the log domain (50 ms), coefficients updated every 16 samples while gliding.
//  * Gain computer: over = level - Thr; gr = -min(Range, over * (1 - 1/Ratio)) for over > 0, else 0; smoothed in dB
//    with one-pole Attack (more reduction) / Release.
//  * Mode 0 (Split-band, default): out = x - (1 - g) * band, i.e. only the detected band is reduced; since the RBJ
//    bandpass and notch are exactly complementary, everything outside the band is untouched (g == 1 => out == x).
//    Mode 1 (Wideband): out = x * g. Mode switches crossfade over 10 ms.
//  * Listen: output is the detector band signal (crossfaded over 10 ms).
//  * On crossfades over 10 ms; once On == 0 has settled the buffer is untouched (bit-exact; non-finite -> 0).
//  * p == nullptr -> treated as On == 0. Channels beyond prepare()'s count (and beyond 64) are untouched.
//  * getGainReductionDb(): the deepest smoothed reduction (<= 0 dB) of the last block.
#include "Biquad.h"
#include "DspCommon.h"
#include <atomic>
#include <vector>

namespace crowdmike::dsp {
class DeEsser {
public:
    void prepare(double sampleRate, int maxBlock, int channels);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, const float* p /* DeEssP */) noexcept;
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }

private:
    double sr = 48000.0;
    int numChannels = 0, countdown = 0;
    bool first = true, settledOff = true;
    std::vector<Biquad> bp;
    float attackMs = -1.0f, releaseMs = -1.0f, attCoef = 0.0f, relCoef = 0.0f, env = 0.0f;
    Smoother sOn, sListen, sMode, logHz;
    std::atomic<float> grMeter { 0.0f };
};
}
