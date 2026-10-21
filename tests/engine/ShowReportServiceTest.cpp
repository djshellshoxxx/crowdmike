// Show Report integration: engine event counters, ShowModel settings migration, marker injection (fake sink and a real
// recorder), export triggers, and the Live / Record page panels (SHOW_REPORT_SPEC section 12).
#include "TestHarness.h"
#include "PluginEditor.h"
#include "Engine/TapMap.h"
#include "Report/ShowReportService.h"
#include "UI/ReportPanels.h"
#include "UI/Ticker.h"
#include "UiTestUtil.h"
#include <cmath>
#include <cstdlib>

using namespace crowdmike;
using namespace crowdmike::ui;
using uitest::click;
using uitest::findById;

namespace {
void setParam(CrowdMikeAudioProcessor& p, const char* id, float actual) {
    auto* q = p.getParameters().getParameter(id);
    q->setValueNotifyingHost(q->convertTo0to1(actual));
}

struct Feed {
    CrowdMikeAudioProcessor& p;
    long long n = 0;
    explicit Feed(CrowdMikeAudioProcessor& proc) : p(proc) {}
    void blocks(int count, float amp) {
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer midi;
        for (int k = 0; k < count; ++k) {
            for (int s = 0; s < 256; ++s, ++n) {
                const float v = amp * std::sin(0.13f * static_cast<float>(n % 100000));
                b.setSample(0, s, v);
                b.setSample(1, s, v);
            }
            p.processBlock(b, midi);
        }
    }
    // One second of audio at `amp`, then one service poll at fake time t.
    void second(double t, float amp) {
        blocks(188, amp);
        p.getReport().poll(t);
    }
};

struct FakeSink final : report::MarkerSink {
    bool recording = true;
    std::vector<std::pair<std::string, double>> markers;
    bool isRecording() const override { return recording; }
    void addPeakMarker(const std::string& label, double secondsAgo) override { markers.emplace_back(label, secondsAgo); }
};

// 60 s of quiet, then `swells` swells (6 s loud, 40 s quiet) so every swell is clearly prominent and well spaced.
int runSwells(Feed& f, int swells, double& t) {
    for (int i = 0; i < 60; ++i) f.second(t++, 0.01f);
    for (int s = 0; s < swells; ++s) {
        for (int i = 0; i < 6; ++i) f.second(t++, 0.4f);
        for (int i = 0; i < 40; ++i) f.second(t++, 0.01f);
    }
    return swells;
}

struct Rig {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> base;
    CrowdMikeAudioProcessorEditor* editor = nullptr;
    Rig() {
        p.prepareToPlay(48000.0, 256);
        p.applyWiringTemplate(2);
        base.reset(p.createEditor());
        editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
        if (editor != nullptr) editor->setSize(1600, 900);
    }
};
juce::File snapshotDir() {
    if (const char* d = std::getenv("CM_SNAPSHOT_DIR")) { juce::File f(d); f.createDirectory(); return f; }
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_snapshots");
    d.createDirectory();
    return d;
}
void snap(CrowdMikeAudioProcessorEditor& e, const juce::String& name) {
    Tickable::tickAllForTesting();
    auto img = e.createComponentSnapshot(e.getLocalBounds(), true, 1.0f);
    auto f = snapshotDir().getChildFile(name + ".png");
    f.deleteFile();
    juce::FileOutputStream out(f);
    CM_EXPECT(out.openedOk() && juce::PNGImageFormat().writeImageToStream(img, out));
}
}

CM_TEST(report_engine_counters_fire_once_per_edge_and_latch_on_first_chunk) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(2);
    Feed f(p);
    auto& ev = p.getMeters().events;
    f.blocks(4, 0.1f);
    CM_EXPECT(ev.panicEvents.load() == 0u && ev.freezeEvents.load() == 0u && ev.echoThrows.load() == 0u);
    setParam(p, "panic", 1.0f);
    f.blocks(10, 0.1f);
    CM_EXPECT(ev.panicEvents.load() == 1u);        // held for many blocks: still one event
    setParam(p, "panic", 0.0f);
    f.blocks(4, 0.1f);
    setParam(p, "panic", 1.0f);
    f.blocks(4, 0.1f);
    CM_EXPECT(ev.panicEvents.load() == 2u);
    setParam(p, "panic", 0.0f);
    setParam(p, "freeze", 1.0f);
    f.blocks(6, 0.1f);
    setParam(p, "freeze", 0.0f);
    f.blocks(2, 0.1f);
    CM_EXPECT(ev.freezeEvents.load() == 1u);
    setParam(p, "fx.echo.trigger", 1.0f);
    f.blocks(6, 0.1f);
    CM_EXPECT(ev.echoThrows.load() == 1u);
    setParam(p, "fx.echo.trigger", 0.0f);
    f.blocks(2, 0.1f);

    // A control that is already on when processing starts is latched, not counted.
    CrowdMikeAudioProcessor q;
    setParam(q, "panic", 1.0f);
    q.prepareToPlay(48000.0, 256);
    Feed g(q);
    g.blocks(6, 0.1f);
    CM_EXPECT(q.getMeters().events.panicEvents.load() == 0u);
}

