#pragma once
// CorrelationMeter -- stereo phase correlation and width estimate for the GUI (GUI_SPEC sections 3 and 4).
//
//  * process() (audio thread): noexcept, allocation/lock-free. Per-sample one-pole averages (time constant set in
//    prepare, default 300 ms) of L*L, R*R and L*R. Results are published to relaxed atomics at the end of each call.
//  * correlation = <LR> / sqrt(<LL><RR>) in [-1, +1]: identical channels +1, inverted -1, uncorrelated ~0.
//    Reads 0 when the signal is below ~-100 dBFS (silence).
//  * width = <S^2> / (<M^2> + <S^2>) in [0, 1] with M = (L+R)/2, S = (L-R)/2: 0 mono, 0.5 uncorrelated/wide,
//    1 fully out of phase. getSideToMidDb() gives the same as a S/M energy ratio in dB (clamped to +-60 dB).
//  * Non-finite samples are treated as 0. reset() is not to be called concurrently with process().
#include "DspCommon.h"
#include <atomic>

namespace crowdmike::dsp {

class CorrelationMeter {
public:
    void prepare(double sampleRate, float timeConstantMs = 300.0f) noexcept;
    void reset() noexcept;
    void process(const float* l, const float* r, int n) noexcept;

    float getCorrelation() const noexcept { return correlation.load(std::memory_order_relaxed); }
    float getWidth() const noexcept { return width.load(std::memory_order_relaxed); }
    float getSideToMidDb() const noexcept { return sideToMidDb.load(std::memory_order_relaxed); }

private:
    float coef = 0.999f;
    double ll = 0.0, rr = 0.0, lr = 0.0;
    std::atomic<float> correlation { 0.0f }, width { 0.0f }, sideToMidDb { -60.0f };
};

} // namespace crowdmike::dsp
