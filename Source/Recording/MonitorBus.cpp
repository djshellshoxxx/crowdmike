#include "MonitorBus.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::rec {

void MonitorBus::prepare(double, int maxBlock) {
    accL.assign((size_t) std::max(1, maxBlock), 0.0f);
    accR.assign((size_t) std::max(1, maxBlock), 0.0f);
    appliedGain = std::pow(10.0f, gainDb.load() * 0.05f);
}

void MonitorBus::setGainDb(float db) noexcept {
    gainDb.store(std::isfinite(db) ? juce::jlimit(kMinGainDb, kMaxGainDb, db) : 0.0f, std::memory_order_relaxed);
}

void MonitorBus::setTrackMonitored(int track, bool on) noexcept {
    if ((unsigned) track >= (unsigned) kMaxTracks) return;
    const std::uint32_t bit = 1u << track;
    if (on) mask.fetch_or(bit, std::memory_order_relaxed);
    else mask.fetch_and(~bit, std::memory_order_relaxed);
}

bool MonitorBus::isTrackMonitored(int track) const noexcept {
    return (unsigned) track < (unsigned) kMaxTracks && (mask.load(std::memory_order_relaxed) >> track & 1u) != 0;
}

void MonitorBus::beginBlock(int n) noexcept {
    n = std::min(n, (int) accL.size());
    std::fill(accL.begin(), accL.begin() + n, 0.0f);
    std::fill(accR.begin(), accR.begin() + n, 0.0f);
}

void MonitorBus::addSource(const float* l, const float* r, int n) noexcept {
    if (l == nullptr) return;
    n = std::min(n, (int) accL.size());
    if (r == nullptr) r = l;
    juce::FloatVectorOperations::add(accL.data(), l, n);
    juce::FloatVectorOperations::add(accR.data(), r, n);
}

void MonitorBus::mixInto(float* outL, float* outR, int n) noexcept {
    n = std::min(n, (int) accL.size());
    const float target = std::pow(10.0f, gainDb.load(std::memory_order_relaxed) * 0.05f);
    const float step = (target - appliedGain) / (float) std::max(1, n);
    float g = appliedGain;
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) {
        g += step;
        const float l = accL[(size_t) i] * g, r = accR[(size_t) i] * g;
        if (outL != nullptr) { outL[i] = juce::jlimit(-1.0f, 1.0f, outL[i] + l); pk = std::max(pk, std::abs(l)); }
        if (outR != nullptr) { outR[i] = juce::jlimit(-1.0f, 1.0f, outR[i] + r); pk = std::max(pk, std::abs(r)); }
    }
    appliedGain = target;
    if (pk > peak.load(std::memory_order_relaxed)) peak.store(pk, std::memory_order_relaxed);
}

} // namespace crowdmike::rec
