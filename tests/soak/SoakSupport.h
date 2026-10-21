#pragma once
// Shared helpers for the nightly soak/stress tests (ctest label "soak"; never run by ci.yml).
// Durations are audio-time seconds and can be scaled down for local runs with environment variables
// (see docs/TESTING_SPEC.md section 12).
#include "TestHarness.h"
#include "PluginProcessor.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace soak {

/** Allocation counters fed by the global operator new/delete replacement in SoakSupport.cpp. */
struct AllocStats {
    std::uint64_t allocs = 0, frees = 0;
    std::int64_t outstanding() const { return (std::int64_t) allocs - (std::int64_t) frees; }
};
AllocStats globalAllocStats() noexcept;
/** Allocations made by the CALLING thread so far (audio-thread allocation check: diff before/after a block). */
std::uint64_t& threadAllocCount() noexcept;

/** Resident set size in bytes of this process, or -1 when unavailable on the platform. */
std::int64_t residentBytes();

inline double envSeconds(const char* name, double fallback) {
    if (const char* v = std::getenv(name)) {
        const double d = std::atof(v);
        if (d > 0.0) return d;
    }
    return fallback;
}

inline double nowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void setParam(CrowdMikeAudioProcessor& p, const juce::String& id, float actual) {
    if (auto* q = p.getParameters().getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(actual));
}

inline bool setLayout(CrowdMikeAudioProcessor& p, int in, int out) {
    auto l = p.getBusesLayout();
    l.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(in);
    l.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(out);
    return p.setBusesLayout(l);
}

/** Turns on every effect path and the optional per-input processing so the soak exercises the whole engine. */
void enableEverything(CrowdMikeAudioProcessor& p, int numInputs);

/** Deterministic pseudo-random signal source (low-level noise plus a decaying transient every ~0.7 s). */
struct Source {
    std::uint32_t state = 0x12345u;
    std::int64_t frame = 0;
    float next() { state = state * 1664525u + 1013904223u; return (float) (state >> 8) / 8388608.0f - 1.0f; }
    void fill(juce::AudioBuffer<float>& b, double sampleRate) {
        const auto period = (std::int64_t) (0.7 * sampleRate);
        for (int s = 0; s < b.getNumSamples(); ++s, ++frame) {
            const float env = (float) std::exp(-(double) (frame % period) / (0.02 * sampleRate));
            for (int c = 0; c < b.getNumChannels(); ++c) b.setSample(c, s, 0.12f * next() + 0.5f * env * next());
        }
    }
};

struct BlockResult { bool finite = true; float peak = 0.0f; };

inline BlockResult scan(const juce::AudioBuffer<float>& b) {
    BlockResult r;
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int s = 0; s < b.getNumSamples(); ++s) {
            if (! std::isfinite(d[s])) r.finite = false;
            else r.peak = std::max(r.peak, std::abs(d[s]));
        }
    }
    return r;
}

} // namespace soak
