// Crowd Energy Tracker + peak detector (SHOW_REPORT_SPEC sections 5, 6): synthetic crowd swells, bounds, counters, windows.
#include "TestHarness.h"
#include "Report/EnergyTracker.h"
#include <cmath>
#include <limits>

using namespace crowdmike::report;

namespace {
struct Bump { double center, height, sigma; };

double levelAt(double t, const std::vector<Bump>& bumps, double base = -40.0) {
    double v = base + 0.3 * std::sin(t * 1.7);
    for (const auto& b : bumps) v += b.height * std::exp(-0.5 * (t - b.center) * (t - b.center) / (b.sigma * b.sigma));
    return v;
}

TrackerInput inputAt(double t, double level) {
    TrackerInput in;
    in.t = t;
    in.crowdDb = static_cast<float>(level);
    in.masterDb = static_cast<float>(level + 3.0);
    in.lufs = static_cast<float>(level + 1.0);
    in.masterPeakDb = static_cast<float>(level + 9.0);
    in.density = 0.4f;
    return in;
}

void feed(EnergyTracker& tr, int seconds, const std::vector<Bump>& bumps, int from = 0) {
    for (int t = from; t < from + seconds; ++t) tr.push(inputAt(t, levelAt(t, bumps)));
}

ReportData whole(const EnergyTracker& tr) { return tr.buildReport({}, 0.0, 1.0e12); }
}

CM_TEST(report_peak_sensitivity_mapping_is_monotonic_and_safe) {
    const auto lo = PeakConfig::fromSensitivity(0.0f), mid = PeakConfig::fromSensitivity(0.5f), hi = PeakConfig::fromSensitivity(1.0f);
    CM_EXPECT_NEAR(lo.prominenceDb, 12.0, 1e-4);
    CM_EXPECT_NEAR(mid.prominenceDb, 7.5, 1e-4);
    CM_EXPECT_NEAR(hi.prominenceDb, 3.0, 1e-4);
    CM_EXPECT_NEAR(lo.minSpacingSec, 45.0, 1e-4);
    CM_EXPECT_NEAR(mid.minSpacingSec, 28.5, 1e-4);
    CM_EXPECT_NEAR(hi.minSpacingSec, 12.0, 1e-4);
    CM_EXPECT_NEAR(PeakConfig::fromSensitivity(7.0f).prominenceDb, 3.0, 1e-4);
    CM_EXPECT_NEAR(PeakConfig::fromSensitivity(-3.0f).prominenceDb, 12.0, 1e-4);
    CM_EXPECT_NEAR(PeakConfig::fromSensitivity(std::numeric_limits<float>::quiet_NaN()).prominenceDb, 7.5, 1e-4);
    CM_EXPECT_NEAR(lo.confirmDropDb(), 4.0, 1e-4);
    CM_EXPECT_NEAR(hi.confirmDropDb(), 2.0, 1e-4);
}

CM_TEST(report_peaks_are_detected_at_the_expected_times_and_small_bumps_are_ignored) {
    EnergyTracker tr;
    const std::vector<Bump> bumps { { 100, 14, 3 }, { 200, 12, 3 }, { 300, 3, 3 }, { 400, 16, 3 } };
    int announced = 0;
    for (int t = 0; t < 500; ++t) {
        const auto r = tr.push(inputAt(t, levelAt(t, bumps)));
        if (r.newPeak) {
            ++announced;
            CM_EXPECT(r.peak.confirmedAt >= r.peak.t);
            CM_EXPECT(r.peak.confirmedAt - r.peak.t <= 8.0);   // confirmed within a few seconds of the maximum
        }
    }
    const auto& pk = tr.peaks();
    CM_EXPECT(announced == 3);
    CM_EXPECT(pk.size() == 3);
    if (pk.size() == 3) {
        CM_EXPECT_NEAR(pk[0].t, 100.0, 2.0);
        CM_EXPECT_NEAR(pk[1].t, 200.0, 2.0);
        CM_EXPECT_NEAR(pk[2].t, 400.0, 2.0);
        CM_EXPECT(pk[0].prominenceDb >= 7.5f && pk[0].prominenceDb < 15.0f);
        CM_EXPECT(pk[2].prominenceDb > pk[1].prominenceDb);
        CM_EXPECT_NEAR(pk[0].levelDb, -26.0, 2.0);
    }
    const auto rep = whole(tr);
    CM_EXPECT(rep.summary.peakCount == 3);
    CM_EXPECT(rep.topMoments.size() == 3);
    if (!rep.topMoments.empty()) CM_EXPECT_NEAR(rep.topMoments.front().peak.t, 400.0, 2.0);
}

