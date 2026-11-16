#pragma once
// Bounded, allocation-free hand-over of short MIDI messages from the MIDI input threads to the standalone audio
// callback (IO_SPEC section 3, CONTROL_SPEC section 5). It replaces juce::MidiMessageCollector, whose
// removeNextBlockOfMessages() takes a lock shared with the MIDI input thread and can grow its MidiBuffer on the
// audio callback.
//
// Contract
//  * Producers: any number of threads (one per MIDI input device) call push(). push() never blocks or allocates.
//    It returns false and counts the event when the queue is full (droppedOverflow) or when the message is longer
//    than kMaxBytes (rejectedLong): SysEx and other long messages are filtered here, before the audio thread.
//  * Consumer: exactly one thread (the audio callback) calls drainInto(). It performs bounded lock-free work: at
//    most Capacity events are moved into the MidiBuffer, which must have been pre-sized with reserve() on a
//    non-audio thread, so the drain never allocates. A producer that has claimed a slot but not yet finished
//    writing it ends that drain early; its event is delivered by the next drain. Order is preserved per producer.
//  * reset(): consumer-role call while the audio device is stopped (audioDeviceAboutToStart). Discards queued
//    events and restarts the time base at the given sample rate.
//  * Timing follows juce::MidiMessageCollector: an event's block position is its timestamp relative to the previous
//    drain, with the elapsed wall-clock window aligned to end at the end of the current block (or compressed into
//    the block when the window is longer). Events are never deferred to a later block; they are clamped into it.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crowdmike {
template <size_t Capacity>
class MidiEventQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::atomic<size_t>::is_always_lock_free, "the MIDI event queue requires lock-free atomics");

