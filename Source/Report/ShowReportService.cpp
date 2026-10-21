#include "ShowReportService.h"
#include <algorithm>
#include <cmath>
#include "Report/ReportWriters.h"

namespace crowdmike::report {
namespace {
constexpr double kOpenEnd = 1.0e18;
constexpr double kMinShutdownSeconds = 30.0;

float linearToDb(float lin) noexcept {
    if (!(lin > 1.0e-6f)) return kFloorDb;
    return std::min(kCeilDb, std::max(kFloorDb, 20.0f * std::log10(lin)));
}
std::uint32_t narrow(std::uint64_t v) noexcept { return static_cast<std::uint32_t>(v & 0xFFFFFFFFu); }
}

void ShowReportService::RecorderSink::addPeakMarker(const std::string& label, double secondsAgo) {
    const double sr = svc.engine.getSampleRate() > 0.0 ? svc.engine.getSampleRate() : 48000.0;
    const auto now = svc.recorder.getFramePosition();
    const auto frame = now - static_cast<juce::int64>(std::llround(std::max(0.0, secondsAgo) * sr));
    svc.recorder.addMarkerAt(frame, juce::String::fromUTF8(label.c_str()), rec::MarkerType::Applause);
}

ShowReportService::ShowReportService(CrowdEngine& e, rec::RecorderEngine& r, ShowModel& s) : engine(e), recorder(r), show(s) {}

ShowReportService::~ShowReportService() {
    if (analysisHeld && analysisControl) analysisControl(false);
}

void ShowReportService::resetHistory() {
    trk.reset();
    clockRunning = false;
    takeActive = haveTake = dirty = wasRecording = false;
    windowStart = windowEnd = 0.0;
    takeSession = juce::File();
    seenMarkers.clear();
    fallbackFolder = juce::File();
    lastFolder = juce::File();
    lastMessage = {};
    lastIn = TrackerInput {};
    nextT = lastElapsed = 0.0;
}

void ShowReportService::syncSettings() {
    const auto& rs = show.report;
    if (rs.peakSensitivity != appliedSensitivity) {
        appliedSensitivity = rs.peakSensitivity;
        trk.setPeakConfig(PeakConfig::fromSensitivity(rs.peakSensitivity));
    }
    const bool wantAnalysis = rs.enabled;
    if (wantAnalysis != analysisHeld && analysisControl) {
        analysisHeld = wantAnalysis;
        analysisControl(wantAnalysis);
    }
}

void ShowReportService::noteSceneRecall(const juce::String& name) {
    if (!clockRunning || !show.report.enabled) return;
    trk.noteSceneRecall(std::max(lastElapsed, trk.empty() ? 0.0 : trk.lastTime()), name.toStdString());
}

void ShowReportService::readAndPush(double t, double elapsed, bool fill) {
    TrackerInput in;
    if (fill) {
        in = lastIn;   // hold levels and counters (no deltas) across a stalled message thread
    } else {
        auto& m = engine.meters;
        in.crowdDb = m.crowdLevelDb.load(std::memory_order_relaxed);
        in.density = m.intensity.load(std::memory_order_relaxed);
        in.masterDb = m.masterRmsDb.load(std::memory_order_relaxed);
        in.masterPeakDb = linearToDb(m.reportMasterPeak.getAndReset());
        in.lufs = m.shortTermLufs.load(std::memory_order_relaxed);
        in.counters.clip = m.events.clipEvents.load(std::memory_order_relaxed);
        in.counters.limiter = m.events.limiterEvents.load(std::memory_order_relaxed);
        in.counters.panic = m.events.panicEvents.load(std::memory_order_relaxed);
        in.counters.echo = m.events.echoThrows.load(std::memory_order_relaxed);
        in.counters.reverse = m.events.reverseTriggers.load(std::memory_order_relaxed);
        in.counters.freeze = m.events.freezeEvents.load(std::memory_order_relaxed);
        in.counters.xrun = narrow(m.overruns.load(std::memory_order_relaxed));
        if (deviceProvider) {
            const auto st = deviceProvider();
            if (st.deviceOpen) in.counters.xrun = static_cast<std::uint32_t>(std::max(0, st.xruns));
        }
        in.recording = recorder.getStatus().recording;
        if (analysisHeld) {
            dsp::FeedbackCandidate c[4];
            const int n = engine.feedback().getCandidates(c, 4);
            for (int i = 0; i < n && i < 4; ++i) in.feedback[static_cast<size_t>(i)] = { c[i].frequencyHz, c[i].severity };
            in.feedbackCount = std::min(n, 4);
        }
    }
    in.t = t;
    lastIn = in;
    dirty = dirty || !haveTake || takeActive;
    const auto res = trk.push(in);
    if (res.newPeak && show.report.autoMarkers && sink->isRecording()) {
        const std::string label = std::string(kAutoMarkerPrefix) + " +" + formatNumber(res.peak.prominenceDb, 1) + " dB";
        sink->addPeakMarker(label, std::max(0.0, elapsed - res.peak.t));
        trk.markLastPeak();
    }
}

void ShowReportService::harvestMarkers() {
    if (seenMarkers.size() > 2000) seenMarkers.clear();
    for (const auto& m : recorder.getMarkers()) {
        const std::pair<juce::int64, std::string> key { m.frame, m.label.toStdString() };
        if (!seenMarkers.insert(key).second) continue;
        if (m.label.startsWith(kAutoMarkerPrefix)) continue;
        const double t = static_cast<double>(m.wallClock.toMilliseconds() - originEpochMs) / 1000.0;
        if (t < -1.0) continue;
        trk.noteMarker(std::min(std::max(t, 0.0), std::max(lastElapsed, 0.0)), key.second);
    }
}

void ShowReportService::syncRecorderState(double elapsed) {
    const auto st = recorder.getStatus();
    if (st.recording && !wasRecording) {
        takeActive = haveTake = dirty = true;
        windowStart = nextT;
        windowEnd = kOpenEnd;
        takeSession = recorder.getLastSessionFile();
        takeNumber = st.take;
        takeScene = sceneName ? sceneName() : juce::String();
    } else if (!st.recording && wasRecording) {
        takeActive = false;
        windowEnd = elapsed;
        if (recorder.getLastSessionFile() != juce::File()) takeSession = recorder.getLastSessionFile();
        if (show.report.autoExport) exportReport();
    }
    wasRecording = st.recording;
}

void ShowReportService::poll(double now) {
    syncSettings();
    if (!show.report.enabled) {
        clockRunning = false;
        return;
    }
    if (!clockRunning) {
        clockRunning = true;
        nextT = trk.empty() ? 0.0 : trk.lastTime() + 1.0;
        origin = now - nextT;
        originEpochMs = juce::Time::currentTimeMillis() - static_cast<juce::int64>(nextT * 1000.0);
        lastElapsed = nextT;
    }
    double elapsed = now - origin;
    if (elapsed < lastElapsed - 0.5) { origin = now - lastElapsed; elapsed = lastElapsed; }   // clock went backwards
    lastElapsed = std::max(lastElapsed, elapsed);

    if (elapsed >= nextT) {
        if (elapsed - nextT > 10.0) nextT = std::floor(elapsed);   // long stall: resynchronise instead of inventing history
        bool first = true;
        int guard = 0;
        while (nextT <= elapsed && guard++ < 12) {
            readAndPush(nextT, elapsed, !first);
            first = false;
            nextT += 1.0;
        }
        harvestMarkers();
    }
    syncRecorderState(elapsed);
}

int ShowReportService::getPeakCount() const {
    double t0 = trk.empty() ? 0.0 : trk.firstTime(), t1 = kOpenEnd;
    if (haveTake) { t0 = windowStart; t1 = takeActive ? kOpenEnd : windowEnd; }
    int n = 0;
    for (const auto& p : trk.peaks()) if (p.t >= t0 - 1e-9 && p.t <= t1 + 1e-9) ++n;
    return n;
}

juce::File ShowReportService::recordFolder() const {
    const auto& f = show.recorder.folder;
    return juce::File::isAbsolutePath(f) ? juce::File(f)
                                         : juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("CrowdMike");
}

juce::File ShowReportService::getReportFolderForUi() {
    if (lastFolder != juce::File() && lastFolder.isDirectory()) return lastFolder;
    return recordFolder();
}

ReportMeta ShowReportService::makeMeta(double t0) const {
    ReportMeta m;
    m.show = show.recorder.show.toStdString();
    m.venue = show.recorder.venue.toStdString();
    m.scene = (haveTake ? takeScene : (sceneName ? sceneName() : juce::String())).toStdString();
    m.appVersion = appVersion;
    m.sessionFile = haveTake ? takeSession.getFileName().toStdString() : std::string();
    m.take = haveTake ? takeNumber : 0;
    m.startEpochMs = originEpochMs + static_cast<juce::int64>(std::llround(t0 * 1000.0));
    m.utcOffsetMinutes = juce::Time(m.startEpochMs).getUTCOffsetSeconds() / 60;
    m.peakSensitivity = static_cast<double>(show.report.peakSensitivity);
    m.autoMarkers = show.report.autoMarkers;

    DeviceStatus st;
    if (deviceProvider) st = deviceProvider();
    auto& d = m.device;
    d.driver = st.typeName.toStdString();
    d.name = st.deviceName.toStdString();
    d.sampleRate = st.sampleRate > 0.0 ? st.sampleRate : engine.getSampleRate();
    d.bufferSize = st.bufferSize > 0 ? st.bufferSize : engine.meters.blockSize.load(std::memory_order_relaxed);
    d.latencySamples = engine.getLatencySamples() + d.bufferSize + std::max(0, st.inputLatencySamples) + std::max(0, st.outputLatencySamples);
    d.latencyMs = d.sampleRate > 0.0 ? 1000.0 * d.latencySamples / d.sampleRate : 0.0;
    d.inputs = engine.getPreparedInputs();
    d.outputs = engine.getPreparedOutputs();
    return m;
}

std::string ShowReportService::pickBaseName(const juce::File& folder, const juce::String& sessionFileName) {
    const std::string base = "show_report";
    const auto existing = folder.getChildFile("show_report.json");
    if (sessionFileName.isEmpty() || !existing.existsAsFile()) return base;
    const auto parsed = juce::JSON::parse(existing);
    const auto owner = parsed["session"]["file"].toString();
    if (parsed.isVoid() || owner == sessionFileName) return base;
    auto stem = juce::File(sessionFileName).getFileNameWithoutExtension();
    if (stem.endsWith("_session")) stem = stem.dropLastCharacters(8);
    return (base + "_" + juce::File::createLegalFileName(stem).toStdString());
}

juce::File ShowReportService::chooseFolder(juce::String& sessionFileName) {
    if (haveTake && takeSession != juce::File()) {
        sessionFileName = takeSession.getFileName();
        return takeSession.getParentDirectory();
    }
    sessionFileName = {};
    if (fallbackFolder == juce::File())
        fallbackFolder = recordFolder().getChildFile("Reports").getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S"));
    return fallbackFolder;
}

ExportResult ShowReportService::exportReport() {
    ExportResult r;
    try {
        if (trk.empty()) {
            r.error = "No data to report yet";
            lastMessage = "Show report: " + r.error;
            return r;
        }
        double t0 = trk.firstTime(), t1 = kOpenEnd;
        if (haveTake) { t0 = windowStart; t1 = takeActive ? kOpenEnd : windowEnd; }
        const auto data = trk.buildReport(makeMeta(t0), t0, t1);
        if (data.series.empty()) {
            r.error = "No data to report yet";
            lastMessage = "Show report: " + r.error;
            return r;
        }
        juce::String sessionName;
        r.folder = chooseFolder(sessionName);
        if (!r.folder.isDirectory() && !r.folder.createDirectory().wasOk()) {
            r.error = "Cannot create folder " + r.folder.getFullPathName();
            lastMessage = "Show report: " + r.error;
            return r;
        }
        const auto base = juce::String(pickBaseName(r.folder, sessionName));
        r.html = r.folder.getChildFile(base + ".html");
        r.csv = r.folder.getChildFile(base + ".csv");
        r.json = r.folder.getChildFile(base + ".json");
        const auto html = renderHtml(data), csv = renderCsv(data), json = renderJson(data);
        const bool ok = r.html.replaceWithData(html.data(), html.size()) && r.csv.replaceWithData(csv.data(), csv.size())
                     && r.json.replaceWithData(json.data(), json.size());
        if (!ok) {
            r.error = "Cannot write report files in " + r.folder.getFullPathName();
            lastMessage = "Show report: " + r.error;
            return r;
        }
        r.ok = true;
        lastFolder = r.folder;
        if (!takeActive) dirty = false;
        lastMessage = "Show report saved: " + r.folder.getFullPathName();
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = juce::String("Unexpected error: ") + e.what();
        lastMessage = "Show report: " + r.error;
    } catch (...) {
        r.ok = false;
        r.error = "Unexpected error";
        lastMessage = "Show report: " + r.error;
    }
    return r;
}

void ShowReportService::shutdown(bool exportIfDirty) {
    try {
        if (clockRunning) {
            if (recorder.getStatus().recording) {
                syncRecorderState(lastElapsed);   // latch a take that started since the last poll
                recorder.stopRecordingNow();
                syncRecorderState(lastElapsed);   // falling edge -> exports the take when autoExport is on
            } else if (exportIfDirty && show.report.autoExport && dirty && getTrackedSeconds() >= kMinShutdownSeconds) {
                exportReport();
            }
        }
    } catch (...) {
    }
    if (analysisHeld && analysisControl) { analysisHeld = false; analysisControl(false); }
}
}
