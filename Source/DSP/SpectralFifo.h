#pragma once
// Private plumbing shared by the analysis modules (FeedbackDetector, SpectrumAnalyzer).
//
// SampleFifo: single-producer / single-consumer lock-free sample FIFO.
//   * push() (audio thread) never blocks, never allocates and always succeeds: when the consumer falls behind, the
//     OLDEST unread samples are overwritten ("drop oldest"). The consumer detects the overrun and skips past it.
//   * Storage is std::atomic<float> with relaxed access so a concurrent overwrite is a well-defined (if stale) read,
//     not a data race. The producer publishes in chunks of at most `guard` samples, and the consumer only trusts
//     indices that are at least `guard` samples away from the write head, so samples it keeps are never torn.
//   * Indices are monotonically increasing 64-bit counters (no wrap in practice).
//
// Also: Hann window and log-frequency display-bin mapping helpers.
#include "DspCommon.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace crowdmike::dsp::detail {

class SampleFifo {
public:
    // Capacity is rounded up to a power of two so that (capacity - guard) >= minSamples.
    void prepare(int minSamples) {
        int cap = 1024;
        while (cap - cap / 4 < minSamples) cap <<= 1;
        capacity = cap;
        mask = static_cast<std::uint64_t>(cap - 1);
        guard = cap / 4;
        buffer = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(cap));
        for (int i = 0; i < cap; ++i) buffer[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
        writeCount.store(0, std::memory_order_relaxed);
        readCount = 0;
        dropped.store(0, std::memory_order_relaxed);
    }

    bool isPrepared() const noexcept { return buffer != nullptr; }
    int getCapacity() const noexcept { return capacity; }
    // Largest backlog the consumer can hold before the oldest samples are dropped.
    int getUsableCapacity() const noexcept { return capacity - guard; }

    // Producer (audio thread). Non-finite samples are stored as 0.
    void push(const float* x, int n) noexcept {
        if (buffer == nullptr || x == nullptr || n <= 0) return;
        if (n > capacity - guard) { // only the newest samples can survive anyway
            x += n - (capacity - guard);
            n = capacity - guard;
        }
        std::uint64_t w = writeCount.load(std::memory_order_relaxed);
        while (n > 0) {
            const int chunk = std::min(n, guard);
            for (int i = 0; i < chunk; ++i)
                buffer[static_cast<size_t>((w + static_cast<std::uint64_t>(i)) & mask)].store(sanitize(x[i]),
                                                                                              std::memory_order_relaxed);
            w += static_cast<std::uint64_t>(chunk);
            writeCount.store(w, std::memory_order_release);
            x += chunk;
            n -= chunk;
        }
    }

    // Consumer. Number of readable samples (after accounting for overrun).
    int getNumReady() noexcept {
        const std::uint64_t w = writeCount.load(std::memory_order_acquire);
        catchUp(w);
        return static_cast<int>(w - readCount);
    }

    // Consumer: discard n of the oldest readable samples.
    void skip(int n) noexcept {
        const std::uint64_t w = writeCount.load(std::memory_order_acquire);
        catchUp(w);
        readCount += std::min<std::uint64_t>(static_cast<std::uint64_t>(std::max(0, n)), w - readCount);
    }

    // Consumer: copies up to maxN of the oldest valid samples, returns the number copied.
    int pop(float* out, int maxN) noexcept {
        if (buffer == nullptr || maxN <= 0) return 0;
        for (;;) {
            const std::uint64_t w = writeCount.load(std::memory_order_acquire);
            catchUp(w);
            const int n = static_cast<int>(std::min<std::uint64_t>(w - readCount, static_cast<std::uint64_t>(maxN)));
            if (n == 0) return 0;
            for (int i = 0; i < n; ++i)
                out[i] = buffer[static_cast<size_t>((readCount + static_cast<std::uint64_t>(i)) & mask)].load(
                    std::memory_order_relaxed);
            // Validate: anything older than (w2 - usable) may have been overwritten meanwhile.
            const std::uint64_t w2 = writeCount.load(std::memory_order_acquire);
            const std::uint64_t usable = static_cast<std::uint64_t>(capacity - guard);
            const std::uint64_t oldestSafe = w2 > usable ? w2 - usable : 0;
            if (readCount >= oldestSafe) {
                readCount += static_cast<std::uint64_t>(n);
                return n;
            }
            // Overrun during the copy: drop what was overwritten and retry (bounded: producer is finite per call).
            dropped.fetch_add(oldestSafe - readCount, std::memory_order_relaxed);
            readCount = oldestSafe;
        }
    }