CM_TEST(report_peaks_closer_than_the_minimum_spacing_are_suppressed_and_counted) {
    EnergyTracker tr;
    feed(tr, 300, { { 100, 14, 3 }, { 112, 14, 3 } });
    CM_EXPECT(tr.peaks().size() == 1);
    CM_EXPECT_NEAR(tr.peaks().empty() ? 0.0 : tr.peaks()[0].t, 100.0, 2.5);
    CM_EXPECT(whole(tr).summary.peaksSuppressed == 1);
    // the same two bumps far enough apart are both reported
    EnergyTracker tr2;
    feed(tr2, 300, { { 100, 14, 3 }, { 160, 14, 3 } });
    CM_EXPECT(tr2.peaks().size() == 2);
}

CM_TEST(report_silence_and_swells_below_the_absolute_floor_never_produce_peaks) {
    EnergyTracker silent;
    for (int t = 0; t < 600; ++t) silent.push(inputAt(t, -120.0));
    CM_EXPECT(silent.peaks().empty());
    CM_EXPECT(whole(silent).summary.averageCrowdDb == kFloorDb);

    EnergyTracker quiet;   // 30 dB swings but entirely below the -60 dBFS floor
    for (int t = 0; t < 600; ++t) quiet.push(inputAt(t, (t / 20) % 2 == 0 ? -95.0 : -62.0));
    CM_EXPECT(quiet.peaks().empty());
}

CM_TEST(report_sensitivity_controls_how_many_peaks_are_found) {
    const std::vector<Bump> bumps { { 100, 8, 4 }, { 300, 8, 4 } };
    EnergyTracker low, high;
    low.setPeakConfig(PeakConfig::fromSensitivity(0.0f));
    high.setPeakConfig(PeakConfig::fromSensitivity(1.0f));
    feed(low, 450, bumps);
    feed(high, 450, bumps);
    CM_EXPECT(low.peaks().empty());
    CM_EXPECT(high.peaks().size() == 2);
}

CM_TEST(report_series_stays_bounded_and_covers_the_whole_show_after_decimation) {
    EnergyTracker::Config cfg;
    cfg.maxSamples = 1000;
    EnergyTracker tr(cfg);
    for (int t = 0; t < 100000; ++t) {
        auto in = inputAt(t, -30.0);
        in.counters.clip = static_cast<std::uint32_t>(t / 100);
        in.recording = t >= 50000;
        tr.push(in);
        CM_EXPECT(tr.series().size() <= 1000);
    }
    const int step = tr.stepSeconds();
    CM_EXPECT(step >= 100 && (step & (step - 1)) == 0);   // power of two
    CM_EXPECT(tr.series().front().t == 0.0);
    CM_EXPECT(tr.memoryBytes() < 1000000u);
    const auto rep = whole(tr);
    CM_EXPECT(rep.series.size() <= 1001);
    CM_EXPECT(rep.stepSeconds == static_cast<double>(step));
    CM_EXPECT_NEAR(rep.summary.durationSeconds, 100000.0, 1.0);
    CM_EXPECT(rep.summary.clipEvents == 999);                      // event counts are exact despite decimation
    CM_EXPECT_NEAR(rep.summary.averageCrowdDb, -30.0, 0.01);       // power mean of a constant
    CM_EXPECT_NEAR(rep.summary.recordedSeconds, 50000.0, static_cast<double>(step));
    for (const auto& s : rep.series) CM_EXPECT_NEAR(s.crowdDb, -30.0, 0.01);
}

CM_TEST(report_default_capacity_is_twelve_hours_at_one_hertz) {
    EnergyTracker tr;
    for (int t = 0; t < 43200; ++t) tr.push(inputAt(t, -40.0));
    CM_EXPECT(tr.series().size() == 43200 && tr.stepSeconds() == 1);
    tr.push(inputAt(43200, -40.0));
    tr.push(inputAt(43201, -40.0));
    CM_EXPECT(tr.stepSeconds() == 2);
    CM_EXPECT(tr.series().size() <= 21601);
    CM_EXPECT(tr.series().size() >= 21000);
}

