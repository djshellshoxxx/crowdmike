#pragma once
// MeterBank -- small header-only metering helpers (GUI_SPEC sections 3 and 4: peak/RMS meters, clip latch).
//
// PeakRmsMeter
//  * process() (audio thread, single writer): noexcept, lock-free, allocation-free.
//  * Peak = largest |x| since the last getAndResetPeak()/reset() (linear). Peak-hold decay/ballistics are the reader's job.
//  * RMS = sqrt of a one-pole average of x^2 (time constant from prepare, default 300 ms): a steady sine of amplitude A
//    reads A/sqrt(2).
//  * Non-finite samples are treated as 0. Getters read relaxed atomics.
// ClipLatch
//  * Latches when any |x| >= 1.0 (0 dBFS) or a sample is non-finite; stays set until reset() (operator action).
#include "DspCommon.h"
#include <atomic>

namespace crowdmike::dsp {

class PeakRmsMeter {
public:
    void prepare(double sampleRate, float rmsTimeMs = 300.0f) noexcept {
        coef = msToCoef(std::max(1.0f, rmsTimeMs), sampleRate > 0.0 ? sampleRate : 48000.0);
        reset();
    }
    void reset() noexcept {
        meanSq = 0.0f;
        peak.store(0.0f, std::memory_order_relaxed);
        rms.store(0.0f, std::memory_order_relaxed);
    }
    void process(const float* x, int n) noexcept {
        if (x == nullptr || n <= 0) return;
        float blockPeak = 0.0f, m = meanSq;
        const float ic = 1.0f - coef;
        for (int i = 0; i < n; ++i) {
            const float v = sanitize(x[i]);
            const float a = std::fabs(v);
            if (a > blockPeak) blockPeak = a;
            m = coef * m + ic * v * v;
        }
        if (m < 1.0e-20f) m = 0.0f;
        meanSq = m;
        rms.store(std::sqrt(m), std::memory_order_relaxed);
        float cur = peak.load(std::memory_order_relaxed);
        while (blockPeak > cur && !peak.compare_exchange_weak(cur, blockPeak, std::memory_order_relaxed)) {}
    }
    float getPeak() const noexcept { return peak.load(std::memory_order_relaxed); }
    float getAndResetPeak() noexcept { return peak.exchange(0.0f, std::memory_order_relaxed); }
    float getRms() const noexcept { return rms.load(std::memory_order_relaxed); }
    float getPeakDb() const noexcept { return gainToDb(getPeak()); }
    float getRmsDb() const noexcept { return gainToDb(getRms()); }

private:
    float coef = 0.9999f, meanSq = 0.0f;
    std::atomic<float> peak { 0.0f }, rms { 0.0f };
};

class ClipLatch {
public:
    void process(const float* x, int n) noexcept {
        if (x == nullptr || clipped.load(std::memory_order_relaxed)) return;
        for (int i = 0; i < n; ++i) {
            const float v = x[i];
            if (!std::isfinite(v) || std::fabs(v) >= 1.0f) {
                clipped.store(true, std::memory_order_relaxed);
                return;
            }
        }
    }
    void set() noexcept { clipped.store(true, std::memory_order_relaxed); }
    bool isClipped() const noexcept { return clipped.load(std::memory_order_relaxed); }
    void reset() noexcept { clipped.store(false, std::memory_order_relaxed); }

private:
    std::atomic<bool> clipped { false };
};

} // namespace crowdmike::dsp
