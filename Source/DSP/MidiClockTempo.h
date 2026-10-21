#pragma once
// MidiClockTempo -- tempo / transport math for incoming MIDI beat clock (0xF8, 24 pulses per quarter note; 0xFA start,
// 0xFB continue, 0xFC stop). Pure logic on a sample-time axis; no JUCE (docs/BEAT_SYNC_SPEC.md section 5).
//
// * onTick(samplePos): positions are monotonically increasing sample counts on the audio clock. A gap longer than 0.5 s
//   (clock paused / cable pulled) clears the interval history but keeps the last tempo.
// * Tempo: once 25 ticks are known the estimate is the exact span of one beat (24 intervals), which cancels per-tick
//   jitter; before that it is the mean interval of the ticks seen. The result is smoothed per tick with a one-pole
//   (0.25) and snaps when it differs by more than 25 % (tempo jump). Always within [20, 300] BPM.
// * hasTempo() needs 3 ticks (2 intervals). Song position pointer is not interpreted (Start resets the position to 0).
// * Transport: Start => running, position 0. Continue => running. Stop => stopped (position kept). A tick with no
//   Start/Stop seen yet (free-running master) is counted as running.
// * ppqAt(pos) extrapolates between ticks, never past the next expected tick, so the position is monotonic and bounded.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace crowdmike::dsp {
class MidiClockTempo {
public:
    static constexpr int kPpqn = 24;
    static constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;

    void prepare(double sampleRate) noexcept { sr = sampleRate > 1000.0 ? sampleRate : 48000.0; reset(); }
    void reset() noexcept { const double s = sr; *this = MidiClockTempo(); sr = s; }

    void onStart() noexcept { ticks = 0; running = true; explicitState = true; haveTick = false; }
    void onContinue() noexcept { running = true; explicitState = true; }
    void onStop() noexcept { running = false; explicitState = true; }

    void onTick(std::int64_t pos) noexcept {
        if (haveTick) {
            if (pos <= lastTick) return;                                  // out of order / duplicate
            if (static_cast<double>(pos - lastTick) > 0.5 * sr) histCount = 0;   // clock was paused
        }
        hist[static_cast<size_t>(head)] = pos;
        head = (head + 1) % kHist;
        histCount = std::min(histCount + 1, kHist);
        if (haveTick && running) ++ticks;
        else if (!haveTick && !explicitState) running = true;            // free-running clock without Start
        lastTick = pos;
        haveTick = true;
        if (histCount >= 3) update();
    }

    bool hasTempo() const noexcept { return valid; }
    double bpm() const noexcept { return currentBpm; }
    bool isRunning() const noexcept { return running; }
    std::int64_t tickCount() const noexcept { return ticks; }
    std::int64_t lastTickPos() const noexcept { return lastTick; }

    // Quarter-note position at sample `pos`.
    double ppqAt(std::int64_t pos) const noexcept {
        double frac = 0.0;
        if (valid && haveTick && pos > lastTick) {
            const double period = 60.0 * sr / (currentBpm * kPpqn);
            frac = std::clamp(static_cast<double>(pos - lastTick) / period, 0.0, 1.0);
        }
        return (static_cast<double>(ticks) + (running ? frac : 0.0)) / kPpqn;
    }

private:
    static constexpr int kHist = kPpqn + 1;

    void update() noexcept {
        const auto at = [this](int back) { return hist[static_cast<size_t>(((head - 1 - back) % kHist + kHist) % kHist)]; };
        double interval;   // samples per tick
        if (histCount >= kHist) interval = static_cast<double>(at(0) - at(kHist - 1)) / (kHist - 1);
        else interval = static_cast<double>(at(0) - at(histCount - 1)) / (histCount - 1);
        if (!(interval > 0.0)) return;
        const double raw = std::clamp(60.0 * sr / (interval * kPpqn), kMinBpm, kMaxBpm);
        if (!valid || std::fabs(raw - currentBpm) > 0.25 * currentBpm) currentBpm = raw;
        else currentBpm += 0.25 * (raw - currentBpm);
        valid = true;
    }

    double sr = 48000.0;
    std::array<std::int64_t, kHist> hist {};
    int head = 0, histCount = 0;
    std::int64_t lastTick = 0, ticks = 0;
    bool haveTick = false, running = false, explicitState = false, valid = false;
    double currentBpm = 120.0;
};
}
