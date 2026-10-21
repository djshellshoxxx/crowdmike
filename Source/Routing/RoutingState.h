#pragma once
// Lock-free, persistent routing state shared by the GUI/control threads (writers) and the audio thread (reader).
// Three matrices (ROUTING_SPEC section 1-2):
//   direct : processed input channel -> output channel      (the first-release "routing matrix", frozen API)
//   assign : processed input channel -> crowd group         (send level)
//   bus    : group L/R, FX return L/R, playback L/R, master L/R -> output channel (row order: Limits.h, persisted layout)
// Writers use set*(); the engine polls revision() once per block and re-targets its smoothed copies.
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <cmath>
#include "Core/Limits.h"

namespace crowdmike {
class RoutingState final {
public:
    static constexpr int kIn = kMaxInputs, kGroups = kMaxGroups, kOut = kMaxOutputs, kBus = kBusSrcCount;

    RoutingState() { clearAll(); }

    // ---- direct (processed input -> output channel); index convention matches the legacy API: (input, output)
    float getDirect(int input, int output) const noexcept { return validDirect(input, output) ? direct[di(input, output)].load(std::memory_order_relaxed) : 0.0f; }
    bool setDirect(int input, int output, float gain) noexcept { return store(direct, validDirect(input, output), di(input, output), gain, -2.0f, 2.0f); }
    // ---- input -> group send level
    float getAssign(int input, int group) const noexcept { return validAssign(input, group) ? assign[static_cast<size_t>(group * kIn + input)].load(std::memory_order_relaxed) : 0.0f; }
    bool setAssign(int input, int group, float gain) noexcept { return store(assign, validAssign(input, group), static_cast<size_t>(group * kIn + input), gain, 0.0f, 2.0f); }
    // ---- bus row (see Limits.h kBusSrc*) -> output channel
    float getBus(int row, int output) const noexcept { return validBus(row, output) ? bus[static_cast<size_t>(output * kBus + row)].load(std::memory_order_relaxed) : 0.0f; }
    bool setBus(int row, int output, float gain) noexcept { return store(bus, validBus(row, output), static_cast<size_t>(output * kBus + row), gain, -2.0f, 2.0f); }

    void clearAll() noexcept;
    // First-release default: processed inputs pass straight to outputs (identity / fold-down), no groups, no bus routes.
    void resetDirectDefault(int inputChannels, int outputChannels) noexcept;
    bool hasSavedDirect() const noexcept { return savedDirect.load(std::memory_order_acquire); }
    void setHasSavedDirect(bool v) noexcept { savedDirect.store(v, std::memory_order_release); }

    std::uint32_t revision() const noexcept { return rev.load(std::memory_order_acquire); }
    void touch() noexcept { rev.fetch_add(1, std::memory_order_release); }

    juce::ValueTree toValueTree() const;
    // Returns false (leaving state untouched) when the tree is not a valid routing tree.
    bool fromValueTree(const juce::ValueTree&);

    static int busRowForGroup(int group, int channel) noexcept { return kBusSrcGroupBase + group * 2 + channel; }
    // FX slots >= kNumLegacyFx map to the appended rows (Limits.h); slots 0..4 keep their first-release rows.
    static int busRowForFx(int fx, int channel) noexcept { return busRowForFxReturn(fx, channel); }
    static int busRowForPlayback(int channel) noexcept { return kBusSrcPlayback + channel; }
    static int busRowForMaster(int channel) noexcept { return kBusSrcMaster + channel; }
    static juce::String busRowName(int row);

private:
    static constexpr size_t di(int input, int output) noexcept { return static_cast<size_t>(output * kIn + input); }
    static bool validDirect(int i, int o) noexcept { return i >= 0 && i < kIn && o >= 0 && o < kOut; }
    static bool validAssign(int i, int g) noexcept { return i >= 0 && i < kIn && g >= 0 && g < kGroups; }
    static bool validBus(int r, int o) noexcept { return r >= 0 && r < kBus && o >= 0 && o < kOut; }
    template <typename Array>
    bool store(Array& a, bool valid, size_t index, float gain, float lo, float hi) noexcept {
        if (!valid || !std::isfinite(gain)) return false;
        const float clamped = gain < lo ? lo : (gain > hi ? hi : gain);
        if (a[index].exchange(clamped, std::memory_order_relaxed) != clamped) touch();
        return true;
    }
    std::array<std::atomic<float>, kIn * kOut> direct {};
    std::array<std::atomic<float>, kIn * kGroups> assign {};
    std::array<std::atomic<float>, kBus * kOut> bus {};
    std::atomic<bool> savedDirect { false };
    std::atomic<std::uint32_t> rev { 0 };
};
}