    // Consumer side: discard everything currently queued.
    void clear() noexcept { readCount = writeCount.load(std::memory_order_acquire); }

    std::uint64_t getDroppedSamples() const noexcept { return dropped.load(std::memory_order_relaxed); }
    void resetDroppedCount() noexcept { dropped.store(0, std::memory_order_relaxed); }

private:
    void catchUp(std::uint64_t w) noexcept {
        const std::uint64_t usable = static_cast<std::uint64_t>(capacity - guard);
        if (w - readCount > usable) {
            dropped.fetch_add(w - readCount - usable, std::memory_order_relaxed);
            readCount = w - usable;
        }
    }

    std::unique_ptr<std::atomic<float>[]> buffer;
    int capacity = 0, guard = 0;
    std::uint64_t mask = 0;
    std::atomic<std::uint64_t> writeCount { 0 };
    std::uint64_t readCount = 0; // consumer-owned
    std::atomic<std::uint64_t> dropped { 0 };
};

// Periodic Hann window (exact 50 % overlap-add property).
inline void makeHann(std::vector<float>& w, int n) {
    w.assign(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        w[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(n));
}

// Maps a linear FFT spectrum in dB (bins 0..numFftBins-1, spacing binHz) onto numOut log-spaced display bins
// between loHz and hiHz. Display bins wider than an FFT bin take the max (peaks stay visible); narrower ones
// interpolate linearly at the bin's geometric centre. Bins beyond Nyquist read floorDb.
inline void mapToLogBins(const float* binDb, int numFftBins, double binHz, float* out, int numOut, float loHz,
                         float hiHz, float floorDb) noexcept {
    if (out == nullptr || numOut <= 0) return;
    if (binDb == nullptr || numFftBins < 2 || binHz <= 0.0) {
        for (int i = 0; i < numOut; ++i) out[i] = floorDb;
        return;
    }
    const double ratio = static_cast<double>(hiHz) / static_cast<double>(loHz);
    const double lastBin = static_cast<double>(numFftBins - 1);
    for (int i = 0; i < numOut; ++i) {
        const double fa = loHz * std::pow(ratio, static_cast<double>(i) / numOut);
        const double fb = loHz * std::pow(ratio, static_cast<double>(i + 1) / numOut);
        const double ka = fa / binHz, kb = fb / binHz;
        if (ka >= lastBin) { out[i] = floorDb; continue; }
        const int k0 = static_cast<int>(std::ceil(ka));
        const int k1 = std::min(static_cast<int>(std::floor(kb)), numFftBins - 1);
        float v;
        if (k1 >= k0) {
            v = binDb[k0];
            for (int k = k0 + 1; k <= k1; ++k) v = std::max(v, binDb[k]);
        } else {
            const double kc = std::min(std::sqrt(fa * fb) / binHz, lastBin);
            const int kl = static_cast<int>(std::floor(kc));
            const int kr = std::min(kl + 1, numFftBins - 1);
            const float t = static_cast<float>(kc - kl);
            v = binDb[kl] + (binDb[kr] - binDb[kl]) * t;
        }
        out[i] = std::max(v, floorDb);
    }
}

// Parabolic interpolation of a peak at index k from three (log-magnitude) values. Returns the offset in [-0.5, 0.5].
inline float parabolicOffset(float a, float b, float c) noexcept {
    const float den = a - 2.0f * b + c;
    if (!(std::fabs(den) > 1.0e-12f)) return 0.0f;
    return clampf(0.5f * (a - c) / den, -0.5f, 0.5f);
}

} // namespace crowdmike::dsp::detail