CM_TEST(report_peak_list_is_capped_and_keeps_the_most_prominent) {
    EnergyTracker::Config cfg;
    cfg.maxPeaks = 5;
    cfg.maxEvents = 5;
    EnergyTracker tr(cfg);
    tr.setPeakConfig(PeakConfig::fromSensitivity(1.0f));
    std::vector<Bump> bumps;
    for (int k = 0; k < 12; ++k) bumps.push_back({ 100.0 + 60.0 * k, k == 3 ? 20.0 : 9.0, 3.0 });
    feed(tr, 900, bumps);
    CM_EXPECT(tr.peaks().size() == 5);
    bool kept = false;
    for (const auto& p : tr.peaks()) kept = kept || std::fabs(p.t - 280.0) < 3.0;   // the 20 dB bump survives the cap
    CM_EXPECT(kept);
    for (int i = 0; i < 20; ++i) tr.noteMarker(i, "m");
    CM_EXPECT(whole(tr).markers.size() == 5);
}

CM_TEST(report_counters_become_deltas_and_a_reset_never_goes_negative) {
    EnergyTracker tr;
    TrackerInput in = inputAt(0, -40.0);
    in.counters.clip = 5; in.counters.panic = 2; in.counters.xrun = 7; in.counters.echo = 4;
    tr.push(in);                                            // first sample only establishes the baseline
    in = inputAt(1, -40.0); in.counters.clip = 6; in.counters.panic = 2; in.counters.xrun = 7; in.counters.echo = 7; in.counters.freeze = 1;
    tr.push(in);
    in = inputAt(2, -40.0); in.counters.clip = 6; in.counters.panic = 3; in.counters.xrun = 9; in.counters.echo = 7; in.counters.freeze = 1; in.counters.reverse = 2;
    tr.push(in);
    in = inputAt(3, -40.0); in.counters.clip = 0; in.counters.panic = 0; in.counters.xrun = 0;   // engine re-prepared: counters restart
    in.counters.echo = 7; in.counters.freeze = 1; in.counters.reverse = 2;
    tr.push(in);
    in = inputAt(4, -40.0); in.counters.clip = 1; in.counters.panic = 1; in.counters.xrun = 1; in.counters.echo = 7; in.counters.freeze = 1; in.counters.reverse = 2;
    tr.push(in);
    const auto rep = whole(tr);
    CM_EXPECT(rep.summary.clipEvents == 2);
    CM_EXPECT(rep.summary.panicEvents == 2);
    CM_EXPECT(rep.summary.xruns == 3);
    CM_EXPECT(rep.summary.fx.echoThrows == 3);
    CM_EXPECT(rep.summary.fx.freezes == 1);
    CM_EXPECT(rep.summary.fx.reverseTriggers == 2);
    CM_EXPECT(rep.summary.fx.total() == 6);
    CM_EXPECT(rep.series[1].fx == 4 && rep.series[2].fx == 2);
}

CM_TEST(report_feedback_candidates_count_once_per_episode) {
    EnergyTracker tr;
    auto withFb = [&](double t, std::initializer_list<FeedbackObs> obs) {
        auto in = inputAt(t, -40.0);
        for (const auto& o : obs) in.feedback[static_cast<size_t>(in.feedbackCount++)] = o;
        return tr.push(in);
    };
    CM_EXPECT(withFb(0, {}).newFeedback == 0);
    CM_EXPECT(withFb(1, { { 1000.0f, 0.8f } }).newFeedback == 1);
    CM_EXPECT(withFb(2, { { 1050.0f, 0.9f } }).newFeedback == 0);                      // same howl, drifted less than 1/6 octave
    CM_EXPECT(withFb(3, { { 1050.0f, 0.9f }, { 2500.0f, 0.7f } }).newFeedback == 1);   // a second frequency joins
    CM_EXPECT(withFb(4, { { 3000.0f, 0.3f } }).newFeedback == 0);                      // below severity 0.5: ignored
    CM_EXPECT(withFb(5, {}).newFeedback == 0);
    CM_EXPECT(withFb(6, { { 1000.0f, 0.8f } }).newFeedback == 1);                      // came back after a gap: new event
    const auto rep = whole(tr);
    CM_EXPECT(rep.summary.feedbackEvents == 3);
    CM_EXPECT(rep.feedbackEvents.size() == 3);
    if (rep.feedbackEvents.size() == 3) {
        CM_EXPECT_NEAR(rep.feedbackEvents[1].frequencyHz, 2500.0, 0.1);
        CM_EXPECT_NEAR(rep.feedbackEvents[2].t, 6.0, 1e-9);
    }
}

