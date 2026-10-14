#pragma once
// Format-neutral DSP helpers. No JUCE dependency so DSP modules compile into light unit tests.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crowdmike::dsp {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) noexcept { return 20.0f * std::log10(std::max(g, 1.0e-9f)); }
inline float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float sanitize(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
// Treats the lowest value of dB-range send/level controls (<= -79.9 dB) as exactly zero gain.
inline float dbToGainOrZero(float db, float floorDb = -79.9f) noexcept { return db <= floorDb ? 0.0f : dbToGain(db); }
// One-pole coefficient for a time constant in milliseconds.
inline float msToCoef(float ms, double sampleRate) noexcept {
    return ms <= 0.0f ? 0.0f : std::exp(-1.0f / (0.001f * ms * static_cast<float>(sampleRate)));
}

// Linear-ramp smoother with a runtime-settable ramp length (scene recalls use longer ramps).
// Never jumps: setTarget() keeps the current value and recomputes the per-sample step.
class Smoother {
public:
    void prepare(double sr, float defaultRampMs) noexcept { sampleRate = sr; defaultRamp = defaultRampMs; }
    void snap(float v) noexcept { current = target = v; step = 0.0f; remaining = 0; }
    void setTarget(float t, float rampMs = -1.0f) noexcept {
        if (t == target) return;
        target = t;
        const float ms = rampMs > 0.0f ? rampMs : defaultRamp;
        remaining = std::max(1, static_cast<int>(ms * 0.001 * sampleRate));
        step = (target - current) / static_cast<float>(remaining);
    }
    float next() noexcept {
        if (remaining > 0) {
            current += step;
            if (--remaining == 0) current = target;
        }
        return current;
    }
    float value() const noexcept { return current; }
    float targetValue() const noexcept { return target; }
    bool isSmoothing() const noexcept { return remaining > 0; }
    // Advance n samples at once (for control-rate updates) and return the new value.
    float skip(int n) noexcept {
        if (remaining <= 0) return current;
        if (n >= remaining) { remaining = 0; current = target; }
        else { current += step * static_cast<float>(n); remaining -= n; }
        return current;
    }
private:
    double sampleRate = 48000.0;
    float defaultRamp = 20.0f;
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0;
};
}
