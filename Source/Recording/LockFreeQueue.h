#pragma once
// Small fixed-capacity lock-free queues shared by the recording / playback / soundcheck classes.
//
//  SpscQueue<T, N>     single producer / single consumer, wait-free, no allocation after construction.
//  CommandQueue<T, N>  many producers (any NON-audio thread, serialised by a mutex on the producer side only)
//                      and ONE lock-free consumer (normally the audio thread). The consumer never touches the mutex.
//
// T must be trivially copyable (commands and raw pointers). N must be a power of two.
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <type_traits>

namespace crowdmike::rec {

template <typename T, int N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0 && N >= 2, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    /** Producer side. Returns false (and drops nothing already queued) when full. */
    bool push(const T& v) noexcept {
        const auto w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) >= (std::uint32_t) N) return false;
        items[w & (N - 1)] = v;
        write.store(w + 1, std::memory_order_release);
        return true;
    }
    /** Consumer side. */
    bool pop(T& out) noexcept {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return false;
        out = items[r & (N - 1)];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
    /** Consumer side: copy of the oldest item without removing it. */
    bool peek(T& out) const noexcept {
        const auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire)) return false;
        out = items[r & (N - 1)];
        return true;
    }
    /** Approximate number of queued items (any thread). */
    int size() const noexcept {
        return (int) (write.load(std::memory_order_acquire) - read.load(std::memory_order_acquire));
    }
    bool isFull() const noexcept { return size() >= N; }
    static constexpr int capacity() noexcept { return N; }

private:
    std::array<T, N> items {};
    std::atomic<std::uint32_t> write { 0 }, read { 0 };
};

template <typename T, int N>
class CommandQueue {
public:
    /** Any non-audio thread. Blocks only other producers, never the consumer. */
    bool push(const T& v) {
        std::lock_guard<std::mutex> lock(producerMutex);
        return queue.push(v);
    }
    /** The single consumer thread (audio thread), lock-free. */
    bool pop(T& out) noexcept { return queue.pop(out); }

private:
    std::mutex producerMutex;
    SpscQueue<T, N> queue;
};

/** Consumer-side mutex variant: one lock-free producer (the audio thread) and any number of
    non-audio consumers serialised by a mutex. Used for deferred-release ("retire") queues. */
template <typename T, int N>
class RetireQueue {
public:
    bool push(const T& v) noexcept { return queue.push(v); }     // audio thread
    bool isFull() const noexcept { return queue.isFull(); }       // audio thread
    template <typename Fn>
    void drain(Fn&& fn) {                                         // any non-audio thread
        std::lock_guard<std::mutex> lock(consumerMutex);
        T v;
        while (queue.pop(v)) fn(v);
    }

private:
    std::mutex consumerMutex;
    SpscQueue<T, N> queue;
};

} // namespace crowdmike::rec
