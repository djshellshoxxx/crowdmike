#pragma once
// StreamPool: worker-thread read-ahead for long clips and beds (RECORDING_SPEC 9.1).
//
// A "ring" is a single-producer (streamer thread) / single-consumer (audio thread) stereo float ring buffer that holds
// the frames of one playback head IN PLAYBACK ORDER. The playback order ("sequence") of a head is defined by a region
// of the clip [regionStart, regionStart + regionLen), a direction (reverse) and a loop flag:
//     forward: seq q maps to source frame regionStart + (q mod regionLen)      (q < regionLen if !loop)
//     reverse: seq q maps to source frame regionStart + regionLen - 1 - (q mod regionLen)
// so seamless loops and reverse playback are properties of the producer; the consumer only ever reads sequentially.
//
// THREADS
//   Audio thread : acquire() / release() / ring access (noexcept, wait-free: scans a fixed array, atomics only).
//   Streamer     : internal juce::Thread polling every ~2 ms (no signalling from the audio thread), or pump() driven
//                  from a test. Owns all file readers.
//   Control      : registerSource() / unregisterSource() (mutex protected, never touched by the audio thread).
//
// UNDERRUNS: the audio thread publishes consumeSeq at each block start; if it reads past writeSeq it plays silence
// for the missing frames, the pool counts them, and the producer skips ahead to consumeSeq so that time stays in sync.
#include "StreamSource.h"
#include "LockFreeQueue.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace crowdmike::rec {

class StreamPool {
public:
    static constexpr int kRings = 16;
    static constexpr int kFillChunk = 4096;          // frames produced per ring per round
    static constexpr double kRingSeconds = 1.3;      // ring length target (rounded up to a power of two frames)

    enum State : int { Free = 0, Requested = 1, Active = 2, Releasing = 3 };

    struct Ring {
        juce::HeapBlock<float> L, R;
        int capacity = 0, mask = 0;
        std::atomic<int> state { Free };
        std::atomic<juce::uint64> writeSeq { 0 }, consumeSeq { 0 };
        std::atomic<bool> loop { false };
        // request (written by the audio thread before state = Requested, read by the streamer after acquire)
        juce::uint64 sourceId = 0;
        juce::int64 regionStart = 0, regionLen = 0;
        bool reverse = false;
        // streamer private
        StreamSource* src = nullptr;
    };

    StreamPool();
    ~StreamPool();

    //==== lifecycle (message thread, audio stopped) ==================================================================
    /** (Re)allocates the rings for this sample rate / block size. Stops the thread first. */
    void prepare(double sampleRate, int maxBlock);
    /** Starts the polling thread (startThread=false: tests drive pump()). */
    void start(bool startThread = true);
    void stop();
    bool hasThread() const noexcept { return thread != nullptr; }
    /** One streamer iteration: services requests/releases and refills rings. Safe from any non-audio thread. */
    void pump();

    //==== control =====================================================================================================
    juce::uint64 registerSource(const juce::File&);
    void unregisterSource(juce::uint64 id);

    //==== audio thread ===============================================================================================
    /** Claims a ring for a new head whose first needed sequence index is startSeq. -1 if none is free (counted). */
    int acquire(juce::uint64 sourceId, juce::int64 regionStart, juce::int64 regionLen, bool reverse, bool loop,
                juce::uint64 startSeq) noexcept;
    void release(int ring) noexcept;
    Ring& ring(int i) noexcept { return *rings[(size_t) i]; }
    void noteUnderrun(juce::uint64 frames) noexcept {
        underrunFrames.fetch_add(frames, std::memory_order_relaxed);
        underrunEvents.fetch_add(1, std::memory_order_relaxed);
    }

    //==== statistics (any thread) ====================================================================================
    struct Stats {
        juce::uint64 underrunEvents = 0, underrunFrames = 0, starvedAcquires = 0, readErrors = 0;
        int activeRings = 0, ringCount = 0;
        juce::int64 ringBytes = 0;
    };
    Stats getStats() const;
    int ringCapacity() const noexcept { return capacity; }
    void resetStats() noexcept { underrunEvents = underrunFrames = starvedAcquires = readErrors = 0; }

private:
    class Thread;
    bool fillOne(Ring&);
    StreamSource* sourceFor(juce::uint64 id);

    double sampleRate = 48000.0;
    int maxBlock = 512, capacity = 0;
    std::vector<std::unique_ptr<Ring>> rings;
    std::mutex pumpLock;
    std::mutex regLock;
    std::unordered_map<juce::uint64, juce::File> registry;
    std::atomic<bool> registryDirty { false };
    std::unordered_map<juce::uint64, std::unique_ptr<StreamSource>> sources;   // streamer-owned
    juce::uint64 nextId = 1;
    std::vector<float> scratch[2];
    std::atomic<juce::uint64> underrunEvents { 0 }, underrunFrames { 0 }, starvedAcquires { 0 }, readErrors { 0 };
    std::unique_ptr<Thread> thread;
    double engineRateForSources = 48000.0;
};

} // namespace crowdmike::rec
