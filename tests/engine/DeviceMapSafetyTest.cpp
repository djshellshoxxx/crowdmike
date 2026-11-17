#include "TestHarness.h"
#include "Standalone/DeviceMapExchange.h"
#include <atomic>
#include <thread>

namespace {
using Exchange = crowdmike::DeviceMapExchange<16, 16>;
Exchange::Maps mapFor(int generation) {
    Exchange::Maps result;
    for (int channel = 0; channel < 16; ++channel) {
        result.inputSource[static_cast<size_t>(channel)] = generation;
        result.outputTarget[static_cast<size_t>(channel)] = -generation;
    }
    return result;
}
bool matches(const Exchange::Maps& map, int generation) {
    for (int channel = 0; channel < 16; ++channel)
        if (map.inputSource[static_cast<size_t>(channel)] != generation
            || map.outputTarget[static_cast<size_t>(channel)] != -generation) return false;
    return true;
}
}

CM_TEST(device_maps_callback_snapshot_survives_repeated_publication) {
    Exchange exchange;
    const auto& initial = exchange.acquire();
    for (int channel = 0; channel < 16; ++channel) {
        CM_EXPECT(initial.inputSource[static_cast<size_t>(channel)] == -1);
        CM_EXPECT(initial.outputTarget[static_cast<size_t>(channel)] == -1);
    }
    exchange.publish(mapFor(1));
    const auto& callbackMap = exchange.acquire();
    CM_EXPECT(matches(callbackMap, 1));
    for (int generation = 2; generation < 20; ++generation) exchange.publish(mapFor(generation));
    CM_EXPECT(matches(callbackMap, 1));    // the in-flight audio callback still owns its complete map
    CM_EXPECT(matches(exchange.acquire(), 19));
    CM_EXPECT(matches(exchange.acquire(), 19));   // unchanged when no update is pending
}

CM_TEST(device_maps_concurrent_callback_snapshots_are_coherent) {
    Exchange exchange;
    exchange.publish(mapFor(0));
    std::atomic<bool> start { false }, done { false }, coherent { true };
    std::thread publisher([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (int generation = 1; generation <= 100000; ++generation) exchange.publish(mapFor(generation));
        done.store(true, std::memory_order_release);
    });
    start.store(true, std::memory_order_release);
    do {
        const auto& callbackMap = exchange.acquire();
        const int generation = callbackMap.inputSource[0];
        for (int sample = 0; sample < 16; ++sample)
            if (!matches(callbackMap, generation)) coherent.store(false, std::memory_order_relaxed);
    } while (!done.load(std::memory_order_acquire));
    publisher.join();
    CM_EXPECT(coherent.load());
    CM_EXPECT(matches(exchange.acquire(), 100000));
}
