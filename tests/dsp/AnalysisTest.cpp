#include "TestHarness.h"
#include "DSP/CorrelationMeter.h"
#include "DSP/LevelHistory.h"
#include "DSP/MeterBank.h"
#include "DSP/RouteRisk.h"
#include "DSP/SpectrumAnalyzer.h"
#include <cmath>
#include <cstdint>
#include <vector>

using namespace crowdmike::dsp;

namespace {
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float>(s) * (2.0f / 4294967296.0f) - 1.0f; }
};
constexpr double kTwoPiD = 6.283185307179586;
}

// ---------------------------------------------------------------- LevelHistory
CM_TEST(level_history_min_max_rms_and_wraparound) {
    LevelHistory h;
    h.prepare(1000.0, 1.0f, 10); // 100 samples per bucket
    CM_EXPECT(h.getSamplesPerBucket() == 100);
    float mn[10], mx[10], rm[10];
    h.snapshot(mn, mx, rm, 10);
    for (int i = 0; i < 10; ++i) CM_EXPECT(mn[i] == 0.0f && mx[i] == 0.0f && rm[i] == 0.0f);

    // Bucket b holds a constant-magnitude square wave of amplitude (b+1)/100 with an offset b/1000:
    // min = off - a, max = off + a, rms = sqrt(off^2 + a^2).
    auto pushBucket = [&](int b, int blockSize) {
        std::vector<float> x(100);
        const float a = static_cast<float>(b + 1) / 100.0f, off = static_cast<float>(b) / 1000.0f;
        for (int i = 0; i < 100; ++i) x[static_cast<size_t>(i)] = off + ((i & 1) ? a : -a);
        for (int i = 0; i < 100; i += blockSize) h.push(x.data() + i, std::min(blockSize, 100 - i));
    };
    for (int b = 0; b < 3; ++b) pushBucket(b, 7);
    h.snapshot(mn, mx, rm, 10);
    for (int i = 0; i < 7; ++i) CM_EXPECT(mx[i] == 0.0f);   // not yet written => zeros, oldest first
    CM_EXPECT_NEAR(mx[9], 0.002 + 0.03, 1e-6);              // newest bucket last
    CM_EXPECT_NEAR(mn[7], 0.0 - 0.01, 1e-6);

    for (int b = 3; b < 37; ++b) pushBucket(b, b % 2 ? 100 : 13); // wraps the ring several times
    CM_EXPECT(h.getNumCompletedBuckets() == 37);
    h.snapshot(mn, mx, rm, 10);
    for (int i = 0; i < 10; ++i) {
        const int b = 27 + i;
        const double a = (b + 1) / 100.0, off = b / 1000.0;
        CM_EXPECT_NEAR(mn[i], off - a, 1e-5);
        CM_EXPECT_NEAR(mx[i], off + a, 1e-5);
        CM_EXPECT_NEAR(rm[i], std::sqrt(off * off + a * a), 1e-5);
    }
    // Resampled to 5 buckets: min of mins / max of maxes.
    float mn5[5], mx5[5];
    h.snapshot(mn5, mx5, nullptr, 5);
    CM_EXPECT_NEAR(mn5[0], std::min(0.027 - 0.28, 0.028 - 0.29), 1e-5);
    CM_EXPECT_NEAR(mx5[4], 0.036 + 0.37, 1e-5);
    // Upsampled to 20 buckets: each source repeated.
    float mx20[20];
    h.snapshot(nullptr, mx20, nullptr, 20);
    CM_EXPECT_NEAR(mx20[19], mx[9], 1e-7);
    CM_EXPECT_NEAR(mx20[0], mx[0], 1e-7);
}

