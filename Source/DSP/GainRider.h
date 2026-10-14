#pragma once
// GainRider: slow automatic level riding towards a target RMS level.
// Signature: process(ch, nch, n, bool enabled) (docs/MODULES.md); target set with setTargetLevelDb (default -20 dBFS).
// Invariants:
//  * Level = RMS of the channel-mean power with a 1.5 s time constant (dBFS). A fast 50 ms RMS gates the rider:
//    while it is below -60 dBFS, or more than 12 dB below the slow level (sudden stop), the gain is frozen (never
//    boosts noise/silence, never chases a crowd that just went quiet).
//  * desired = clamp(target - levelDb, -6, +6) dB; the applied gain glides to it with a 2 s time constant and a slew
//    limit of 3 dB/s, so riding never pumps audibly.
//  * enabled toggles crossfade over 50 ms; settled disabled: buffer untouched (bit-exact; non-finite -> 0), gain
//    state reset to 0 dB. Channels beyond prepare()'s count are untouched. getGainDb(): applied riding gain.
#include "DspCommon.h"
#include <atomic>

namespace crowdmike::dsp {
class GainRider {
public:
    void prepare(double sampleRate, int maxBlock, int channels);
    void reset() noexcept;
    void setTargetLevelDb(float db) noexcept { targetDb = clampf(db, -60.0f, 0.0f); }
    void process(float* const* ch, int nch, int n, bool enabled) noexcept;
    float getGainDb() const noexcept { return gainMeter.load(std::memory_order_relaxed); }

    static constexpr float kMaxGainDb = 6.0f;
    static constexpr float kFreezeDb = -60.0f;

private:
    double sr = 48000.0;
    int numChannels = 0;
    bool first = true, settledOff = true, warm = false;
    float targetDb = -20.0f;
    double slowPow = 0.0, fastPow = 0.0;
    float slowCoef = 0.0f, fastCoef = 0.0f, glideCoef = 0.0f, maxStep = 0.0f;
    float gainDb = 0.0f;
    Smoother sOn;
    std::atomic<float> gainMeter { 0.0f };
};
}
