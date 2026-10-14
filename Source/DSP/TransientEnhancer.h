#pragma once
// TransientEnhancer: envelope-differential transient designer driven by TransP (Source/Core/ParamTables.h).
// Invariants:
//  * Linked detector: |x| = max over channels -> peak follower (instant attack, 20 ms release, removes waveform
//    ripple) -> four one-pole followers:
//      attack pair : fast (1 ms attack / 80 ms release) vs slow (25 ms attack / 80 ms release)
//      sustain pair: fast (1 ms attack / 40 ms release) vs slow (1 ms attack / 300 ms release)
//    attackDiff = dB(fastA) - dB(slowA) >= 0 on onsets; sustainDiff = dB(slowR) - dB(fastR) >= 0 on decays.
//  * Sensitivity maps to the differential (dB) giving full effect: ref = 9 - 7 * Sens (Sens 0 -> 9 dB, 1 -> 2 dB).
//    gainDb = AttackGain * clamp(attackDiff / ref, 0, 1) + SustainGain * clamp(sustainDiff / ref, 0, 1),
//    clamped to +/-18 dB. Steady signals converge to 0 dB (both differentials -> 0). A -100 dBFS floor is added to
//    the followers so noise-floor wiggles never trigger.
//  * Mix: out = x + Mix * (x * g - x), smoothed 20 ms. On crossfades 10 ms; settled-off is bit-exact pass-through
//    (non-finite -> 0). p == nullptr -> On == 0. Channels beyond prepare()'s count are untouched.
#include "DspCommon.h"
#include <atomic>

namespace crowdmike::dsp {
class TransientEnhancer {
public:
    void prepare(double sampleRate, int maxBlock, int channels);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, const float* p /* TransP */) noexcept;
    float getGainDb() const noexcept { return gainMeter.load(std::memory_order_relaxed); }   // last applied gain

private:
    double sr = 48000.0;
    int numChannels = 0;
    bool first = true, settledOff = true;
    float cFastAtt = 0, cSlowAtt = 0, cRelA = 0, cRelFast = 0, cRelSlow = 0, cPre = 0;
    float fastA = 0, slowA = 0, fastR = 0, slowR = 0, pre = 0;
    Smoother sOn, sMix;
    std::atomic<float> gainMeter { 0.0f };
};
}
