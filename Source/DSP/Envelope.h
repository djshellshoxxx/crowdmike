#pragma once
// Envelope followers.
//
// EnvelopeFollower: one-pole attack/release follower of |x| (Peak) or of x^2 with sqrt output (Rms).
//   prepare(sr) only stores the rate (no allocation); process() is noexcept; reset() returns to 0 (deterministic).
//   Attack/release are time constants (63 % step response). Non-finite input is treated as 0. Output >= 0, finite.
//
// DensityFollower: slow crowd-intensity estimate 0..1 from the RMS level history of a (multi-channel) signal.
//   Mapping (documented contract):
//     1. short-term RMS of the channel-mean power, 300 ms time constant, in dBFS;
//     2. norm = clamp((rmsDb - floorDb) / (ceilDb - floorDb), 0, 1) with floorDb = -60, ceilDb = -12 (settable);
//     3. density = one-pole of smoothstep(norm) with 1.5 s attack and 4 s release.
//   So silence -> 0, steady crowd at -36 dBFS RMS -> 0.5, roaring crowd at >= -12 dBFS RMS -> 1.
//   process() only reads the buffers. getDensity() is a relaxed atomic read, safe from any thread.
#include "DspCommon.h"
#include <atomic>

namespace crowdmike::dsp {
class EnvelopeFollower {
public:
    enum class Mode { Peak, Rms };
    void prepare(double sampleRate) noexcept { sr = sampleRate > 0.0 ? sampleRate : 48000.0; update(); }
    void setMode(Mode m) noexcept { mode = m; }
    void setAttackMs(float ms) noexcept { attackMs = ms; update(); }
    void setReleaseMs(float ms) noexcept { releaseMs = ms; update(); }
    void reset() noexcept { state = 0.0f; }
    float process(float x) noexcept;                    // returns the envelope (linear amplitude)
    float processBlock(const float* x, int n) noexcept;  // returns the envelope after the block
    float getValue() const noexcept { return mode == Mode::Rms ? std::sqrt(state) : state; }

private:
    void update() noexcept { att = msToCoef(attackMs, sr); rel = msToCoef(releaseMs, sr); }
    double sr = 48000.0;
    Mode mode = Mode::Peak;
    float attackMs = 5.0f, releaseMs = 100.0f, att = 0.0f, rel = 0.0f, state = 0.0f;
};

class DensityFollower {
public:
    void prepare(double sampleRate, int maxBlock = 0) noexcept;
    void reset() noexcept;
    void setRange(float floorDbFs, float ceilDbFs) noexcept { floorDb = floorDbFs; ceilDb = std::max(ceilDbFs, floorDbFs + 1.0f); }
    void process(const float* const* ch, int nch, int n) noexcept;
    float getDensity() const noexcept { return density.load(std::memory_order_relaxed); }
    float getLevelDb() const noexcept { return levelDb.load(std::memory_order_relaxed); }

private:
    double sr = 48000.0;
    float floorDb = -60.0f, ceilDb = -12.0f;
    float rmsCoef = 0.0f, attCoef16 = 0.0f, relCoef16 = 0.0f;   // slow stages run every 16 samples
    double power = 0.0;
    float value = 0.0f, lastDb = -120.0f;
    int tick = 0;
    std::atomic<float> density { 0.0f }, levelDb { -120.0f };
};
}
