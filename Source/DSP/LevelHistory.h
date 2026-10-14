#pragma once
// LevelHistory -- lock-free single-writer min/max/RMS history for waveform / level-history displays (GUI_SPEC 3, 4).
//
//  * prepare(sr, seconds, buckets) allocates a fixed ring (buckets + kSlack entries); nothing is allocated afterwards,
//    so memory is bounded regardless of run time (getMemoryBytes() is constant after prepare).
//  * push() (audio thread, single writer): noexcept, lock-free. Each bucket covers samplesPerBucket =
//    max(1, round(sr * seconds / buckets)) samples; a bucket is published (release) when complete.
//  * snapshot() (GUI thread, any number of readers) copies the newest completed buckets, oldest -> newest. Buckets not
//    yet written read as 0. The ring keeps kSlack spare entries so the writer can run ahead by kSlack buckets during a
//    copy without overwriting entries being read. If the requested count differs from the prepared bucket count the
//    history is resampled (min of mins, max of maxes, power-mean RMS).
//  * Non-finite samples are treated as 0.
#include "DspCommon.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace crowdmike::dsp {

class LevelHistory {
public:
    static constexpr int kSlack = 16;

    void prepare(double sampleRate, float seconds, int buckets);
    void reset() noexcept; // not concurrently with push()
    void push(const float* mono, int n) noexcept;
    // Writes `buckets` values into each non-null output, oldest -> newest.
    void snapshot(float* minOut, float* maxOut, float* rmsOut, int buckets) const;

    int getNumBuckets() const noexcept { return numBuckets; }
    int getSamplesPerBucket() const noexcept { return samplesPerBucket; }
    std::uint64_t getNumCompletedBuckets() const noexcept { return completed.load(std::memory_order_acquire); }
    std::size_t getMemoryBytes() const noexcept { return static_cast<std::size_t>(capacity) * 3u * sizeof(std::atomic<float>); }

private:
    std::unique_ptr<std::atomic<float>[]> mins, maxs, rmss;
    int numBuckets = 0, capacity = 0, samplesPerBucket = 1;
    std::atomic<std::uint64_t> completed { 0 };
    // writer state
    float curMin = 0.0f, curMax = 0.0f;
    double curSumSq = 0.0;
    int curCount = 0;
};

} // namespace crowdmike::dsp
