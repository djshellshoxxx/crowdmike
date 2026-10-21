#pragma once
// EnergyTracker: bounded 1 Hz crowd-energy history + event log + peak detection (SHOW_REPORT_SPEC sections 5, 6).
// Pure model: no JUCE, no threads (the caller serialises access), no I/O. Time is supplied by the caller in seconds.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "PeakDetector.h"
#include "ReportTypes.h"

namespace crowdmike::report {
// Cumulative engine counters (uint32, since process start). A counter that goes backwards is treated as a reset.
struct Counters { std::uint32_t clip = 0, limiter = 0, panic = 0, echo = 0, reverse = 0, freeze = 0, xrun = 0; };
struct FeedbackObs { float frequencyHz = 0.0f, severity = 0.0f; };

struct TrackerInput {
    double t = 0.0;                         // seconds, strictly increasing between calls
    float crowdDb = kFloorDb, density = 0.0f, masterDb = kFloorDb, masterPeakDb = kFloorDb, lufs = kFloorDb;
    Counters counters;
    bool recording = false;
    std::array<FeedbackObs, 4> feedback {};
    int feedbackCount = 0;
};

class EnergyTracker {
public:
    struct Config {
        std::size_t maxSamples = 43200;     // 12 h at 1 Hz; the series is halved in place beyond this
        std::size_t maxPeaks = 2000;
        std::size_t maxEvents = 500;        // per event list (scenes, feedback, markers, suppressed peaks); fx list is 4x
        PeakConfig peak;
    };
    struct PushResult {
        bool newPeak = false;
        PeakEvent peak;                      // valid when newPeak
        int newFeedback = 0;
    };

    EnergyTracker() : EnergyTracker(Config {}) {}
    explicit EnergyTracker(const Config& c);

    void reset();
    void setPeakConfig(const PeakConfig& c) { detector.setConfig(c); cfg.peak = c; }
    const PeakConfig& peakConfig() const noexcept { return cfg.peak; }

    // One sample (about once per second). Inputs are clamped; a sample with t <= the previous one is ignored.
    PushResult push(const TrackerInput& in);
    // Message-thread events (time on the same axis as push()).
    void noteSceneRecall(double t, const std::string& name);
    void noteMarker(double t, const std::string& label);
    void markLastPeak() noexcept { if (!peaks_.empty()) peaks_.back().marker = true; }

    // ---- queries
    bool empty() const noexcept { return !haveFirst; }
    double firstTime() const noexcept { return firstT; }
    double lastTime() const noexcept { return lastT; }
    int stepSeconds() const noexcept { return step; }
    std::size_t sampleCount() const noexcept { return series_.size() + (acc.n > 0 ? 1u : 0u); }
    const std::vector<Sample>& series() const noexcept { return series_; }
    const std::vector<PeakEvent>& peaks() const noexcept { return peaks_; }
    int peakCountSince(double t0) const noexcept;
    // Approximate bytes held (bounded: independent of the show length).
    std::size_t memoryBytes() const noexcept;
    // Last `count` crowd levels (oldest first, newest at the end) for sparklines; missing history is kFloorDb.
    void recentCrowdDb(float* out, int count) const noexcept;
    float currentCrowdDb() const noexcept { return lastCrowd; }

    // Builds a report for [t0, t1] (absolute tracker time). Series/events are rebased so the window starts at 0.
    // `includePending` adds a swell that is still open (e.g. the show ended during applause).
    ReportData buildReport(const ReportMeta& meta, double t0, double t1, bool includePending = true) const;

private:
    struct Acc {
        double t = 0.0, pCrowd = 0.0, pMaster = 0.0, pLufs = 0.0, dens = 0.0;
        float peak = kFloorDb;
        int clip = 0, limiter = 0, feedback = 0, panic = 0, fx = 0, scene = 0, xrun = 0, n = 0;
        bool rec = false;
        void add(const Sample& s) noexcept;
        Sample finish() const noexcept;
    };
    void flushBucket();
    void halve();

    Config cfg;
    PeakDetector detector;
    std::vector<Sample> series_;
    int step = 1;
    Acc acc;
    bool haveFirst = false, havePrev = false;
    double firstT = 0.0, lastT = 0.0;
    float lastCrowd = kFloorDb;
    Counters prev;
    int pendingScene = 0;
    std::vector<PeakEvent> peaks_;
    std::vector<double> suppressed_;
    std::vector<FeedbackEvent> feedback_;
    std::vector<SceneEvent> scenes_;
    std::vector<MarkerEvent> markers_;
    std::vector<FxEvent> fx_;
    std::array<float, 8> activeFb {};
    int activeFbCount = 0;
    std::array<float, 120> recent {};     // ring of the last 120 raw crowd levels (1 Hz)
    int recentPos = 0, recentCount = 0;
};
}