CM_TEST(report_engine_clip_and_limiter_episodes_are_counted_not_blocks) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, "input1.trimDb", 24.0f);
    Feed f(p);
    f.blocks(200, 0.9f);
    const auto lim = p.getMeters().events.limiterEvents.load();
    CM_EXPECT(lim >= 1u && lim < 20u);
    f.blocks(400, 0.0f);
    f.blocks(200, 0.9f);
    CM_EXPECT(p.getMeters().events.limiterEvents.load() > lim);
}

CM_TEST(report_engine_feeds_levels_and_loudness) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(2);
    Feed f(p);
    f.blocks(1200, 0.25f);
    CM_EXPECT(p.getMeters().masterRmsDb.load() > -40.0f);
    CM_EXPECT(p.getMeters().shortTermLufs.load() > -60.0f);
    CM_EXPECT(p.getMeters().reportMasterPeak.getAndReset() > 0.01f);
}

CM_TEST(report_show_model_roundtrip_and_old_state_loads_with_defaults) {
    ShowModel a;
    a.report.enabled = false;
    a.report.autoMarkers = false;
    a.report.autoExport = false;
    a.report.peakSensitivity = 0.8f;
    ShowModel b;
    CM_EXPECT(b.fromValueTree(a.toValueTree()));
    CM_EXPECT(!b.report.enabled && !b.report.autoMarkers && !b.report.autoExport);
    CM_EXPECT_NEAR(b.report.peakSensitivity, 0.8, 1e-6);

    // Regression: a tree saved before the feature (no Report child) loads with defaults and is otherwise identical.
    ShowModel c;
    c.recorder.preRollSeconds = 7;
    auto tree = c.toValueTree();
    auto child = tree.getChildWithName("Report");
    CM_EXPECT(child.isValid());
    tree.removeChild(child, nullptr);
    ShowModel d;
    d.report.enabled = false;                       // must be overwritten by the defaults
    CM_EXPECT(d.fromValueTree(tree));
    CM_EXPECT(d.report.enabled && d.report.autoMarkers && d.report.autoExport);
    CM_EXPECT_NEAR(d.report.peakSensitivity, 0.5, 1e-6);
    CM_EXPECT(d.recorder.preRollSeconds == 7);
    CM_EXPECT(d.toValueTree().getChildWithName("Report").isValid());
    // Everything except the new child serialises identically.
    auto again = d.toValueTree();
    again.removeChild(again.getChildWithName("Report"), nullptr);
    CM_EXPECT(again.isEquivalentTo(tree));
}

CM_TEST(report_service_injects_peak_markers_into_sink_with_time_offset) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(2);
    FakeSink sink;
    p.getReport().setMarkerSink(&sink);
    Feed f(p);
    double t = 0.0;
    runSwells(f, 3, t);
    CM_EXPECT(sink.markers.size() == 3u);
    CM_EXPECT(p.getReport().getPeakCount() >= 3);
    for (const auto& m : sink.markers) {
        CM_EXPECT(juce::String(m.first).startsWith("Crowd peak +"));
        CM_EXPECT(juce::String(m.first).endsWith(" dB"));
        CM_EXPECT(m.second >= 0.0 && m.second < 30.0);
    }
}

CM_TEST(report_service_does_not_mark_when_not_recording_or_disabled) {
    {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.applyWiringTemplate(2);
        FakeSink sink;
        sink.recording = false;
        p.getReport().setMarkerSink(&sink);
        Feed f(p);
        double t = 0.0;
        runSwells(f, 2, t);
        CM_EXPECT(sink.markers.empty());
        CM_EXPECT(p.getReport().getPeakCount() >= 2);   // still tracked
    }
    {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.applyWiringTemplate(2);
        p.getShow().report.autoMarkers = false;
        FakeSink sink;
        p.getReport().setMarkerSink(&sink);
        Feed f(p);
        double t = 0.0;
        runSwells(f, 2, t);
        CM_EXPECT(sink.markers.empty());
    }
    {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.applyWiringTemplate(2);
        p.getShow().report.enabled = false;
        Feed f(p);
        double t = 0.0;
        runSwells(f, 2, t);
        CM_EXPECT(p.getReport().getPeakCount() == 0);
    }
}

CM_TEST(report_export_on_demand_writes_three_files) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_report_demand");
    dir.deleteRecursively();
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(2);
    p.getShow().recorder.folder = dir.getFullPathName();
    Feed f(p);
    double t = 0.0;
    runSwells(f, 2, t);
    auto r = p.getReport().exportReport();
    CM_EXPECT(r.ok);
    CM_EXPECT(r.html.existsAsFile() && r.csv.existsAsFile() && r.json.existsAsFile());
    CM_EXPECT(r.html.loadFileAsString().contains("<svg"));
    CM_EXPECT(r.csv.loadFileAsString().startsWith("time_s,"));
    CM_EXPECT(juce::JSON::parse(r.json.loadFileAsString()).isObject());
    dir.deleteRecursively();
}

