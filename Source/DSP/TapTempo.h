#pragma once
// TapTempo -- tap-tempo estimator (docs/BEAT_SYNC_SPEC.md section 4). Pure logic, caller supplies timestamps in seconds.
//
// * Keeps the last kMaxTaps (8) taps. A tap more than kResetGapSec (2.5 s) after the previous one starts a new sequence.
// * Taps closer than 60/kMaxBpm (0.2 s) to the previous tap are contact bounce and ignored; non-monotonic times too.
// * bpm = 60 / mean(inlier intervals). With >= 4 intervals, intervals further than 25 % from the median are outliers; if
//   fewer than half remain the sequence restarts from the newest two taps. With 2..3 intervals, a newest interval that
//   differs from the mean of the earlier ones by more than 50 % is a tempo change: restart from the newest two taps.
// * bpm is always finite and within [kMinBpm, kMaxBpm]; hasBpm() needs >= 2 taps.
#include <algorithm>
#include <array>
#include <cmath>

namespace crowdmike::dsp {
class TapTempo {
public:
    static constexpr int kMaxTaps = 8;
    static constexpr double kResetGapSec = 2.5;
    static constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;

    void reset() noexcept { count = 0; valid = false; }

    // Returns true when the tap was accepted. After an accepted tap, hasBpm() tells whether a tempo is available.
    bool tap(double timeSec) noexcept {
        if (!std::isfinite(timeSec)) return false;
        if (count > 0) {
            const double dt = timeSec - times[static_cast<size_t>(count - 1)];
            if (dt < 0.0) { reset(); }               // clock went backwards: start over
            else if (dt > kResetGapSec) { count = 0; }   // keeps the last tempo until a new one is known
            else if (dt < 60.0 / kMaxBpm) return false;
        }
        if (count == kMaxTaps) { for (int i = 1; i < kMaxTaps; ++i) times[static_cast<size_t>(i - 1)] = times[static_cast<size_t>(i)]; --count; }
        times[static_cast<size_t>(count++)] = timeSec;
        recompute();
        return true;
    }

    bool hasBpm() const noexcept { return valid; }
    double bpm() const noexcept { return currentBpm; }
    int tapCount() const noexcept { return count; }

private:
    void restartFromNewestTwo() noexcept {
        times[0] = times[static_cast<size_t>(count - 2)];
        times[1] = times[static_cast<size_t>(count - 1)];
        count = 2;
    }

    void recompute() noexcept {
        if (count < 2) return;
        std::array<double, kMaxTaps> iv {};
        int n = count - 1;
        for (int i = 0; i < n; ++i) iv[static_cast<size_t>(i)] = times[static_cast<size_t>(i + 1)] - times[static_cast<size_t>(i)];

        if (n >= 4) {
            std::array<double, kMaxTaps> sorted = iv;
            std::sort(sorted.begin(), sorted.begin() + n);
            const double med = n % 2 ? sorted[static_cast<size_t>(n / 2)] : 0.5 * (sorted[static_cast<size_t>(n / 2 - 1)] + sorted[static_cast<size_t>(n / 2)]);
            double sum = 0.0;
            int kept = 0;
            for (int i = 0; i < n; ++i)
                if (std::fabs(iv[static_cast<size_t>(i)] - med) <= 0.25 * med) { sum += iv[static_cast<size_t>(i)]; ++kept; }
            if (kept * 2 < n) { restartFromNewestTwo(); n = 1; iv[0] = times[1] - times[0]; sum = iv[0]; kept = 1; }
            publish(sum / kept);
            return;
        }
        if (n >= 2) {
            double prior = 0.0;
            for (int i = 0; i < n - 1; ++i) prior += iv[static_cast<size_t>(i)];
            prior /= (n - 1);
            if (std::fabs(iv[static_cast<size_t>(n - 1)] - prior) > 0.5 * prior) { restartFromNewestTwo(); publish(times[1] - times[0]); return; }
        }
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += iv[static_cast<size_t>(i)];
        publish(sum / n);
    }

    void publish(double meanInterval) noexcept {
        if (!(meanInterval > 0.0)) return;
        currentBpm = std::clamp(60.0 / meanInterval, kMinBpm, kMaxBpm);
        valid = true;
    }

    std::array<double, kMaxTaps> times {};
    int count = 0;
    bool valid = false;
    double currentBpm = 120.0;
};
}