CM_TEST(level_history_memory_is_bounded_and_non_finite_is_safe) {
    LevelHistory h;
    h.prepare(48000.0, 10.0f, 512);
    const auto bytes = h.getMemoryBytes();
    CM_EXPECT(bytes > 0 && bytes < 64 * 1024);
    std::vector<float> x(4096);
    Rng r(9);
    for (int b = 0; b < 3000; ++b) { // > 4 minutes of audio
        for (auto& v : x) v = r.next();
        h.push(x.data(), static_cast<int>(x.size()));
    }
    CM_EXPECT(h.getMemoryBytes() == bytes);
    float bad[3] = { NAN, INFINITY, 0.5f };
    for (int i = 0; i < 2000; ++i) h.push(bad, 3);
    std::vector<float> mn(512), mx(512), rm(512);
    h.snapshot(mn.data(), mx.data(), rm.data(), 512);
    for (int i = 0; i < 512; ++i) CM_EXPECT(std::isfinite(mn[static_cast<size_t>(i)]) && std::isfinite(mx[static_cast<size_t>(i)]) && std::isfinite(rm[static_cast<size_t>(i)]));
    CM_EXPECT_NEAR(mx[511], 0.5, 1e-6);
    CM_EXPECT_NEAR(mn[511], 0.0, 1e-6);
    h.reset();
    h.snapshot(mn.data(), mx.data(), rm.data(), 512);
    CM_EXPECT(mx[511] == 0.0f);
}

// ---------------------------------------------------------------- CorrelationMeter
CM_TEST(correlation_identical_inverted_uncorrelated) {
    const double sr = 48000.0;
    const int n = 48000;
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n)), inv(static_cast<size_t>(n)), other(static_cast<size_t>(n)), zero(static_cast<size_t>(n), 0.0f);
    Rng a(1), b(2);
    for (int i = 0; i < n; ++i) {
        l[static_cast<size_t>(i)] = 0.5f * a.next();
        inv[static_cast<size_t>(i)] = -l[static_cast<size_t>(i)];
        other[static_cast<size_t>(i)] = 0.5f * b.next();
    }
    (void) r;
    CorrelationMeter m;
    m.prepare(sr, 100.0f);
    for (int i = 0; i < n; i += 128) m.process(l.data() + i, l.data() + i, std::min(128, n - i));
    CM_EXPECT_NEAR(m.getCorrelation(), 1.0, 1e-4);
    CM_EXPECT_NEAR(m.getWidth(), 0.0, 1e-4);
    CM_EXPECT(m.getSideToMidDb() <= -59.0f);

    m.reset();
    for (int i = 0; i < n; i += 512) m.process(l.data() + i, inv.data() + i, std::min(512, n - i));
    CM_EXPECT_NEAR(m.getCorrelation(), -1.0, 1e-4);
    CM_EXPECT_NEAR(m.getWidth(), 1.0, 1e-4);

    m.reset();
    for (int i = 0; i < n; i += 64) m.process(l.data() + i, other.data() + i, std::min(64, n - i));
    CM_EXPECT_NEAR(m.getCorrelation(), 0.0, 0.1);
    CM_EXPECT_NEAR(m.getWidth(), 0.5, 0.05);
    CM_EXPECT_NEAR(m.getSideToMidDb(), 0.0, 1.0);

    // Silence reads 0 correlation; non-finite input is ignored.
    m.reset();
    m.process(zero.data(), zero.data(), n);
    CM_EXPECT(m.getCorrelation() == 0.0f);
    float bad[2] = { NAN, INFINITY };
    m.process(bad, bad, 2);
    CM_EXPECT(std::isfinite(m.getCorrelation()) && std::isfinite(m.getWidth()));
}

