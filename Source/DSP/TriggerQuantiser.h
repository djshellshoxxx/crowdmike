#pragma once
// TriggerQuantiser -- lands momentary triggers (Echo Throw, Reverse Chunk, clip triggers) on the beat grid
// (docs/BEAT_SYNC_SPEC.md section 7). Pure logic, real-time safe (no allocation, no JUCE).
//
// Per block the caller passes `edge` (a trigger request arrived since the previous block) and the block timing. The
// result is one of:
//   Passthrough : quantising is inactive (mode Off, or no usable tempo / transport); the caller handles the trigger
//                 exactly as before (a pending request, if any, is released as Fire at offset 0).
//   None        : nothing to do this block (a request may be pending).
//   Fire        : the trigger fires at sample `offset` of this block (0 <= offset < n), the first sample at or after the
//                 next grid line. Sample-exact: ppq(offset) >= grid > ppq(offset - 1).
// Rules:
//   * grid = 1 beat (1/4), 2 beats (1/2) or barBeats beats (1 bar). A request is treated as made at the start of the
//     block in which it was seen; if the block contains a grid line the trigger fires there, otherwise it waits.
//   * Requests that arrive while one is pending coalesce into it.
//   * A pending request never waits more than 2 bars + 1 beat (a stalled/looping transport cannot hang a trigger): it
//     then fires at offset 0 of the block in which the limit is hit.
//   * Usable timing: hasTempo && playing && finite bpm in [1, 1000] && finite ppq. Anything else => Passthrough.
#include <cmath>

namespace crowdmike::dsp {
enum class QuantiseMode : int { Off = 0, Quarter = 1, Half = 2, Bar = 3 };

struct QuantiseTiming {
    int mode = 0;              // QuantiseMode
    int barBeats = 4;          // beats per bar (2..7)
    double bpm = 120.0;
    bool hasTempo = false;
    bool playing = false;
    double ppq = 0.0;          // quarter-note position at the first sample of the block
    double sampleRate = 48000.0;
};

struct QuantiseResult {
    enum class Kind { Passthrough, None, Fire } kind = Kind::Passthrough;
    int offset = 0;
};

inline double gridBeatsFor(int mode, int barBeats) noexcept {
    switch (mode) {
        case 1: return 1.0;
        case 2: return 2.0;
        case 3: return barBeats < 1 ? 4.0 : static_cast<double>(barBeats);
        default: return 0.0;
    }
}

// First sample index in [0, n) at which the position reaches a multiple of `gridBeats` (the sample whose position is >= the
// grid line while the previous sample's is below it), or -1.
inline int findGridOffset(double ppq0, double bpm, double sampleRate, int n, double gridBeats) noexcept {
    if (!(gridBeats > 0.0) || !(bpm > 0.0) || !(sampleRate > 0.0) || n <= 0 || !std::isfinite(ppq0)) return -1;
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    // First grid line the position reaches at or after sample 0 (a line less than one sample behind sample 0 was not
    // reached by the previous block's last sample, so it belongs to this block's sample 0).
    const double k = std::floor((ppq0 - beatsPerSample) / gridBeats + 1.0e-9) + 1.0;
    const double gridPos = k * gridBeats;
    const double samples = (gridPos - ppq0) / beatsPerSample;
    int i = static_cast<int>(std::ceil(samples - 1.0e-4));
    if (i < 0) i = 0;
    return i < n ? i : -1;
}

class TriggerQuantiser {
public:
    void reset() noexcept { pending = false; waited = 0.0; }
    bool isPending() const noexcept { return pending; }

    QuantiseResult process(bool edge, const QuantiseTiming& t, int n) noexcept {
        QuantiseResult r;
        const bool usable = t.mode >= 1 && t.mode <= 3 && t.hasTempo && t.playing && std::isfinite(t.bpm) && t.bpm >= 1.0
                            && t.bpm <= 1000.0 && std::isfinite(t.ppq) && n > 0;
        if (!usable) {
            if (pending) { pending = false; r.kind = QuantiseResult::Kind::Fire; r.offset = 0; return r; }
            r.kind = QuantiseResult::Kind::Passthrough;
            return r;
        }
        r.kind = QuantiseResult::Kind::None;
        if (edge && !pending) { pending = true; waited = 0.0; }
        if (!pending) return r;
        const double grid = gridBeatsFor(t.mode, t.barBeats);
        const int off = findGridOffset(t.ppq, t.bpm, t.sampleRate, n, grid);
        if (off >= 0) { pending = false; r.kind = QuantiseResult::Kind::Fire; r.offset = off; return r; }
        waited += static_cast<double>(n) / t.sampleRate;
        const double limitSec = (2.0 * (t.barBeats < 1 ? 4 : t.barBeats) + 1.0) * 60.0 / t.bpm;
        if (waited > limitSec) { pending = false; r.kind = QuantiseResult::Kind::Fire; r.offset = 0; }
        return r;
    }

private:
    bool pending = false;
    double waited = 0.0;
};
}