CM_TEST(report_window_slice_rebases_times_and_filters_events) {
    EnergyTracker tr;
    for (int t = 0; t < 300; ++t) {
        if (t == 150) tr.noteMarker(150.0, "drop");
        if (t == 160) tr.noteSceneRecall(160.0, "Chorus");
        tr.push(inputAt(t, -40.0));
    }
    tr.noteMarker(10.0, "outside");
    tr.noteSceneRecall(20.0, "Intro");
    const auto rep = tr.buildReport({}, 100.0, 199.5);
    CM_EXPECT(rep.series.size() == 100);
    CM_EXPECT(rep.series.front().t == 0.0 && rep.series.back().t == 99.0);
    CM_EXPECT_NEAR(rep.summary.durationSeconds, 99.5, 0.01);
    CM_EXPECT(rep.markers.size() == 1 && rep.markers[0].label == "drop");
    if (!rep.markers.empty()) CM_EXPECT_NEAR(rep.markers[0].t, 50.0, 1e-9);
    CM_EXPECT(rep.scenes.size() == 1 && rep.scenes[0].name == "Chorus");
    CM_EXPECT(rep.sceneEvents.size() == 1);
    CM_EXPECT(rep.summary.sceneRecalls == 1);
    CM_EXPECT(tr.buildReport({}, 5000.0, 6000.0).series.empty());
    CM_EXPECT(EnergyTracker().buildReport({}, 0.0, 1.0).series.empty());
}

CM_TEST(report_summary_ranks_the_top_five_moments_and_aggregates_scene_use) {
    EnergyTracker tr;
    const std::vector<Bump> bumps { { 100, 10, 3 }, { 200, 14, 3 }, { 300, 12, 3 }, { 400, 16, 3 }, { 500, 9, 3 }, { 600, 11, 3 }, { 700, 13, 3 } };
    for (int t = 0; t < 800; ++t) {
        if (t == 50) tr.noteSceneRecall(50, "A");
        if (t == 60) tr.noteSceneRecall(60, "B");
        if (t == 70) tr.noteSceneRecall(70, "A");
        tr.push(inputAt(t, levelAt(t, bumps)));
    }
    const auto rep = whole(tr);
    CM_EXPECT(rep.summary.peakCount == 7);
    CM_EXPECT(rep.topMoments.size() == 5);
    for (size_t i = 0; i < rep.topMoments.size(); ++i) {
        CM_EXPECT(rep.topMoments[i].rank == static_cast<int>(i) + 1);
        if (i > 0) CM_EXPECT(rep.topMoments[i - 1].peak.prominenceDb >= rep.topMoments[i].peak.prominenceDb);
    }
    if (rep.topMoments.size() == 5) {
        CM_EXPECT_NEAR(rep.topMoments[0].peak.t, 400.0, 2.0);
        CM_EXPECT_NEAR(rep.topMoments[1].peak.t, 200.0, 2.0);
        CM_EXPECT_NEAR(rep.topMoments[2].peak.t, 700.0, 2.0);
        CM_EXPECT_NEAR(rep.topMoments[4].peak.t, 600.0, 2.0);   // the 10 dB and 9 dB bumps (t=100, 500) fall out of the top five
        for (const auto& t : rep.topMoments) CM_EXPECT(std::fabs(t.peak.t - 100.0) > 5.0 && std::fabs(t.peak.t - 500.0) > 5.0);
    }
    CM_EXPECT(rep.scenes.size() == 2);
    if (rep.scenes.size() == 2) {
        CM_EXPECT(rep.scenes[0].name == "A" && rep.scenes[0].recalls == 2 && rep.scenes[0].firstSeconds == 50.0);
        CM_EXPECT(rep.scenes[1].name == "B" && rep.scenes[1].recalls == 1);
    }
    CM_EXPECT(rep.summary.sceneRecalls == 3);
    CM_EXPECT(rep.summary.peakCrowdDb > -30.0f);
    CM_EXPECT_NEAR(rep.summary.peakCrowdAtSeconds, 400.0, 2.0);
    CM_EXPECT_NEAR(rep.summary.maxMasterPeakDb, rep.summary.peakCrowdDb + 9.0, 1.5);
}

