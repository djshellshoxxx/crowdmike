#pragma once
// Show Report data model (SHOW_REPORT_SPEC sections 5, 9). Plain data only: no JUCE, no threads, no I/O.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace crowdmike::report {
constexpr int kSchemaVersion = 1;
constexpr float kFloorDb = -120.0f;
constexpr float kCeilDb = 24.0f;
constexpr float kActiveFloorDb = -70.0f;   // samples below this are "no crowd" for averages

// One series row (1 Hz, or every EnergyTracker::stepSeconds() after decimation).
struct Sample {
    double t = 0.0;                       // seconds since the window start (absolute tracker time inside the tracker)
    float crowdDb = kFloorDb;             // crowd-bus RMS, dBFS
    float density = 0.0f;                 // 0..1
    float masterDb = kFloorDb;            // master RMS, dBFS
    float masterPeakDb = kFloorDb;        // master sample peak, dBFS
    float lufs = kFloorDb;                // approx. short-term LUFS
    int clip = 0, limiter = 0, feedback = 0, panic = 0, fx = 0, scene = 0, xrun = 0;   // events in the interval
    bool recording = false;
};

struct PeakEvent {
    double t = 0.0;                       // time of the maximum
    float levelDb = kFloorDb;             // smoothed crowd level at the maximum
    float prominenceDb = 0.0f;            // rise above the adaptive baseline
    double confirmedAt = 0.0;             // when the detector confirmed it
    bool marker = false;                  // a recorder marker was dropped for it
};

struct FeedbackEvent { double t = 0.0; float frequencyHz = 0.0f, severity = 0.0f; };
struct SceneEvent { double t = 0.0; std::string name; };
struct MarkerEvent { double t = 0.0; std::string label; };
struct FxEvent { enum Kind { EchoThrow = 0, ReverseTrigger = 1, Freeze = 2 }; double t = 0.0; int kind = 0, count = 1; };

struct SceneUse { std::string name; int recalls = 0; double firstSeconds = 0.0; };

struct DeviceInfo {
    std::string driver, name;
    double sampleRate = 0.0;
    int bufferSize = 0, latencySamples = 0;
    double latencyMs = 0.0;
    int inputs = 0, outputs = 0;
};

// Everything the writers need that does not come from the tracker. All strings are user data and get escaped on output.
struct ReportMeta {
    std::string show, venue, scene, appVersion, sessionFile;
    int take = 0;
    std::int64_t startEpochMs = 0;        // wall clock at window start (UTC)
    int utcOffsetMinutes = 0;             // local offset used for display
    DeviceInfo device;
    double peakSensitivity = 0.5;
    double peakProminenceDb = 7.5, peakMinSpacingSeconds = 28.5;
    bool autoMarkers = true;
};

struct FxUsage {
    int echoThrows = 0, reverseTriggers = 0, freezes = 0;
    int total() const noexcept { return echoThrows + reverseTriggers + freezes; }
};

struct Summary {
    double durationSeconds = 0.0, recordedSeconds = 0.0, peakCrowdAtSeconds = 0.0;
    float averageCrowdDb = kFloorDb, peakCrowdDb = kFloorDb;
    float maxLufs = kFloorDb, meanLufs = kFloorDb, maxMasterPeakDb = kFloorDb;
    int peakCount = 0, peaksSuppressed = 0;
    int clipEvents = 0, limiterEvents = 0, feedbackEvents = 0, panicEvents = 0, xruns = 0, sceneRecalls = 0;
    FxUsage fx;
};

struct TopMoment { int rank = 0; PeakEvent peak; };

// A finished, self-contained report. `series`, `peaks` and all event times are relative to the window start.
struct ReportData {
    ReportMeta meta;
    Summary summary;
    double stepSeconds = 1.0;
    std::vector<Sample> series;
    std::vector<PeakEvent> peaks;
    std::vector<TopMoment> topMoments;    // at most 5, ranked by prominence
    std::vector<SceneUse> scenes;
    std::vector<FeedbackEvent> feedbackEvents;
    std::vector<MarkerEvent> markers;     // user markers (auto peak markers are in `peaks`)
    std::vector<SceneEvent> sceneEvents;
};
}
