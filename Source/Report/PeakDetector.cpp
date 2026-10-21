#include "PeakDetector.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::report {
namespace {
constexpr double kLn2 = 0.6931471805599453;
float clampLevel(float x) noexcept {
    if (!std::isfinite(x)) return kFloorDb;
    return std::min(std::max(x, kFloorDb), kCeilDb);
}
}

PeakConfig PeakConfig::fromSensitivity(float s) noexcept {
    if (!std::isfinite(s)) s = 0.5f;
    s = std::min(std::max(s, 0.0f), 1.0f);
    PeakConfig c;
    c.prominenceDb = 12.0f - 9.0f * s;
    c.minSpacingSec = 45.0f - 33.0f * s;
    return c;
}

void PeakDetector::reset() noexcept {
    initialised = inSwell = false;
    lastT = 0.0;
    lastAcceptedT = -1.0e30;
    y = base = ymax = baseAtMax = kFloorDb;
    tmax = 0.0;
}

PeakEvent PeakDetector::makePeak(double confirmedAt) const noexcept {
    PeakEvent p;
    p.t = tmax;
    p.levelDb = ymax;
    p.prominenceDb = ymax - baseAtMax;
    p.confirmedAt = confirmedAt;
    return p;
}

std::optional<PeakEvent> PeakDetector::pendingPeak() const noexcept {
    if (!inSwell || tmax - lastAcceptedT < static_cast<double>(cfg.minSpacingSec)) return std::nullopt;
    return makePeak(lastT);
}

PeakDetector::Result PeakDetector::push(double t, float levelDb) noexcept {
    Result r;
    const float x = clampLevel(levelDb);
    if (!initialised) {
        initialised = true;
        y = base = x;
        lastT = t;
        return r;
    }
    const double dt = std::min(std::max(t - lastT, 0.1), 10.0);
    lastT = t;
    y += static_cast<float>(1.0 - std::exp(-dt * kLn2)) * (x - y);
    const double tau = static_cast<double>(y > base ? cfg.baselineRiseSec : cfg.baselineFallSec);
    base += static_cast<float>(1.0 - std::exp(-dt / std::max(tau, 0.5))) * (y - base);

    if (y < cfg.minLevelDb) base = y;   // the baseline never lags behind the noise floor (no "peaks" out of silence)
    const float excursion = y - base;
    if (!inSwell) {
        if (excursion >= cfg.prominenceDb && y >= cfg.minLevelDb) {
            inSwell = true;
            ymax = y;
            tmax = t;
            baseAtMax = base;
        }
        return r;
    }
    if (y > ymax) { ymax = y; tmax = t; baseAtMax = base; }
    if (y <= ymax - cfg.confirmDropDb() || excursion < cfg.prominenceDb * 0.5f) {
        inSwell = false;
        const PeakEvent p = makePeak(t);
        if (p.t - lastAcceptedT < static_cast<double>(cfg.minSpacingSec)) {
            r.suppressed = true;
            r.suppressedAt = p.t;
        } else {
            lastAcceptedT = p.t;
            r.accepted = p;
        }
    }
    return r;
}
}
