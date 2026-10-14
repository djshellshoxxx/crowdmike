#pragma once
// RouteRisk -- advisory feedback-risk scoring for a mic -> loudspeaker route (ROUTING_SPEC section 6).
// Pure computation (no audio, no allocation, noexcept, thread-agnostic). NOT an acoustic guarantee.
//
// Model:
//   loopGainDb = inputTrimDb + groupGainDb + busGainDb + outputGainDb + max(0, compressorMakeupDb)
//              + delayBoostDb,   delayBoostDb = -20 log10(1 - clamp(delayFeedback, 0, 0.99))   (recirculation gain)
//   gainScore  = 1 / (1 + exp(-(loopGainDb - kMidpointDb) / kSlopeDb))       kMidpointDb = 0 dB, kSlopeDb = 4 dB
//   zoneFactor = sameZone ? 1.0 : 0.6           (mic and speaker in the same zone couple more strongly)
//   score      = 1 - (1 - gainScore * zoneFactor) * (1 - 0.6 * clamp(persistentPeakSeverity, 0, 1))
//   level      = score >= kHighThreshold (0.6) ? 2 (high) : score >= kCautionThreshold (0.3) ? 1 (caution) : 0 (safe)
//   routeExists == false => score 0, level 0.
// The score is monotonic non-decreasing in every gain input, delayFeedback, persistentPeakSeverity and sameZone.
// Non-finite inputs are treated as 0 (severity/feedback) or -inf dB (gains).
// Examples: same zone, unity loop gain => 0.5 (caution); same zone +2 dB => 0.62 (high); other zone, -12 dB => 0.03 (safe).
namespace crowdmike::dsp {

struct RiskInputs {
    bool routeExists = false;
    float inputTrimDb = 0.0f;
    float groupGainDb = 0.0f;
    float busGainDb = 0.0f;
    float outputGainDb = 0.0f;
    float compressorMakeupDb = 0.0f;
    float delayFeedback = 0.0f;           // 0..0.99 linear feedback of any delay in the path
    float persistentPeakSeverity = 0.0f;  // 0..1 from FeedbackDetector, 0 if unknown
    bool sameZone = false;
};

struct RiskResult {
    float score = 0.0f; // 0..1
    int level = 0;      // 0 safe, 1 caution, 2 high
    const char* reason = "no route"; // static string, never null
};

namespace risk {
constexpr float kMidpointDb = 0.0f;
constexpr float kSlopeDb = 4.0f;
constexpr float kOtherZoneFactor = 0.6f;
constexpr float kPeakWeight = 0.6f;
constexpr float kCautionThreshold = 0.3f;
constexpr float kHighThreshold = 0.6f;
}

// Total loop gain in dB used by scoreRisk (exposed for display).
float routeLoopGainDb(const RiskInputs& in) noexcept;
RiskResult scoreRisk(const RiskInputs& in) noexcept;

} // namespace crowdmike::dsp
