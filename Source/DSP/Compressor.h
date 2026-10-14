#pragma once
// Compressor: feed-forward, log-domain compressor driven by CompP (Source/Core/ParamTables.h).
// Invariants:
//  * Detector: per-channel sidechain 2nd-order Butterworth HPF (CompP::ScHpf), linked peak detection = max over the
//    processed channels (1..N, normally 1 or 2), instantaneous level in dBFS.
//  * Static curve (Giannoulis soft knee, width Knee dB):  below T - W/2 -> 0 dB;  above T + W/2 -> (x - T)(1/R - 1);
//    inside the knee (1/R - 1)(x - T + W/2)^2 / (2W). The gain-reduction (dB) is smoothed with branching one-pole
//    attack/release (CompP::Attack / Release time constants).
//  * Makeup: AutoMakeup on -> makeup = -Thr * (1 - 1/Ratio) * 0.5 dB and the manual Makeup is ignored; off -> Makeup.
//  * Mix: out = dry + Mix * (compressed * makeup - dry) (parallel compression).
//  * Threshold, ratio, knee, makeup, mix are smoothed over 20 ms; On crossfades over 10 ms. Once On == 0 has settled
//    the buffer is untouched (bit-exact; non-finite -> 0) and the detector state is cleared.
//  * p == nullptr -> treated as On == 0. Channels beyond prepare()'s count are untouched.
//  * getGainReductionDb(): the deepest smoothed gain reduction (<= 0 dB, excluding makeup) of the last block.
#include "Biquad.h"
#include "DspCommon.h"
#include <atomic>
#include <vector>

namespace crowdmike::dsp {
class Compressor {
public:
    void prepare(double sampleRate, int maxBlock, int channels);
    void reset() noexcept;
    void process(float* const* ch, int nch, int n, const float* p /* CompP */) noexcept;
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }
    // Static gain computer (dB in -> gain change dB), exposed for tests/GUI curve drawing.
    static float computeGainDb(float levelDb, float thrDb, float ratio, float kneeDb) noexcept;

private:
    double sr = 48000.0;
    int numChannels = 0;
    bool first = true, settledOff = true;
    std::vector<Biquad> scHpf;
    float appliedScHz = -1.0f, attackMs = -1.0f, releaseMs = -1.0f, attCoef = 0.0f, relCoef = 0.0f;
    float env = 0.0f;
    Smoother sThr, sRatio, sKnee, sMakeup, sMix, sOn;
    std::atomic<float> grMeter { 0.0f };
};
}
