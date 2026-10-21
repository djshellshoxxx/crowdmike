#include "Ducker.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr double kLn10Over20 = 0.115129254649702;
}

void Ducker::prepare(double sampleRate, int) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    detCoef = msToCoef(kDetectorReleaseMs, sr);
    appliedHpf = appliedLpf = -1.0f;
    reset();
}

void Ducker::reset() noexcept {
    hp.reset();
    lp.reset();
    env = 0.0f;
    grDb = 0.0;
    open = false;
    holdLeft = 0;
    grMeter.store(0.0f, std::memory_order_relaxed);
    levelMeter.store(-120.0f, std::memory_order_relaxed);
    peakMeter.store(0.0f, std::memory_order_relaxed);
    openMeter.store(false, std::memory_order_relaxed);
}

void Ducker::updateFilters(float hpf, float lpf) noexcept {
    if (std::abs(hpf - appliedHpf) > 0.005f * appliedHpf || appliedHpf < 0.0f) {
        appliedHpf = hpf;
        hp.setCoeffs(designHighpass(sr, hpf, 0.70710678f));
    }
    if (std::abs(lpf - appliedLpf) > 0.005f * appliedLpf || appliedLpf < 0.0f) {
        appliedLpf = lpf;
        lp.setCoeffs(designLowpass(sr, lpf, 0.70710678f));
    }
}

bool Ducker::process(const float* key, int n, const float* p, bool enabled, float* gain) noexcept {
    namespace D = params::DuckP;
    if (p == nullptr) enabled = false;
    // Settled and disabled: exact unity, nothing to compute.
    if (!enabled && !open && grDb == 0.0 && env < 1.0e-9f) {
        for (int i = 0; i < n; ++i) gain[i] = 1.0f;
        grMeter.store(0.0f, std::memory_order_relaxed);
        levelMeter.store(-120.0f, std::memory_order_relaxed);
        peakMeter.store(0.0f, std::memory_order_relaxed);
        openMeter.store(false, std::memory_order_relaxed);
        return false;
    }

    float thr = -35.0f, depth = -6.0f, range = -30.0f, attackMs = 10.0f, releaseMs = 400.0f, holdMs = 250.0f;
    if (p != nullptr) {
        thr = clampf(sanitize(p[D::Thr]), -80.0f, 0.0f);
        depth = clampf(sanitize(p[D::Depth]), -30.0f, 0.0f);
        range = clampf(sanitize(p[D::Range]), -30.0f, 0.0f);
        attackMs = clampf(sanitize(p[D::Attack]), 1.0f, 200.0f);
        releaseMs = clampf(sanitize(p[D::Release]), 10.0f, 5000.0f);
        holdMs = clampf(sanitize(p[D::Hold]), 0.0f, 2000.0f);
        updateFilters(clampf(sanitize(p[D::KeyHpf]), 20.0f, 2000.0f), clampf(sanitize(p[D::KeyLpf]), 1000.0f, 20000.0f));
    } else {
        updateFilters(120.0f, 6000.0f);
    }
    const double effDepth = std::max(depth, range);                      // both <= 0: smaller magnitude wins
    const double attackStep = 1.0 / std::max(1.0, attackMs * 0.001 * sr);
    const double releaseStep = 1.0 / std::max(1.0, releaseMs * 0.001 * sr);
    const long holdSamples = static_cast<long>(std::lround(holdMs * 0.001 * sr));
    const float closeLevel = thr - kHysteresisDb;

    bool any = false;
    float levelDb = -120.0f;
    for (int i = 0; i < n; ++i) {
        const float x = sanitize(key[i]);
        const float y = lp.process(hp.process(x));
        env = std::max(std::abs(y), env * detCoef);
        if (env < 1.0e-20f) env = 0.0f;
        levelDb = env > 1.0e-6f ? 20.0f * std::log10(env) : -120.0f;

        if (!enabled) { open = false; holdLeft = 0; }
        else if (levelDb >= thr) { open = true; holdLeft = holdSamples; }
        else if (open) {
            if (levelDb >= closeLevel) holdLeft = holdSamples;
            else if (holdLeft > 0) --holdLeft;
            else open = false;
        }

        const double target = open ? effDepth : 0.0;
        if (target < grDb) {
            const double span = std::max({ std::abs(effDepth), std::abs(grDb), 0.01 });
            grDb = std::max(target, grDb - span * attackStep);
        } else if (target > grDb) {
            const double span = std::max({ std::abs(effDepth), std::abs(grDb), 0.01 });
            grDb = std::min(target, grDb + span * releaseStep);
        }
        if (grDb == 0.0) gain[i] = 1.0f;
        else { gain[i] = static_cast<float>(std::exp(grDb * kLn10Over20)); any = true; }
    }
    hp.flushDenormals();
    lp.flushDenormals();
    grMeter.store(static_cast<float>(-grDb), std::memory_order_relaxed);
    levelMeter.store(levelDb, std::memory_order_relaxed);
    peakMeter.store(env, std::memory_order_relaxed);
    openMeter.store(open, std::memory_order_relaxed);
    return any;
}
}
