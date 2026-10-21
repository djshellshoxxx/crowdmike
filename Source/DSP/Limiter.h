#pragma once
// Limiter: lookahead brickwall peak limiter driven by LimP (Source/Core/ParamTables.h).
// Invariants:
//  * Latency L = round(lookaheadMs * sr / 1000) samples (lookaheadMs clamped 0..50), fixed at prepare(). The audio is
//    ALWAYS delayed by exactly L samples, also when On == 0, so getLatencySamples() never changes.
//  * Gain path per channel (linked: every path is fed max |x| over channels, so all paths agree):
//      req[t]  = min(1, ceiling / |drive * x[t]|)
//      held[t] = min(req[t-L .. t])                       (exact sliding minimum, window W = L + 1)
//      env[t]  = held < env ? held : held + rc * (env - held)   (instant attack, one-pole Release)
//      G[t]    = mean(env[t-L .. t])                      (boxcar of W samples -> smooth attack ramp)
//      out[t]  = clamp(drive[t-L] * x[t-L] * G[t], +/-ceiling)
//    G[t] <= req[t-L] by construction, so the final clamp is only a numeric safety net: output magnitude never exceeds
//    the ceiling once On has settled. lookaheadMs == 0 gives an instant-attack bus limiter plus the hard clamp.
//  * Drive (input gain, dB) is smoothed over 20 ms. Ceiling and Release apply immediately (the boxcar smooths).
//  * On crossfades over 10 ms between the delayed dry signal and the limited signal. Settled off: delayed copy only
//    (bit-exact delayed input; with L == 0 the buffer is untouched except non-finite -> 0), gain path reset. On
//    re-enable the gain path is primed with the L samples already in the delay line (full lookahead at once).
//  * Toggling linkChannels is click-free (paths converge through the boxcar/release).
//  * p == nullptr -> On == 0. Channels beyond prepare()'s count are untouched (and not delayed!).
//  * getGainReductionDb(): deepest gain reduction (<= 0 dB, drive excluded) in the last block.
#include "DspCommon.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace crowdmike::dsp {
class Limiter {
public:
    void prepare(double sampleRate, int maxBlock, int channels, float lookaheadMs);
    void reset() noexcept;
    int getLatencySamples() const noexcept { return latency; }
    void process(float* const* ch, int nch, int n, const float* p /* LimP */, bool linkChannels) noexcept;
    float getGainReductionDb() const noexcept { return grMeter.load(std::memory_order_relaxed); }

private:
    void resetGainPaths() noexcept;
    float gainStep(size_t channel, float req, std::int64_t index, double invW) noexcept;

    double sr = 48000.0;
    int numChannels = 0, latency = 0, window = 1, dqSize = 1, delaySize = 1, writePos = 0, boxPos = 0;
    std::int64_t t = 0;
    bool first = true, settledOff = true;
    float relMs = -1.0f, relCoef = 0.0f;
    std::vector<float> delay, drvRing, box, dqVal, env;
    std::vector<std::int64_t> dqIdx;
    std::vector<int> dqHead, dqTail;
    std::vector<double> boxSum;
    Smoother sDriveDb, sOn;
    std::atomic<float> grMeter { 0.0f };
};
}
