#pragma once
// Everything the GUI reads back from the audio thread. Single-writer (audio) / many-reader (GUI), relaxed atomics only.
#include <array>
#include <atomic>
#include "Core/Limits.h"

namespace crowdmike {
struct AtomicPeak {
    std::atomic<float> value { 0.0f };
    void update(float v) noexcept { float cur = value.load(std::memory_order_relaxed); while (v > cur && !value.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {} }
    float getAndReset() noexcept { return value.exchange(0.0f, std::memory_order_relaxed); }
    float peek() const noexcept { return value.load(std::memory_order_relaxed); }
};

struct EngineMeters {
    std::array<AtomicPeak, kMaxInputs> inPeak;
    std::array<std::atomic<float>, kMaxInputs> inGrDb {};          // compressor gain reduction (<= 0 dB shown as positive reduction)
    std::array<std::atomic<bool>, kMaxInputs> inClip {};
    std::array<AtomicPeak, kMaxGroups> groupPeakL, groupPeakR;
    std::array<std::atomic<float>, kMaxGroups> groupGrDb {};
    std::array<AtomicPeak, kNumFx> fxPeak;                          // return activity
    AtomicPeak masterPeakL, masterPeakR;
    std::atomic<float> masterGrDb { 0.0f }, masterLimGrDb { 0.0f };
    std::array<AtomicPeak, kMaxOutputs> outPeak;
    std::array<std::atomic<float>, kMaxOutputs> outLimGrDb {};
    std::array<std::atomic<bool>, kMaxOutputs> outClip {};
    std::atomic<float> intensity { 0.0f };                          // crowd density 0..1
    std::atomic<float> correlation { 1.0f }, width { 0.0f };        // master stereo
    std::atomic<float> cpuLoad { 0.0f };                            // 0..1 of the block budget (smoothed)
    std::atomic<int> latencySamples { 0 };
    std::atomic<int> sampleRate { 0 }, blockSize { 0 };
    std::atomic<std::uint64_t> blocks { 0 }, overruns { 0 };        // overrun: block took longer than its real-time budget
    std::atomic<bool> panicActive { false }, bypassActive { false }, rehearsalActive { false }, soundcheckActive { false };
    std::array<std::atomic<float>, kMaxInputs> inRisk {};            // feedback-risk score 0..1 (advisory)

    // ---- Show Report feeds (SHOW_REPORT_SPEC section 3). Audio thread writes, the report tracker reads at 1 Hz.
    std::atomic<float> crowdLevelDb { -120.0f };     // 300 ms RMS of the summed crowd groups (pre-FX), dBFS
    std::atomic<float> masterRmsDb { -120.0f };      // 400 ms unweighted RMS of the master bus, dBFS
    std::atomic<float> shortTermLufs { -120.0f };    // K-weighted 3 s loudness (approximation, no gating)
    AtomicPeak reportMasterPeak;                     // reset only by the report tracker
    struct EventCounters {                           // cumulative since process start; episodes / rising edges, not blocks
        std::atomic<std::uint32_t> clipEvents { 0 }, limiterEvents { 0 }, panicEvents { 0 }, echoThrows { 0 }, reverseTriggers { 0 }, freezeEvents { 0 };
    } events;
};
}