CM_TEST(report_inactive_samples_do_not_drag_the_average_down) {
    EnergyTracker tr;
    for (int t = 0; t < 10; ++t) tr.push(inputAt(t, -30.0));
    for (int t = 10; t < 20; ++t) tr.push(inputAt(t, -120.0));
    const auto s = whole(tr).summary;
    CM_EXPECT_NEAR(s.averageCrowdDb, -30.0, 0.01);
    CM_EXPECT_NEAR(s.peakCrowdDb, -30.0, 0.01);
    CM_EXPECT_NEAR(s.durationSeconds, 20.0, 1e-9);
    CM_EXPECT_NEAR(s.maxLufs, -29.0, 0.01);
    CM_EXPECT_NEAR(s.meanLufs, -29.0, 0.01);
}

CM_TEST(report_open_swell_is_included_when_the_show_ends_during_applause) {
    EnergyTracker tr;
    feed(tr, 101, { { 100, 14, 3 } });   // stops right at the maximum, the detector has not confirmed it yet
    CM_EXPECT(tr.peaks().empty());
    CM_EXPECT(tr.buildReport({}, 0.0, 1.0e9, true).peaks.size() == 1);
    CM_EXPECT(tr.buildReport({}, 0.0, 1.0e9, false).peaks.empty());
}

CM_TEST(report_ignores_non_increasing_and_non_finite_input) {
    EnergyTracker tr;
    tr.push(inputAt(5, -40.0));
    tr.push(inputAt(5, -10.0));    // same time: ignored
    tr.push(inputAt(3, -10.0));    // backwards: ignored
    CM_EXPECT(tr.sampleCount() == 1);
    TrackerInput bad = inputAt(6, 0.0);
    bad.crowdDb = std::numeric_limits<float>::quiet_NaN();
    bad.masterDb = std::numeric_limits<float>::infinity();
    bad.density = std::numeric_limits<float>::quiet_NaN();
    bad.lufs = 1.0e9f;
    tr.push(bad);
    TrackerInput nanTime = inputAt(std::numeric_limits<double>::quiet_NaN(), -40.0);
    tr.push(nanTime);
    CM_EXPECT(tr.sampleCount() == 2);
    const auto rep = whole(tr);
    for (const auto& s : rep.series) {
        CM_EXPECT(std::isfinite(s.crowdDb) && std::isfinite(s.masterDb) && std::isfinite(s.lufs) && std::isfinite(s.density));
        CM_EXPECT(s.crowdDb >= kFloorDb && s.lufs <= kCeilDb && s.density >= 0.0f && s.density <= 1.0f);
    }
    tr.reset();
    CM_EXPECT(tr.empty() && tr.sampleCount() == 0 && tr.peaks().empty());
    tr.push(inputAt(0, -40.0));
    CM_EXPECT(tr.sampleCount() == 1);
}

CM_TEST(report_recent_level_ring_returns_the_newest_values_last) {
    EnergyTracker tr;
    float out[200];
    tr.recentCrowdDb(out, 5);
    for (int i = 0; i < 5; ++i) CM_EXPECT(out[i] == kFloorDb);
    for (int t = 0; t < 130; ++t) tr.push(inputAt(t, -100.0 + t * 0.5));
    tr.recentCrowdDb(out, 5);
    for (int i = 0; i < 5; ++i) CM_EXPECT_NEAR(out[i], -100.0 + (125 + i) * 0.5, 1e-4);
    tr.recentCrowdDb(out, 200);
    CM_EXPECT(out[0] == kFloorDb && out[79] == kFloorDb);
    CM_EXPECT_NEAR(out[80], -100.0 + 10 * 0.5, 1e-4);   // oldest kept sample is t = 10
    CM_EXPECT_NEAR(out[199], -100.0 + 129 * 0.5, 1e-4);
    CM_EXPECT_NEAR(tr.currentCrowdDb(), -100.0 + 129 * 0.5, 1e-4);
}
