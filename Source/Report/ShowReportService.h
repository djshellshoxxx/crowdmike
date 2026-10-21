#pragma once
// ShowReportService: message-thread adapter between the engine/recorder/show model and the pure report model
// (SHOW_REPORT_SPEC sections 2, 8). Owns an EnergyTracker, samples the engine meters at 1 Hz from poll(), drops "Crowd peak"
// recorder markers, tracks the report window (one recorder take, or the whole history) and writes the three report files.
// Everything here runs on the message thread; the audio thread only touches the relaxed atomics in EngineMeters.
#include <juce_core/juce_core.h>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include "Core/ShowModel.h"
#include "Engine/CrowdEngine.h"
#include "Recording/RecorderEngine.h"
#include "Report/EnergyTracker.h"
#include "Standalone/HostServices.h"

namespace crowdmike::report {
// Where auto markers go. The default sink writes to the RecorderEngine; tests inject a fake.
struct MarkerSink {
    virtual ~MarkerSink() = default;
    virtual bool isRecording() const = 0;
    virtual void addPeakMarker(const std::string& label, double secondsAgo) = 0;
};

struct ExportResult {
    bool ok = false;
    juce::File folder, html, csv, json;
    juce::String error;
};

class ShowReportService {
public:
    static constexpr const char* kAutoMarkerPrefix = "Crowd peak";

    ShowReportService(CrowdEngine& engine, rec::RecorderEngine& recorder, ShowModel& show);
    ~ShowReportService();

    // ---- wiring (message thread, before use)
    void setDeviceStatusProvider(std::function<DeviceStatus()> f) { deviceProvider = std::move(f); }
    void setFeedbackAnalysisControl(std::function<void(bool)> f) { analysisControl = std::move(f); }
    void setSceneNameProvider(std::function<juce::String()> f) { sceneName = std::move(f); }
    void setMarkerSink(MarkerSink* s) noexcept { sink = s != nullptr ? s : &defaultSink; }
    void setAppVersion(const juce::String& v) { appVersion = v.toStdString(); }

    // ---- driving
    // Call often (the processor timer, 20 Hz) with a monotonic clock in seconds. Samples once per second internally.
    void poll(double nowSeconds);
    // A scene was recalled (message thread).
    void noteSceneRecall(const juce::String& sceneName);
    // Forget all history and start a new show clock (the next poll() starts at t = 0).
    void resetHistory();

    // ---- export
    // Window rules: SHOW_REPORT_SPEC section 8. Never throws; failures are reported in the result and getLastExportMessage().
    ExportResult exportReport();
    // Processor teardown: finalises an open take and exports it; otherwise exports a dirty history when `exportIfDirty`
    // and autoExport is on and the history covers at least 30 s. Releases the feedback analysis worker.
    void shutdown(bool exportIfDirty);

    // ---- UI queries (message thread)
    const EnergyTracker& tracker() const noexcept { return trk; }
    bool isTracking() const noexcept { return clockRunning; }
    int getPeakCount() const;                                   // peaks in the current report window
    float getCurrentCrowdDb() const noexcept { return trk.currentCrowdDb(); }
    void getRecentLevels(float* out, int count) const noexcept { trk.recentCrowdDb(out, count); }
    double getTrackedSeconds() const noexcept { return trk.empty() ? 0.0 : trk.lastTime() - trk.firstTime() + 1.0; }
    bool hasOpenTake() const noexcept { return takeActive; }
    juce::File getLastReportFolder() const { return lastFolder; }
    juce::String getLastExportMessage() const { return lastMessage; }
    juce::File getReportFolderForUi();                          // last export folder, else the record folder

private:
    void readAndPush(double t, double elapsed, bool fill);
    void syncSettings();
    void syncRecorderState(double elapsed);
    void harvestMarkers();
    juce::File recordFolder() const;
    juce::File chooseFolder(juce::String& sessionFileName);
    static std::string pickBaseName(const juce::File& folder, const juce::String& sessionFileName);
    ReportMeta makeMeta(double t0) const;

    struct RecorderSink final : MarkerSink {
        explicit RecorderSink(ShowReportService& s) : svc(s) {}
        bool isRecording() const override { return svc.recorder.getStatus().recording; }
        void addPeakMarker(const std::string& label, double secondsAgo) override;
        ShowReportService& svc;
    };

    CrowdEngine& engine;
    rec::RecorderEngine& recorder;
    ShowModel& show;
    EnergyTracker trk;
    RecorderSink defaultSink { *this };
    MarkerSink* sink = &defaultSink;
    std::function<DeviceStatus()> deviceProvider;
    std::function<void(bool)> analysisControl;
    std::function<juce::String()> sceneName;
    std::string appVersion = "0.0.0";

    // clock
    bool clockRunning = false;
    double origin = 0.0, nextT = 0.0, lastElapsed = 0.0;
    juce::int64 originEpochMs = 0;
    // settings applied
    float appliedSensitivity = -1.0f;
    bool analysisHeld = false;
    // report window
    bool takeActive = false, haveTake = false, dirty = false;
    double windowStart = 0.0, windowEnd = 0.0;
    juce::File takeSession;
    int takeNumber = 0;
    juce::String takeScene;
    bool wasRecording = false;
    TrackerInput lastIn;
    // markers
    std::set<std::pair<juce::int64, std::string>> seenMarkers;
    // export state
    juce::File lastFolder, fallbackFolder;
    juce::String lastMessage;
};
}
