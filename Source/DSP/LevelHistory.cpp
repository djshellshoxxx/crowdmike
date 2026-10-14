#include "LevelHistory.h"
#include <cmath>

namespace crowdmike::dsp {

void LevelHistory::prepare(double sampleRate, float seconds, int buckets) {
    numBuckets = std::max(1, buckets);
    capacity = numBuckets + kSlack;
    const double total = std::max(1.0e-3, static_cast<double>(seconds)) * std::max(1.0, sampleRate);
    samplesPerBucket = std::max(1, static_cast<int>(std::lround(total / numBuckets)));
    mins = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(capacity));
    maxs = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(capacity));
    rmss = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(capacity));
    reset();
}

void LevelHistory::reset() noexcept {
    for (int i = 0; i < capacity; ++i) {
        mins[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
        maxs[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
        rmss[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
    }
    curMin = curMax = 0.0f;
    curSumSq = 0.0;
    curCount = 0;
    completed.store(0, std::memory_order_release);
}

void LevelHistory::push(const float* mono, int n) noexcept {
    if (mono == nullptr || capacity == 0) return;
    std::uint64_t done = completed.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        const float x = sanitize(mono[i]);
        if (curCount == 0) curMin = curMax = x;
        else {
            curMin = std::min(curMin, x);
            curMax = std::max(curMax, x);
        }
        curSumSq += static_cast<double>(x) * x;
        if (++curCount >= samplesPerBucket) {
            const size_t slot = static_cast<size_t>(done % static_cast<std::uint64_t>(capacity));
            mins[slot].store(curMin, std::memory_order_relaxed);
            maxs[slot].store(curMax, std::memory_order_relaxed);
            rmss[slot].store(static_cast<float>(std::sqrt(curSumSq / curCount)), std::memory_order_relaxed);
            completed.store(++done, std::memory_order_release);
            curCount = 0;
            curSumSq = 0.0;
        }
    }
}

void LevelHistory::snapshot(float* minOut, float* maxOut, float* rmsOut, int buckets) const {
    if (buckets <= 0) return;
    if (capacity == 0) {
        for (int i = 0; i < buckets; ++i) {
            if (minOut) minOut[i] = 0.0f;
            if (maxOut) maxOut[i] = 0.0f;
            if (rmsOut) rmsOut[i] = 0.0f;
        }
        return;
    }
    const std::uint64_t done = completed.load(std::memory_order_acquire);
    const std::uint64_t nb = static_cast<std::uint64_t>(numBuckets);
    // Source bucket j (0 = oldest of the visible window) maps to absolute index done - nb + j.
    auto readBucket = [&](std::uint64_t j, float& mn, float& mx, float& rm) {
        if (done + j < nb) { mn = mx = rm = 0.0f; return; } // not written yet
        const std::uint64_t abs = done + j - nb;
        const size_t slot = static_cast<size_t>(abs % static_cast<std::uint64_t>(capacity));
        mn = mins[slot].load(std::memory_order_relaxed);
        mx = maxs[slot].load(std::memory_order_relaxed);
        rm = rmss[slot].load(std::memory_order_relaxed);
    };
    for (int i = 0; i < buckets; ++i) {
        // Output bucket i covers source buckets [j0, j1).
        std::uint64_t j0 = static_cast<std::uint64_t>(i) * nb / static_cast<std::uint64_t>(buckets);
        std::uint64_t j1 = static_cast<std::uint64_t>(i + 1) * nb / static_cast<std::uint64_t>(buckets);
        if (j1 <= j0) j1 = j0 + 1; // upsampling: repeat
        float mn = 0.0f, mx = 0.0f;
        double pw = 0.0;
        for (std::uint64_t j = j0; j < j1; ++j) {
            float a, b, r;
            readBucket(j, a, b, r);
            if (j == j0) { mn = a; mx = b; }
            else { mn = std::min(mn, a); mx = std::max(mx, b); }
            pw += static_cast<double>(r) * r;
        }
        if (minOut) minOut[i] = mn;
        if (maxOut) maxOut[i] = mx;
        if (rmsOut) rmsOut[i] = static_cast<float>(std::sqrt(pw / static_cast<double>(j1 - j0)));
    }
}

} // namespace crowdmike::dsp
