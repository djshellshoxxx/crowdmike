#pragma once
// FxUtil.h -- small private building blocks for the time-based effects (DigitalDelay, EchoThrow, TapeDelay,
// ReverseChunk, NoiseEnhancer, WidthProcessor). Deliberately self-contained (namespace crowdmike::dsp::fx, names
// prefixed "Fx") so it never collides with the general filter modules (Biquad.h, ChannelDelay.h ...).
//
// Invariants:
// * Nothing here allocates except FxDelayBuffer::allocate() / FxAllpass::allocate() (call from prepare()).
// * FxBiquad coefficient designs clamp the cutoff to [10 Hz, 0.45 * sr] and are stable for any finite input.
//   Butterworth (Q = 1/sqrt 2) low/high-pass designs have |H| <= 1 at every frequency (used for loop-gain proofs).
// * FxDelayBuffer::clear() is O(1): reads older than the samples written since clear() return exactly 0, so stale
//   memory is never audible and reset() stays deterministic without a large memset on the audio thread.
// * FxRng is a deterministic xorshift32; identical seed => identical stream.
#include "DspCommon.h"
#include <cstdint>
#include <vector>

namespace crowdmike::dsp::fx {

inline float fxFlush(float v) noexcept { return std::fabs(v) < 1.0e-24f ? 0.0f : v; }

inline int fxNextPow2(int v) noexcept {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

// Soft saturation with slope 1 below |x| = knee and an asymptote at 2 * knee. |f(x)| <= |x| and |f(x)| < 2 * knee.
inline float fxSoftLimit(float x, float knee) noexcept {
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float y = knee + knee * std::tanh((a - knee) / knee);
    return x < 0.0f ? -y : y;
}

// ------------------------------------------------------------------------------------------------ biquad
struct FxBiquadCoeffs { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };

inline float fxClampHz(double sr, float hz) noexcept {
    return clampf(std::isfinite(hz) ? hz : 1000.0f, 10.0f, static_cast<float>(0.45 * sr));
}

inline FxBiquadCoeffs fxDesignLowpass(double sr, float hz, float q = 0.70710678f) noexcept {
    const double w0 = 2.0 * 3.14159265358979323846 * fxClampHz(sr, hz) / sr;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    FxBiquadCoeffs k;
    k.b0 = static_cast<float>((1.0 - c) * 0.5 / a0);
    k.b1 = static_cast<float>((1.0 - c) / a0);
    k.b2 = k.b0;
    k.a1 = static_cast<float>(-2.0 * c / a0);
    k.a2 = static_cast<float>((1.0 - alpha) / a0);
    return k;
}

inline FxBiquadCoeffs fxDesignHighpass(double sr, float hz, float q = 0.70710678f) noexcept {
    const double w0 = 2.0 * 3.14159265358979323846 * fxClampHz(sr, hz) / sr;
    const double c = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    FxBiquadCoeffs k;
    k.b0 = static_cast<float>((1.0 + c) * 0.5 / a0);
    k.b1 = static_cast<float>(-(1.0 + c) / a0);
    k.b2 = k.b0;
    k.a1 = static_cast<float>(-2.0 * c / a0);
    k.a2 = static_cast<float>((1.0 - alpha) / a0);
    return k;
}

// Transposed direct form II.
class FxBiquad {
public:
    void setCoeffs(const FxBiquadCoeffs& c) noexcept { k = c; }
    void reset() noexcept { s1 = s2 = 0.0f; }
    float process(float x) noexcept {
        const float y = k.b0 * x + s1;
        s1 = fxFlush(k.b1 * x - k.a1 * y + s2);
        s2 = fxFlush(k.b2 * x - k.a2 * y);
        return y;
    }
private:
    FxBiquadCoeffs k;
    float s1 = 0.0f, s2 = 0.0f;
};

// ------------------------------------------------------------------------------------------------ one-pole (TPT)
class FxOnePole {
public:
    void setCutoff(double sr, float hz) noexcept {
        const double g = std::tan(3.14159265358979323846 * fxClampHz(sr, hz) / sr);
        G = static_cast<float>(g / (1.0 + g));
    }
    void reset() noexcept { s = 0.0f; }
    float lowpass(float x) noexcept {
        const float v = (x - s) * G;
        const float lp = v + s;
        s = fxFlush(lp + v);
        return lp;
    }
    float highpass(float x) noexcept { return x - lowpass(x); }
private:
    float G = 0.5f, s = 0.0f;
};

// ------------------------------------------------------------------------------------------------ delay buffer
// Mono ring buffer, power-of-two capacity. Convention: read(d) after push() of sample n-1 returns sample n-d.
class FxDelayBuffer {
public:
    void allocate(int minCapacity) {
        size = fxNextPow2(std::max(4, minCapacity));
        mask = size - 1;
        data.assign(static_cast<size_t>(size), 0.0f);
        clear();
    }
    void clear() noexcept { w = 0; filled = 0; }
    void push(float x) noexcept {
        data[static_cast<size_t>(w)] = x;
        w = (w + 1) & mask;
        if (filled < size) ++filled;
    }
    float read(int d) const noexcept {
        if (d < 1 || d > filled) return 0.0f;
        return data[static_cast<size_t>((w - d) & mask)];
    }
    // Linear interpolation (|H| <= 1 for every fractional delay, so it never adds loop gain).
    float readLinear(float d) const noexcept {
        const float fl = std::floor(d);
        const int i = static_cast<int>(fl);
        const float f = d - fl;
        return read(i) + f * (read(i + 1) - read(i));
    }
    int capacity() const noexcept { return size; }
private:
    std::vector<float> data;
    int size = 0, mask = 0, w = 0, filled = 0;
};

// Schroeder allpass  w[n] = x[n] + g w[n-M];  y[n] = -g w[n] + w[n-M].   |H| == 1 for |g| < 1.
class FxAllpass {
public:
    void allocate(int len) { buf.assign(static_cast<size_t>(std::max(1, len)), 0.0f); pos = 0; }
    void reset() noexcept { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; }
    float process(float x, float g) noexcept {
        const float wm = buf[static_cast<size_t>(pos)];
        const float wn = fxFlush(x + g * wm);
        buf[static_cast<size_t>(pos)] = wn;
        if (++pos >= static_cast<int>(buf.size())) pos = 0;
        return wm - g * wn;
    }
private:
    std::vector<float> buf;
    int pos = 0;
};

// ------------------------------------------------------------------------------------------------ RNG
inline uint32_t fxHashSeed(uint32_t a, uint32_t b) noexcept {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h != 0u ? h : 0x1234567u;
}

struct FxRng {
    uint32_t s = 0x2545F491u;
    void seed(uint32_t v) noexcept { s = v != 0u ? v : 0x2545F491u; }
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    // Uniform in [-1, 1).
    float bipolar() noexcept { return static_cast<float>(static_cast<int32_t>(next())) * (1.0f / 2147483648.0f); }
};

// Sanitised parameter read: tolerates nullptr and non-finite values, clamps to [lo, hi].
inline float fxParam(const float* p, int idx, float def, float lo, float hi) noexcept {
    const float v = p != nullptr ? p[idx] : def;
    return clampf(std::isfinite(v) ? v : def, lo, hi);
}
inline int fxChoice(const float* p, int idx, int def, int lo, int hi) noexcept {
    return static_cast<int>(std::lround(fxParam(p, idx, static_cast<float>(def), static_cast<float>(lo), static_cast<float>(hi))));
}
inline bool fxBool(const float* p, int idx, bool def) noexcept { return fxParam(p, idx, def ? 1.0f : 0.0f, 0.0f, 1.0f) > 0.5f; }
}
