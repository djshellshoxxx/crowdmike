#pragma once
// Physical channel maps are acquired once at callback entry and stay stable for the complete callback.
#include "Core/SnapshotExchange.h"
#include <cstddef>

namespace crowdmike {
template <size_t Inputs, size_t Outputs>
class DeviceMapExchange {
public:
    struct Maps {
        std::array<int, Inputs> inputSource;     // engine input -> callback input index (-1 = disconnected)
        std::array<int, Outputs> outputTarget;  // engine output -> callback output index (-1 = disconnected)
    };

    DeviceMapExchange() noexcept : snapshots(disconnected()) {}
    void publish(const Maps& map) noexcept { snapshots.publish(map); }
    const Maps& acquire() noexcept { return snapshots.acquire(); }

private:
    static Maps disconnected() noexcept {
        Maps map;
        map.inputSource.fill(-1);
        map.outputTarget.fill(-1);
        return map;
    }
    SnapshotExchange<Maps> snapshots;
};
}