// ---------------------------------------------------------------- SpectrumAnalyzer
CM_TEST(spectrum_analyzer_peak_at_known_tone) {
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
        SpectrumAnalyzer s;
        s.prepare(sr);
        float spec[256];
        s.getSpectrumDb(spec, 256);
        for (float v : spec) CM_EXPECT(v == SpectrumAnalyzer::kFloorDb);
        const double hz = 2500.0;
        std::vector<float> blk(256);
        long long pos = 0;
        for (int b = 0; b < static_cast<int>(sr * 0.5) / 256; ++b) {
            for (auto& v : blk) { v = 0.25f * static_cast<float>(std::sin(kTwoPiD * hz * static_cast<double>(pos) / sr)); ++pos; }
            s.pushSamples(blk.data(), 256);
            if (b % 8 == 0) s.analyse();
        }
        s.analyse();
        CM_EXPECT_NEAR(s.getPeakFrequencyHz(), hz, 0.25 * sr / SpectrumAnalyzer::kFftSize);
        s.getSpectrumDb(spec, 256);
        int best = 0;
        for (int i = 1; i < 256; ++i) if (spec[i] > spec[best]) best = i;
        const double lo = 20.0 * std::pow(1000.0, best / 256.0), hi = 20.0 * std::pow(1000.0, (best + 1) / 256.0);
        CM_EXPECT(lo <= hz * 1.02 && hi >= hz * 0.98);
        CM_EXPECT_NEAR(spec[best], 20.0 * std::log10(0.25), 1.5);
        // Far away from the tone the spectrum is much lower.
        CM_EXPECT(spec[10] < spec[best] - 60.0f);
        s.reset();
        s.getSpectrumDb(spec, 256);
        CM_EXPECT(spec[best] == SpectrumAnalyzer::kFloorDb);
    }
}

CM_TEST(spectrum_analyzer_backlog_is_skipped) {
    SpectrumAnalyzer s;
    s.prepare(48000.0);
    std::vector<float> x(48000 * 5);
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * static_cast<float>(std::sin(kTwoPiD * 440.0 * static_cast<double>(i) / 48000.0));
    s.pushSamples(x.data(), static_cast<int>(x.size())); // much more than the FIFO holds
    s.analyse();
    CM_EXPECT_NEAR(s.getPeakFrequencyHz(), 440.0, 6.0);
}

// ---------------------------------------------------------------- MeterBank
CM_TEST(peak_rms_meter_known_sines) {
    const double sr = 48000.0;
    for (float amp : { 1.0f, 0.5f, 0.1f }) {
        PeakRmsMeter m;
        m.prepare(sr, 100.0f);
        std::vector<float> x(4800);
        long long pos = 0;
        for (int b = 0; b < 20; ++b) { // 2 s
            for (auto& v : x) { v = amp * static_cast<float>(std::sin(kTwoPiD * 997.0 * static_cast<double>(pos++) / sr)); }
            for (int i = 0; i < 4800; i += 480) m.process(x.data() + i, 480);
        }
        CM_EXPECT_NEAR(m.getPeak(), amp, amp * 1e-3);
        CM_EXPECT_NEAR(m.getRms(), amp / std::sqrt(2.0), amp * 0.01);
        CM_EXPECT_NEAR(m.getRmsDb(), 20.0 * std::log10(amp / std::sqrt(2.0)), 0.1);
        CM_EXPECT_NEAR(m.getAndResetPeak(), amp, amp * 1e-3);
        CM_EXPECT(m.getPeak() == 0.0f);
    }
    PeakRmsMeter m;
    m.prepare(sr);
    float bad[3] = { NAN, -0.25f, INFINITY };
    m.process(bad, 3);
    CM_EXPECT_NEAR(m.getPeak(), 0.25, 1e-7);
    CM_EXPECT(std::isfinite(m.getRms()));
}

CM_TEST(clip_latch_sets_and_holds_until_reset) {
    ClipLatch c;
    float ok[4] = { 0.5f, -0.99f, 0.999f, 0.0f };
    c.process(ok, 4);
    CM_EXPECT(!c.isClipped());
    float hot[3] = { 0.1f, -1.0f, 0.2f };
    c.process(hot, 3);
    CM_EXPECT(c.isClipped());
    c.process(ok, 4);
    CM_EXPECT(c.isClipped()); // latched
    c.reset();
    CM_EXPECT(!c.isClipped());
    float over[1] = { 1.5f };
    c.process(over, 1);
    CM_EXPECT(c.isClipped());
    c.reset();
    float bad[1] = { NAN };
    c.process(bad, 1);
    CM_EXPECT(c.isClipped());
}