CM_TEST(report_recorder_stop_exports_into_session_folder_and_second_take_does_not_overwrite) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_report_rec");
    dir.deleteRecursively();
    Rig rig;
    if (rig.editor == nullptr) { CM_EXPECT(false); return; }
    auto& p = rig.p;
    p.getShow().recorder.folder = dir.getFullPathName();
    rig.editor->showPage(PageId::Record);
    click(findById(*rig.editor, "rec.autoAssign"));
    click(findById(*rig.editor, "rec.recordAll"));
    Feed f(p);
    double t = 0.0;

    auto takeOnce = [&]() {
        CM_EXPECT(p.startRecording());
        for (int i = 0; i < 60; ++i) f.second(t++, 0.01f);
        for (int i = 0; i < 6; ++i) f.second(t++, 0.4f);
        for (int i = 0; i < 30; ++i) f.second(t++, 0.01f);
        p.getRecorder().stopRecordingNow();
        f.second(t++, 0.01f);                         // falling edge -> auto export
        f.second(t++, 0.01f);
    };
    takeOnce();
    int markers = 0;
    for (const auto& m : p.getRecorder().getMarkers()) markers += m.label.startsWith("Crowd peak") ? 1 : 0;
    CM_EXPECT(markers >= 1);
    auto jsons = dir.findChildFiles(juce::File::findFiles, true, "show_report*.json");
    CM_EXPECT(jsons.size() == 1);
    CM_EXPECT(dir.findChildFiles(juce::File::findFiles, true, "show_report*.html").size() == 1);
    CM_EXPECT(dir.findChildFiles(juce::File::findFiles, true, "show_report*.csv").size() == 1);
    const auto firstContent = jsons.size() == 1 ? jsons[0].loadFileAsString() : juce::String();

    takeOnce();
    auto jsons2 = dir.findChildFiles(juce::File::findFiles, true, "show_report*.json");
    CM_EXPECT(jsons2.size() == 2);
    for (const auto& j : jsons2) CM_EXPECT(juce::JSON::parse(j.loadFileAsString()).isObject());
    for (const auto& j : jsons)
        if (j.existsAsFile()) CM_EXPECT(j.loadFileAsString() == firstContent);
    dir.deleteRecursively();
}

CM_TEST(report_shutdown_exports_dirty_history_only_when_asked) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_report_shutdown");
    dir.deleteRecursively();
    {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.applyWiringTemplate(2);
        p.getShow().recorder.folder = dir.getFullPathName();
        Feed f(p);
        double t = 0.0;
        runSwells(f, 1, t);
        p.getReport().shutdown(false);
        CM_EXPECT(dir.findChildFiles(juce::File::findFiles, true, "show_report*").isEmpty());
        p.getReport().shutdown(true);
        CM_EXPECT(dir.findChildFiles(juce::File::findFiles, true, "show_report*.json").size() == 1);
    }
    dir.deleteRecursively();
}

CM_TEST(report_ui_panels_exist_and_edit_the_show_model) {
    Rig rig;
    if (rig.editor == nullptr) { CM_EXPECT(false); return; }
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_report_ui");
    dir.deleteRecursively();
    rig.p.getShow().recorder.folder = dir.getFullPathName();

    rig.editor->showPage(PageId::Live);
    Tickable::tickAllForTesting();
    auto* spark = dynamic_cast<EnergySparkline*>(findById(*rig.editor, "live.energy"));
    CM_EXPECT(spark != nullptr);
    if (spark != nullptr) CM_EXPECT(spark->peaksText().startsWith("PEAKS "));
    snap(*rig.editor, "Report_Live");

    rig.editor->showPage(PageId::Record);
    Tickable::tickAllForTesting();
    CM_EXPECT(findById(*rig.editor, "report.openFolder") != nullptr);
    CM_EXPECT(findById(*rig.editor, "report.exportNow") != nullptr);
    click(findById(*rig.editor, "report.autoExport"));
    CM_EXPECT(!rig.p.getShow().report.autoExport);
    click(findById(*rig.editor, "report.autoMarkers"));
    CM_EXPECT(!rig.p.getShow().report.autoMarkers);
    auto* sens = dynamic_cast<juce::Slider*>(findById(*rig.editor, "report.sensitivity"));
    CM_EXPECT(sens != nullptr);
    if (sens != nullptr) {
        sens->setValue(90.0, juce::sendNotificationSync);
        CM_EXPECT_NEAR(rig.p.getShow().report.peakSensitivity, 0.9, 0.02);
    }
    click(findById(*rig.editor, "report.exportNow"));
    Tickable::tickAllForTesting();
    CM_EXPECT(findById(*rig.editor, "report.status") != nullptr);
    for (auto size : { juce::Point<int>(1100, 650), juce::Point<int>(2560, 1440) }) {
        rig.editor->setSize(size.x, size.y);
        snap(*rig.editor, "Report_Record_" + juce::String(size.x));
        rig.editor->showPage(PageId::Live);
        snap(*rig.editor, "Report_Live_" + juce::String(size.x));
        rig.editor->showPage(PageId::Record);
    }
    dir.deleteRecursively();
}
