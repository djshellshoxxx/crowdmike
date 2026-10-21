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
    // ---- appended (docs/DUCKER_SPEC.md, docs/ENGINE_SPEC.md "Meters added for usability")
    std::array<std::atomic<float>, kMaxGroups> groupLimGrDb {};      // group limiter gain reduction (<= 0 dB; use abs() for a bar)
    std::array<AtomicPeak, kNumFx> fxSendPeak;                       // level of the send bus entering each effect (max of L/R, linear)
    std::atomic<float> duckGrDb { 0.0f };                            // performer ducker: applied reduction, >= 0 dB
    AtomicPeak duckKeyPeak;                                          // ducker detector (filtered key), linear
    std::atomic<bool> duckOpen { false };                            // ducker detector state
};
}