public:
    static constexpr int kMaxBytes = 3;
    static constexpr size_t kCapacity = Capacity;
    // juce::MidiBuffer stores each event as sample position (int32) + size (uint16) + data bytes.
    static constexpr size_t kBytesPerBufferedEvent = sizeof(int32_t) + sizeof(uint16_t) + static_cast<size_t>(kMaxBytes);

    MidiEventQueue() noexcept {
        for (size_t i = 0; i < Capacity; ++i) cells[i].sequence.store(i, std::memory_order_relaxed);
    }

    /** Sizes `buffer` so that a full drain never allocates. Call from a non-audio thread before the first drain. */
    static void reserve(juce::MidiBuffer& buffer) { buffer.ensureSize(Capacity * kBytesPerBufferedEvent); }

    /** Consumer-role call while the audio callback is stopped: discards queued events, restarts the time base. */
    void reset(double newSampleRate) noexcept { reset(newSampleRate, nowSeconds()); }
    void reset(double newSampleRate, double nowSecondsValue) noexcept {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 0.0;
        lastDrainTime = nowSecondsValue;
        Event discarded;
        for (size_t i = 0; i < Capacity && dequeue(discarded); ++i) {}
    }

    /** Producer side (any thread). Returns false when the message is dropped: longer than kMaxBytes, or queue full.
        A zero timestamp is replaced by the current time (juce::MidiInput stamps real messages in seconds). */
    bool push(const juce::MidiMessage& message) noexcept {
        const int size = message.getRawDataSize();
        if (size <= 0 || size > kMaxBytes) { rejectedLong.fetch_add(1, std::memory_order_relaxed); return false; }
        Event e;
        const auto* raw = message.getRawData();
        for (int i = 0; i < size; ++i) e.bytes[static_cast<size_t>(i)] = raw[i];
        e.size = static_cast<uint8_t>(size);
        e.timeSeconds = message.getTimeStamp() > 0.0 ? message.getTimeStamp() : nowSeconds();
        if (enqueue(e)) return true;
        droppedOverflowCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /** Consumer side (audio thread). Moves every queued event into `buffer` at block-relative positions.
        Returns the number of events moved. `buffer` must have been reserve()d. */
    int drainInto(juce::MidiBuffer& buffer, int numSamples) noexcept { return drainInto(buffer, numSamples, nowSeconds()); }
    int drainInto(juce::MidiBuffer& buffer, int numSamples, double nowSecondsValue) noexcept {
        const double previous = lastDrainTime;
        lastDrainTime = nowSecondsValue;
        if (numSamples <= 0) return 0;
        const int numSourceSamples = juce::jmax(1, juce::roundToInt(clampSamples((nowSecondsValue - previous) * sampleRate)));
        int moved = 0;
        Event e;
        while (moved < static_cast<int>(Capacity) && dequeue(e)) {
            const auto sampleNumber = static_cast<int64_t>(std::llround(clampSamples((e.timeSeconds - previous) * sampleRate)));
            buffer.addEvent(e.bytes.data(), static_cast<int>(e.size), mapToBlock(sampleNumber, numSourceSamples, numSamples));
            ++moved;
        }
        return moved;
    }

    /** Pure mapping (unit-tested) of an event position relative to the previous drain into [0, numSamples):
        the elapsed window of `numSourceSamples` ends at the block end, or is compressed when longer than the block. */
    static int mapToBlock(int64_t sampleNumber, int numSourceSamples, int numSamples) noexcept {
        if (numSamples <= 0) return 0;
        numSourceSamples = juce::jmax(1, numSourceSamples);
        const int64_t pos = numSourceSamples > numSamples
                                ? sampleNumber * static_cast<int64_t>(numSamples) / static_cast<int64_t>(numSourceSamples)
                                : sampleNumber + static_cast<int64_t>(numSamples - numSourceSamples);
        return static_cast<int>(juce::jlimit<int64_t>(0, static_cast<int64_t>(numSamples) - 1, pos));
    }

    /** Events dropped because the queue was full (the audio device stopped, or a burst beyond Capacity). */
    uint32_t droppedOverflow() const noexcept { return droppedOverflowCount.load(std::memory_order_relaxed); }
    /** Messages rejected at the producer because they were longer than kMaxBytes (SysEx and similar). */
    uint32_t rejectedLongMessages() const noexcept { return rejectedLong.load(std::memory_order_relaxed); }

private:
    struct Event {
        std::array<uint8_t, kMaxBytes> bytes {};
        uint8_t size = 0;
        double timeSeconds = 0.0;
    };
    struct Cell {
        std::atomic<size_t> sequence { 0 };
        Event event;
    };
    static constexpr size_t mask = Capacity - 1;

    static double nowSeconds() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    // Keeps a corrupt timestamp from overflowing the integer sample arithmetic (about 2.9 h at 96 kHz is plenty).
    static double clampSamples(double samples) noexcept { return juce::jlimit(-1.0e9, 1.0e9, samples); }

    // Bounded multi-producer ring: a producer claims a slot by advancing enqueuePos, then publishes it by storing
    // the slot's sequence. The single consumer reads slots in order and hands each back with its next-lap sequence.
    bool enqueue(const Event& e) noexcept {
        size_t pos = enqueuePos.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = cells[pos & mask];
            const size_t seq = cell.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                if (enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.event = e;
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;   // the slot still holds an event the consumer has not taken: full
            } else {
                pos = enqueuePos.load(std::memory_order_relaxed);
            }
        }
    }
    bool dequeue(Event& out) noexcept {
        Cell& cell = cells[dequeuePos & mask];
        const size_t seq = cell.sequence.load(std::memory_order_acquire);
        if (seq != dequeuePos + 1) return false;   // empty, or a producer is still writing this slot
        out = cell.event;
        cell.sequence.store(dequeuePos + mask + 1, std::memory_order_release);
        ++dequeuePos;
        return true;
    }

    std::array<Cell, Capacity> cells;
    std::atomic<size_t> enqueuePos { 0 };
    size_t dequeuePos = 0;                // consumer-owned
    double sampleRate = 0.0, lastDrainTime = 0.0;   // consumer-owned
    std::atomic<uint32_t> droppedOverflowCount { 0 }, rejectedLong { 0 };
};

// Capacity used by the standalone: more than a second of dense controller traffic between callbacks.
using StandaloneMidiQueue = MidiEventQueue<1024>;
}
