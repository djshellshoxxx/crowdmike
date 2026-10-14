#include "BusMatrix.h"
#include "juce_audio_basics/juce_audio_basics.h"

namespace crowdmike {
void BusMatrix::prepare(double sr, float rampMs) noexcept { for (auto& c : cells) { c.prepare(sr, rampMs); c.snap(0.0f); } }

void BusMatrix::syncTargets(const RoutingState& state, int numOut, float rampMs) noexcept {
    for (int o = 0; o < kMaxOutputs; ++o)
        for (int r = 0; r < kBusSrcCount; ++r)
            cells[static_cast<size_t>(o * kBusSrcCount + r)].setTarget(o < numOut ? state.getBus(r, o) : 0.0f, rampMs);
}

void BusMatrix::snapToTargets() noexcept { for (auto& c : cells) c.snap(c.targetValue()); }

void BusMatrix::process(const float* const* src, float* const* dst, int numOut, int n) noexcept {
    for (int o = 0; o < numOut; ++o) {
        float* out = dst[o];
        for (int r = 0; r < kBusSrcCount; ++r) {
            auto& cell = cells[static_cast<size_t>(o * kBusSrcCount + r)];
            const float* in = src[r];
            if (in == nullptr) { cell.skip(n); continue; }
            if (cell.isSmoothing()) {
                for (int i = 0; i < n; ++i) out[i] += in[i] * cell.next();
            } else if (cell.value() != 0.0f) {
                juce::FloatVectorOperations::addWithMultiply(out, in, cell.value(), n);
            }
        }
    }
}
}
