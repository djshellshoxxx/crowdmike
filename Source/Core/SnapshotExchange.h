#pragma once
// Bounded SPSC snapshots for trivially copyable state. One control-thread producer publishes complete values; one
// audio-thread consumer acquires once per block. The returned reference remains valid until that consumer's next
// acquire(). Three slots have exclusive producer, consumer and exchange ownership: repeated publications can replace
// the pending snapshot, but never overwrite the snapshot held by the callback. No locks, allocations or retry loops.
// Startup may publish from another thread only while the normal producer and consumer are stopped.
#include <array>
#include <atomic>
#include <type_traits>

namespace crowdmike {
template <typename T>
class SnapshotExchange {
public:
    static_assert(std::is_trivially_copyable_v<T>, "snapshots must be trivially copyable");
    static_assert(std::atomic<unsigned>::is_always_lock_free, "snapshot exchange requires lock-free atomics");

    explicit SnapshotExchange(const T& initial = {}) noexcept : slots { initial, initial, initial } {}

    void publish(const T& value) noexcept {
        slots[producer] = value;
        producer = exchange.exchange(producer | dirty, std::memory_order_acq_rel) & indexMask;
    }

    const T& acquire() noexcept {
        if ((exchange.load(std::memory_order_acquire) & dirty) != 0)
            consumer = exchange.exchange(consumer, std::memory_order_acq_rel) & indexMask;
        return slots[consumer];
    }

private:
    static constexpr unsigned dirty = 4, indexMask = 3;
    std::array<T, 3> slots;
    unsigned producer = 0, consumer = 1;
    std::atomic<unsigned> exchange { 2 };
};
}
