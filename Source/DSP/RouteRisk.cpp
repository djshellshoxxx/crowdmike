#include "RouteRisk.h"
#include "DspCommon.h"
#include <cmath>
#include <limits>

namespace crowdmike::dsp {

namespace {
float gainIn(float db) noexcept { return std::isfinite(db) ? db : -std::numeric_limits<float>::infinity(); }
float unitIn(float v, float hi) noexcept { return std::isfinite(v) ? clampf(v, 0.0f, hi) : 0.0f; }
}

float routeLoopGainDb(const RiskInputs& in) noexcept {
    const float fb = unitIn(in.delayFeedback, 0.99f);
    const float delayBoostDb = -20.0f * std::log10(1.0f - fb);
    const float makeup = std::isfinite(in.compressorMakeupDb) ? std::max(0.0f, in.compressorMakeupDb) : 0.0f;
    return gainIn(in.inputTrimDb) + gainIn(in.groupGainDb) + gainIn(in.busGainDb) + gainIn(in.outputGainDb) + makeup
         + delayBoostDb;
}

RiskResult scoreRisk(const RiskInputs& in) noexcept {
    RiskResult r;
    if (!in.routeExists) return r;
    const float loop = routeLoopGainDb(in);
    float gainScore = 0.0f;
    if (std::isfinite(loop)) gainScore = 1.0f / (1.0f + std::exp(-(loop - risk::kMidpointDb) / risk::kSlopeDb));
    else if (loop > 0.0f) gainScore = 1.0f;
    const float zone = in.sameZone ? 1.0f : risk::kOtherZoneFactor;
    const float sev = unitIn(in.persistentPeakSeverity, 1.0f);
    const float a = gainScore * zone, b = risk::kPeakWeight * sev;
    r.score = clampf(1.0f - (1.0f - a) * (1.0f - b), 0.0f, 1.0f);
    r.level = r.score >= risk::kHighThreshold ? 2 : (r.score >= risk::kCautionThreshold ? 1 : 0);
    if (r.level == 0) r.reason = "low loop gain";
    else if (b >= a) r.reason = "persistent feedback peak measured";
    else if (in.sameZone) r.reason = r.level == 2 ? "high loop gain, mic and speaker in same zone"
                                                  : "moderate loop gain, mic and speaker in same zone";
    else r.reason = r.level == 2 ? "high loop gain" : "moderate loop gain";
    return r;
}

} // namespace crowdmike::dsp
