#pragma once
// Internal helpers shared by the filter/dynamics modules (header-only, no JUCE dependency).
// Invariants:
//  * cleanSample(): non-finite -> 0, finite values clamped to +/-kMaxSample (+120 dBFS) so squares never overflow.
//  * readParam(): nullptr or non-finite parameter -> default; result clamped to [lo, hi].
//  * cleanBuffer() leaves finite in-range samples bit-identical (used by bit-exact bypass paths).
#include "DspCommon.h"
#include <cstdint>
#include <cstring>
#include <functional>

namespace crowdmike::dsp::util {
constexpr float kMaxSample = 1.0e6f;

inline bool isFiniteBits(float x) noexcept {
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}
// Exact float equality without -Wfloat-equal noise (used for "settled at 0/1" and "parameter changed" checks).
inline bool same(float a, float b) noexcept { return std::equal_to<float>()(a, b); }
inline float cleanSample(float x) noexcept {
    if (!isFiniteBits(x)) return 0.0f;
    return x > kMaxSample ? kMaxSample : (x < -kMaxSample ? -kMaxSample : x);
}
inline void cleanBuffer(float* d, int n) noexcept {
    for (int i = 0; i < n; ++i) d[i] = cleanSample(d[i]);
}
inline float readParam(const float* p, int idx, float def, float lo, float hi) noexcept {
    if (p == nullptr) return def;
    const float v = p[idx];
    if (!isFiniteBits(v)) return def;
    return clampf(v, lo, hi);
}
inline bool readBool(const float* p, int idx, bool def) noexcept {
    if (p == nullptr || !isFiniteBits(p[idx])) return def;
    return p[idx] > 0.5f;
}
inline int readChoice(const float* p, int idx, int def, int lo, int hi) noexcept {
    if (p == nullptr || !isFiniteBits(p[idx])) return def;
    const int v = static_cast<int>(std::lround(p[idx]));
    return v < lo ? lo : (v > hi ? hi : v);
}
// Flush tiny filter state so decays never reach the denormal range (cheap, called once per block).
inline double flushTiny(double v) noexcept { return (v > -1.0e-25 && v < 1.0e-25) ? 0.0 : v; }
inline float flushTiny(float v) noexcept { return (v > -1.0e-25f && v < 1.0e-25f) ? 0.0f : v; }
}
