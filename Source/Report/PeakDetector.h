#pragma once
// PeakDetector: online applause / energy-peak detector over a ~1 Hz crowd-level series (SHOW_REPORT_SPEC section 6).
// Deterministic, no JUCE, no allocation after construction. Feed it (time, dB) pairs in time order.
#include <optional>
#include "ReportTypes.h"

namespace crowdmike::report {
struct PeakConfig {
    float prominenceDb = 7.5f;        // minimum rise above the adaptive baseline
    float minSpacingSec = 28.5f;      // minimum time between accepted peaks
    float minLevelDb = -60.0f;        // absolute floor for a peak
    float baselineRiseSec = 60.0f;    // baseline time constant when the level is above it
    float baselineFallSec = 8.0f;     // ... and when below it

    // Sensitivity 0..1 (higher = more peaks): prominence 12 -> 3 dB, spacing 45 -> 12 s. Non-finite -> 0.5.
    static PeakConfig fromSensitivity(float s) noexcept;
    float confirmDropDb() const noexcept { return prominenceDb / 3.0f > 2.0f ? prominenceDb / 3.0f : 2.0f; }
};

class PeakDetector {
public:
    struct Result {
        std::optional<PeakEvent> accepted;   // a peak confirmed (and accepted) by this sample
        bool suppressed = false;             // a peak was confirmed but rejected by the spacing rule
        double suppressedAt = 0.0;           // its time
    };

    explicit PeakDetector(const PeakConfig& c = {}) : cfg(c) {}
    void setConfig(const PeakConfig& c) noexcept { cfg = c; }
    const PeakConfig& config() const noexcept { return cfg; }
    void reset() noexcept;

    Result push(double t, float levelDb) noexcept;

    // A swell that has not been confirmed yet (e.g. the show ended during the applause), if it qualifies.
    std::optional<PeakEvent> pendingPeak() const noexcept;
    float baselineDb() const noexcept { return base; }
    float smoothedDb() const noexcept { return y; }

private:
    PeakEvent makePeak(double confirmedAt) const noexcept;

    PeakConfig cfg;
    bool initialised = false, inSwell = false;
    double lastT = 0.0, lastAcceptedT = -1.0e30;
    float y = kFloorDb, base = kFloorDb;
    float ymax = kFloorDb, baseAtMax = kFloorDb;
    double tmax = 0.0;
};
}