// ---------------------------------------------------------------- RouteRisk
CM_TEST(route_risk_monotonic_in_every_input) {
    RiskInputs base;
    base.routeExists = true;
    base.inputTrimDb = -6.0f;
    base.groupGainDb = -3.0f;
    base.busGainDb = -2.0f;
    base.outputGainDb = 0.0f;
    base.compressorMakeupDb = 2.0f;
    base.delayFeedback = 0.2f;
    base.persistentPeakSeverity = 0.1f;
    base.sameZone = false;
    float RiskInputs::*gains[] = { &RiskInputs::inputTrimDb, &RiskInputs::groupGainDb, &RiskInputs::busGainDb,
                                   &RiskInputs::outputGainDb, &RiskInputs::compressorMakeupDb };
    for (auto g : gains) {
        for (int zone = 0; zone < 2; ++zone) {
            RiskInputs in = base;
            in.sameZone = zone == 1;
            float prev = -1.0f;
            for (float db = -80.0f; db <= 24.0f; db += 0.5f) {
                in.*g = db;
                const float s = scoreRisk(in).score;
                CM_EXPECT(s >= prev);
                CM_EXPECT(s >= 0.0f && s <= 1.0f);
                prev = s;
            }
        }
    }
    {
        RiskInputs in = base;
        float prev = -1.0f;
        for (float fb = 0.0f; fb <= 1.0f; fb += 0.01f) {
            in.delayFeedback = fb;
            const float s = scoreRisk(in).score;
            CM_EXPECT(s >= prev);
            prev = s;
        }
        in = base;
        prev = -1.0f;
        for (float sev = 0.0f; sev <= 1.0f; sev += 0.02f) {
            in.persistentPeakSeverity = sev;
            const float s = scoreRisk(in).score;
            CM_EXPECT(s > prev);
            prev = s;
        }
        in = base;
        in.sameZone = false;
        const float other = scoreRisk(in).score;
        in.sameZone = true;
        CM_EXPECT(scoreRisk(in).score > other);
    }
}

CM_TEST(route_risk_level_thresholds) {
    RiskInputs in;
    in.routeExists = false;
    in.outputGainDb = 24.0f;
    in.sameZone = true;
    RiskResult r = scoreRisk(in);
    CM_EXPECT(r.score == 0.0f && r.level == 0 && r.reason != nullptr);

    in = RiskInputs {};
    in.routeExists = true;
    in.sameZone = true; // unity loop gain, same zone => exactly 0.5 => caution
    r = scoreRisk(in);
    CM_EXPECT_NEAR(r.score, 0.5, 1e-5);
    CM_EXPECT(r.level == 1);
    in.outputGainDb = 2.0f;
    r = scoreRisk(in);
    CM_EXPECT(r.level == 2 && r.score >= risk::kHighThreshold);
    in.outputGainDb = -12.0f;
    in.sameZone = false;
    r = scoreRisk(in);
    CM_EXPECT(r.level == 0 && r.score < risk::kCautionThreshold);
    CM_EXPECT(r.reason != nullptr);
    // A strong persistent peak alone escalates even a low-gain route.
    in.persistentPeakSeverity = 1.0f;
    r = scoreRisk(in);
    CM_EXPECT(r.level >= 1);
    // Levels follow the documented thresholds for every score.
    for (float db = -40.0f; db <= 20.0f; db += 0.25f) {
        in.outputGainDb = db;
        r = scoreRisk(in);
        const int expect = r.score >= risk::kHighThreshold ? 2 : (r.score >= risk::kCautionThreshold ? 1 : 0);
        CM_EXPECT(r.level == expect);
    }
    // Delay feedback adds recirculation gain: 0.5 feedback = +6 dB.
    RiskInputs d;
    d.routeExists = true;
    d.delayFeedback = 0.5f;
    CM_EXPECT_NEAR(routeLoopGainDb(d), 6.02, 0.01);
    d.compressorMakeupDb = -5.0f; // negative makeup never lowers the risk estimate
    CM_EXPECT_NEAR(routeLoopGainDb(d), 6.02, 0.01);
    d.inputTrimDb = NAN;
    CM_EXPECT(std::isfinite(scoreRisk(d).score));
}
