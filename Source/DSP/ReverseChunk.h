#pragma once
// ReverseChunk (params: crowdmike::params::ReverseP) -- plays chunks of the continuously captured send backward.
// Output is the stereo WET return (Mix scales it; `Return` is applied by the engine).
//
// Invariants / contract:
// * Capture: every process() sample of the selected source (Source 0 = sendL/R, 1 = preL/R; switching crossfades in
//   20 ms; null pre pointers fall back to send) is written into a PREALLOCATED stereo circular buffer. Capacity =
//   2 * 8 s + 0.5 s + maxBlock (+margin) at the prepared rate: a backward read of an L-sample chunk that started when
//   the chunk ended reaches data up to 2*L old while live capture continues, so 2 * kMaxChunkSec is the minimum for
//   glitch-free 8 s chunks. Allocation happens only in prepare(); a sample-rate change requires prepare() again (the
//   host/engine does this), which re-sizes and resets everything.
// * Nothing is copied on a trigger: a voice stores {absolute start index, length, read position k, generation}.
//   Voice output at position k is capture[start + len - 1 - k] (exact reverse order), multiplied by an equal-power
//   window that is exactly 1.0 outside the first/last `xf` samples (xf = Crossfade ms, clamped to len/2).
//   Indices are absolute 64-bit sample counts, so ChunkSec changes never affect a voice already playing (each voice
//   latches its own length/crossfade); data older than the buffer reads as exact 0 (never garbage).
// * Modes: Triggered = each trigger plays the most recent ChunkSec backward once (wet only while playing).
//   Continuous = a new voice every (len - xf) samples plays the previous chunk backward; consecutive voices overlap by
//   xf with equal-power fades (TrigMode/Trigger ignored). Repeat = a trigger starts a loop: pass 0 reads the circular
//   buffer and simultaneously records the reversed samples into one of two preallocated hold buffers (one sample per
//   output sample, never a block copy); later passes play the hold buffer, each pass Decay times the previous (held
//   while FxContext.freeze), overlapping by xf; the loop ends when the gain falls below -80 dB.
// * TrigMode: 0 Manual (Trigger rising edge), 1 Repeating (every len - xf samples), 2 Transient (onset detector on
//   the captured mono signal, Sens 0..1, 150 ms refractory), 3 Beat-sync (FxContext ppq crossing a multiple of
//   BeatDiv beats {1,2,4,8,16}; needs hasTempo && playing, otherwise falls back to Manual). A manual Trigger rising
//   edge also fires in every TrigMode. The first process() after reset() only latches the Trigger state.
// * Retrigger while a voice/loop is busy (not yet in its final fade): Restart = old voice fades out over its
//   crossfade while the new chunk fades in; Ignore = dropped (in Repeat mode a trigger then STOPS the loop, so a
//   loop can always be ended); Queue = one pending trigger fires when the current voice reaches its fade-out.
// * StereoLink = 1: both channels share indices. 0: the right channel reads its chunk min(len/2, 0.5 s) earlier
//   (decorrelated stereo reverse); channel synchronisation is otherwise identical.
// * Bypass: 10 ms fade of the return, then all state is reset (O(1)) and process() writes zeros (no capture).
// * Real-time safe (no allocation/locks), deterministic after reset(), non-finite input treated as 0,
//   block-size independent (all scheduling is per sample; manual triggers act at the block start).
#include "FxContext.h"
#include "FxUtil.h"
#include <atomic>

namespace crowdmike::dsp {
class ReverseChunk {
public:
    static constexpr double kMaxChunkSec = 8.0;
    static constexpr int kVoices = 6;

    // GUI snapshot. All positions/lengths are normalised to the circular buffer capacity (0..1).
    struct Viz {
        float writeHead01;
        float playHead01;
        float storedStart01;
        float storedLen01;
        int state;   // 0 idle, 1 armed/pending, 2 playing, 3 crossfading
        bool pending;
        float crossfade01;   // crossfade length / capacity
        float wet01;         // Mix * current voice gain * bypass fade
    };

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(const float* sendL, const float* sendR, const float* preL, const float* preR, float* outL, float* outR,
                 int n, const float* p, const FxContext& ctx) noexcept;

    Viz getViz() const noexcept;
    int getCapacitySamples() const noexcept { return cap; }
    uint32_t getTriggerCount() const noexcept { return trigCount.load(std::memory_order_relaxed); }
    // Absolute capture index (samples since reset) at which the last voice started (chunk end, exclusive).
    int64_t getLastTriggerSample() const noexcept { return lastTrigSample.load(std::memory_order_relaxed); }

private:
    struct Voice {
        bool active = false, releasing = false;
        int kind = 0;            // 0 one-shot from the circular buffer, 1 repeat-loop pass
        int64_t start = 0;       // absolute index of the first (oldest) chunk sample
        int64_t birth = 0;       // wTotal when created; a voice never plays in the sample that created it
        int len = 0, xf = 1, k = 0, rOff = 0;
        int relLen = 1, relPos = 0;
        int holdSlot = -1, pass = 0;
        float gain = 1.0f;
        uint32_t gen = 0;
    };
    float readCirc(const std::vector<float>& buf, int64_t absIdx) const noexcept;
    bool busy() const noexcept;
    void releaseAll() noexcept;
    void handleTrigger() noexcept;
    bool startVoice(int kind, int holdSlot, int pass, float gain, int64_t start, int len, int xf, int rOff) noexcept;
    bool startFresh() noexcept;
    int freeHoldSlot() noexcept;
    void resetState() noexcept;

    double sr = 48000.0;
    int cap = 1, maxLen = 1, maxROff = 0;
    std::vector<float> circL, circR;
    std::vector<float> hold[2][2];
    int wIdx = 0;
    int64_t wTotal = 0, filled = 0;
    Voice voices[kVoices];
    Smoother mixAmt, srcBlend, bypassGain;

    // per-block parameter snapshot
    int mode = 0, trigMode = 0, retrig = 0, lenParam = 1, xfParam = 1;
    bool link = true, frozen = false;
    float decay = 0.6f;

    // trigger sources
    float envFast = 0.0f, envSlow = 0.0f, atkFast = 0.0f, relFast = 0.0f, atkSlow = 0.0f, relSlow = 0.0f;
    int refractory = 0, contCounter = 1, repCounter = 1;
    double lastBeatPos = 0.0;
    bool beatValid = false, prevTrig = false, pending = false, fresh = true, cleared = true;
    int prevMode = -1, prevTrigMode = -1;
    uint32_t generation = 0;

    std::atomic<uint32_t> trigCount { 0 };
    std::atomic<int64_t> lastTrigSample { 0 };
    std::atomic<float> vWrite { 0.0f }, vPlay { 0.0f }, vStart { 0.0f }, vLen { 0.0f }, vXf { 0.0f }, vWet { 0.0f };
    std::atomic<int> vState { 0 };
    std::atomic<bool> vPending { false };
};
}
