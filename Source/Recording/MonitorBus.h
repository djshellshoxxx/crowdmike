#pragma once
// MonitorBus: the cue / monitor output of the recorder (RECORDING_SPEC 14).
//
// Every recorder track whose MON switch is on contributes its record tap (the same signal that is written to disk)
// to ONE stereo cue pair of the output buses. The bus is pure DSP glue with no knowledge of taps or buses: the engine
// feeds it the tap pointers of the monitored tracks once per block and lets it add the finished cue mix to the chosen
// output pair AFTER the output stage, so panic / rehearsal / bypass and the output limiters never touch the cue.
//
// THREADS: setters from any thread (atomics); beginBlock/addSource/mixInto from the audio thread only.
//          prepare() allocates and must not run concurrently with the audio thread.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <vector>

namespace crowdmike::rec {

class MonitorBus {
public:
    static constexpr int kMaxTracks = 32;
    static constexpr float kMinGainDb = -60.0f, kMaxGainDb = 12.0f;

    void prepare(double sampleRate, int maxBlock);

    //==== control (any thread) =======================================================================================
    /** Output pair that carries the cue: pair k = output channels 2k, 2k+1. -1 = monitor off. */
    void setPair(int pair) noexcept { pairIdx.store(pair < 0 ? -1 : pair, std::memory_order_relaxed); }
    void setGainDb(float db) noexcept;
    void setTrackMonitored(int track, bool on) noexcept;
    int getPair() const noexcept { return pairIdx.load(std::memory_order_relaxed); }
    float getGainDb() const noexcept { return gainDb.load(std::memory_order_relaxed); }
    bool isTrackMonitored(int track) const noexcept;
    /** Peak of the cue mix since the last call (linear, resets). */
    float takePeak() noexcept { return peak.exchange(0.0f, std::memory_order_relaxed); }

    //==== audio thread ===============================================================================================
    bool isActive() const noexcept { return getPair() >= 0 && mask.load(std::memory_order_relaxed) != 0; }
    void beginBlock(int n) noexcept;
    /** Adds one tap. r == nullptr (or channels == 1) feeds l to both sides at unity (mono tap = centred). */
    void addSource(const float* l, const float* r, int n) noexcept;
    /** Adds the mix (gain applied, ramped across the block) into outL/outR and hard-limits the sum to +-1.
        Either pointer may be nullptr (pair partly beyond the available outputs). */
    void mixInto(float* outL, float* outR, int n) noexcept;

private:
    std::atomic<int> pairIdx { -1 };
    std::atomic<float> gainDb { 0.0f };
    std::atomic<std::uint32_t> mask { 0 };
    std::atomic<float> peak { 0.0f };
    std::vector<float> accL, accR;
    float appliedGain = 1.0f;
};

} // namespace crowdmike::rec
