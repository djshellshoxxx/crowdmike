#pragma once
// Shared helpers for the time-effect tests (DelayFxTest / ReverseChunkTest / NoiseWidthTest).
#include "Core/ParamTables.h"
#include "DSP/FxContext.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fxtest {
template <size_t N> std::vector<float> defaults(const crowdmike::params::Def (&defs)[N]) {
    std::vector<float> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = defs[i].def;
    return v;
}

struct Lcg {   // deterministic test noise
    uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f; }
};

// Runs a send/return effect (process(inL,inR,outL,outR,n,p,ctx)) over whole buffers in blocks of `block`.
template <class Fx>
void runStereo(Fx& fx, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& outL,
               std::vector<float>& outR, int block, const float* p, const crowdmike::dsp::FxContext& ctx) {
    const int total = static_cast<int>(inL.size());
    outL.assign(inL.size(), 0.0f);
    outR.assign(inR.size(), 0.0f);
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        fx.process(inL.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n, p, ctx);
    }
}

inline float maxAbs(const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX) {
    float m = 0.0f;
    for (size_t i = from; i < std::min(to, v.size()); ++i) m = std::max(m, std::fabs(v[i]));
    return m;
}
inline bool allFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
inline double energy(const std::vector<float>& v, size_t from, size_t to) {
    double e = 0.0;
    for (size_t i = from; i < std::min(to, v.size()); ++i) e += static_cast<double>(v[i]) * v[i];
    return e;
}
inline float maxJump(const std::vector<float>& v, size_t from = 1, size_t to = SIZE_MAX) {
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, v.size()); ++i) m = std::max(m, std::fabs(v[i] - v[i - 1]));
    return m;
}
inline float maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float m = 0.0f;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}
inline std::vector<float> sine(int n, double sr, double hz, float amp) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = amp * static_cast<float>(std::sin(2.0 * 3.141592653589793 * hz * i / sr));
    return v;
}
}
