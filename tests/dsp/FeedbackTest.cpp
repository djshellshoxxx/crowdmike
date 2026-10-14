#include "TestHarness.h"
#include "DSP/FeedbackDetector.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

using crowdmike::dsp::FeedbackCandidate;
using crowdmike::dsp::FeedbackDetector;

namespace {
// Deterministic white noise (xorshift32), uniform in [-1, 1).
struct Noise {
    std::uint32_t s;
    explicit Noise(std::uint32_t seed) : s(seed) {}
    float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s) * (2.0f / 4294967296.0f) - 1.0f;
    }
};

constexpr double kTwoPiD = 6.283185307179586;

// Feeds gen(sampleIndex) through the detector in varied block sizes, calling analyse() roughly every 20 ms
// (like a GUI timer). onAnalyse is invoked after each analyse().
struct Sim {
    FeedbackDetector& det;
    double sr;
    long long pos = 0;
    std::vector<float> block = std::vector<float>(2048);
    void run(double seconds, const std::function<float(long long)>& gen,
             const std::function<void()>& onAnalyse = nullptr) {
        static const int sizes[] = { 1, 16, 64, 128, 512, 2048, 480, 333 };
        const long long end = pos + static_cast<long long>(seconds * sr);
        const long long analyseEvery = static_cast<long long>(0.02 * sr);
        long long sinceAnalyse = 0;
        int si = 0;
        while (pos < end) {
            const int n = static_cast<int>(std::min<long long>(sizes[si++ % 8], end - pos));
            for (int i = 0; i < n; ++i) block[static_cast<size_t>(i)] = gen(pos + i);
            det.pushSamples(block.data(), n);
            pos += n;
            sinceAnalyse += n;
            if (sinceAnalyse >= analyseEvery) {
                det.analyse();
                sinceAnalyse = 0;
                if (onAnalyse) onAnalyse();
            }
        }
        det.analyse();
        if (onAnalyse) onAnalyse();
    }
};

int candidates(const FeedbackDetector& d, FeedbackCandidate* out) { return d.getCandidates(out, FeedbackDetector::kMaxCandidates); }

std::function<float(long long)> toneInNoise(double sr, double hz, float amp, float noiseAmp, std::uint32_t seed) {
    auto noise = std::make_shared<Noise>(seed);
    return [=](long long i) { return amp * static_cast<float>(std::sin(kTwoPiD * hz * static_cast<double>(i) / sr)) + noiseAmp * noise->next(); };
}
}

CM_TEST(feedback_finds_stable_tone_in_noise_after_persistence) {
    const double sr = 48000.0;
    FeedbackDetector d;
    d.prepare(sr);
    Sim sim { d, sr };
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    // -26 dBFS tone under ~-15 dBFS-rms white noise.
    auto gen = toneInNoise(sr, 1000.0, 0.05f, 0.3f, 1234u);
    sim.run(0.3, gen);
    CM_EXPECT(candidates(d, c) == 0); // not yet persistent
    sim.run(1.7, gen);
    const int n = candidates(d, c);
    CM_EXPECT(n == 1);
    if (n >= 1) {
        CM_EXPECT_NEAR(c[0].frequencyHz, 1000.0, d.getBinHz());
        CM_EXPECT(c[0].prominenceDb >= 12.0f);
        CM_EXPECT(c[0].persistenceSec >= 0.5f && c[0].persistenceSec <= 2.1f);
        CM_EXPECT(c[0].severity > 0.0f && c[0].severity <= 1.0f);
        CM_EXPECT(c[0].suggestedQ >= 10.0f && c[0].suggestedQ <= 30.0f);
        CM_EXPECT(c[0].suggestedDepthDb <= -3.0f && c[0].suggestedDepthDb >= -18.0f);
        // A clean (window-limited) peak deserves a narrow notch.
        CM_EXPECT(c[0].suggestedQ >= 20.0f);
    }
}

CM_TEST(feedback_severity_grows_with_persistence_and_level) {
    const double sr = 48000.0;
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    float sevShort = 0, sevLong = 0, persShort = 0, persLong = 0;
    {
        FeedbackDetector d;
        d.prepare(sr);
        Sim sim { d, sr };
        auto gen = toneInNoise(sr, 2000.0, 0.05f, 0.1f, 99u);
        sim.run(1.0, gen);
        if (candidates(d, c) >= 1) { sevShort = c[0].severity; persShort = c[0].persistenceSec; }
        sim.run(3.0, gen);
        if (candidates(d, c) >= 1) { sevLong = c[0].severity; persLong = c[0].persistenceSec; }
    }
    CM_EXPECT(sevShort > 0.0f);
    CM_EXPECT(persLong > persShort + 2.5f);
    CM_EXPECT(sevLong > sevShort + 0.1f);
    CM_EXPECT_NEAR(persLong, 4.0, 0.3);

    float sevQuiet = 0, sevLoud = 0, depthQuiet = 0, depthLoud = 0;
    for (int pass = 0; pass < 2; ++pass) {
        FeedbackDetector d;
        d.prepare(sr);
        Sim sim { d, sr };
        sim.run(2.0, toneInNoise(sr, 2000.0, pass == 0 ? 0.02f : 0.3f, 0.1f, 7u));
        CM_EXPECT(candidates(d, c) == 1);
        (pass == 0 ? sevQuiet : sevLoud) = c[0].severity;
        (pass == 0 ? depthQuiet : depthLoud) = c[0].suggestedDepthDb;
    }
    CM_EXPECT(sevLoud > sevQuiet + 0.1f);
    CM_EXPECT(depthLoud < depthQuiet); // deeper notch for the more severe peak
}

CM_TEST(feedback_rising_tone_scores_higher_than_steady_tone) {
    const double sr = 48000.0;
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    float sev[2] = { 0, 0 };
    for (int pass = 0; pass < 2; ++pass) {
        FeedbackDetector d;
        d.prepare(sr);
        Sim sim { d, sr };
        auto noise = std::make_shared<Noise>(5u);
        const bool rising = pass == 1;
        sim.run(2.0, [&](long long i) {
            const double t = static_cast<double>(i) / sr;
            const float amp = rising ? crowdmike::dsp::dbToGain(static_cast<float>(-38.0 + 12.0 * t)) : 0.1f; // ends at -14 dBFS
            return amp * static_cast<float>(std::sin(kTwoPiD * 1500.0 * t)) + 0.05f * noise->next();
        });
        CM_EXPECT(candidates(d, c) == 1);
        sev[pass] = c[0].severity;
    }
    // The rising tone is quieter on average but its growth trend raises severity close to / above the steady one.
    CM_EXPECT(sev[1] > 0.0f);
    CM_EXPECT(sev[1] > sev[0] - 0.05f);
}

CM_TEST(feedback_ignores_broadband_noise) {
    for (double sr : { 44100.0, 96000.0 }) {
        FeedbackDetector d;
        d.prepare(sr);
        Sim sim { d, sr };
        FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
        int maxSeen = 0;
        auto noise = std::make_shared<Noise>(4242u);
        // White noise plus a gently coloured (one-pole lowpassed) layer.
        float lp = 0.0f;
        sim.run(6.0, [&](long long) { const float w = noise->next(); lp = 0.95f * lp + 0.05f * w; return 0.3f * w + 2.0f * lp; },
                [&] { maxSeen = std::max(maxSeen, candidates(d, c)); });
        CM_EXPECT(maxSeen == 0);
    }
}

CM_TEST(feedback_rejects_short_bursts) {
  for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
    FeedbackDetector d;
    d.prepare(sr);
    Sim sim { d, sr };
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    int maxSeen = 0;
    auto noise = std::make_shared<Noise>(77u);
    // 200 ms tone bursts every 800 ms, plus 120 ms clicks/bursts at varying frequencies in between.
    sim.run(5.0, [&](long long i) {
        const double t = static_cast<double>(i) / sr;
        const double ph = std::fmod(t, 0.8);
        float x = 0.02f * noise->next();
        if (ph < 0.2) x += 0.2f * static_cast<float>(std::sin(kTwoPiD * 1000.0 * t));
        if (ph >= 0.4 && ph < 0.52) {
            const double hz = 500.0 + 700.0 * std::floor(t / 0.8);
            x += 0.2f * static_cast<float>(std::sin(kTwoPiD * hz * t));
        }
        return x;
    }, [&] { maxSeen = std::max(maxSeen, candidates(d, c)); });
    CM_EXPECT(maxSeen == 0);
  }
}

CM_TEST(feedback_ignores_music_like_partials_and_sweeps) {
  for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 }) {
    FeedbackDetector d;
    d.prepare(sr);
    Sim sim { d, sr };
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    int maxSeen = 0;
    auto noise = std::make_shared<Noise>(31337u);
    // Stepwise melody (semitone / whole-tone steps, never repeating, no wrap-around leaps), 250 ms notes, 6 harmonics with 1/h amplitudes,
    // 5.5 Hz vibrato; every 4th second a gliding harmonic sweep. Phases are accumulated so glides are continuous.
    static const int steps[] = { 2, 1, 2, -1, -2, 2, 2, 1, -2, -1, 2, -2, 1, 2, -1, 2, -2, -1, -2, 2 };
    double phase[6] = { 0, 0, 0, 0, 0, 0 };
    sim.run(8.0, [&](long long i) {
        const double t = static_cast<double>(i) / sr;
        double f0;
        const double seg = std::fmod(t, 4.0);
        if (seg >= 3.0) f0 = 180.0 * std::pow(4.0, (seg - 3.0)); // 180 -> 720 Hz glide in 1 s
        else {
            const int note = static_cast<int>(t / 0.25);
            int semis = 0;
            for (int k = 0; k <= note; ++k) semis += steps[k % 20];
            f0 = 220.0 * std::pow(2.0, semis / 12.0) * (1.0 + 0.012 * std::sin(kTwoPiD * 5.5 * t));
        }
        float x = 0.01f * noise->next();
        for (int h = 0; h < 6; ++h) {
            phase[h] += kTwoPiD * f0 * (h + 1) / sr;
            if (phase[h] > kTwoPiD) phase[h] -= kTwoPiD;
            x += (0.2f / static_cast<float>(h + 1)) * static_cast<float>(std::sin(phase[h]));
        }
        return x;
    }, [&] { maxSeen = std::max(maxSeen, candidates(d, c)); });
    CM_EXPECT(maxSeen == 0);
  }
}

CM_TEST(feedback_tracks_and_ranks_two_tones) {
    const double sr = 48000.0;
    FeedbackDetector d;
    d.prepare(sr);
    Sim sim { d, sr };
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    auto noise = std::make_shared<Noise>(2024u);
    sim.run(3.0, [&](long long i) {
        const double t = static_cast<double>(i) / sr;
        return 0.15f * static_cast<float>(std::sin(kTwoPiD * 3150.0 * t)) + 0.01f * static_cast<float>(std::sin(kTwoPiD * 800.0 * t))
             + 0.05f * noise->next();
    });
    const int n = candidates(d, c);
    CM_EXPECT(n == 2);
    if (n >= 2) {
        CM_EXPECT_NEAR(c[0].frequencyHz, 3150.0, d.getBinHz());
        CM_EXPECT_NEAR(c[1].frequencyHz, 800.0, d.getBinHz());
        CM_EXPECT(c[0].severity > c[1].severity);
        CM_EXPECT(c[0].suggestedDepthDb <= c[1].suggestedDepthDb);
    }
    // maxCount is honoured
    CM_EXPECT(d.getCandidates(c, 1) == 1);
    CM_EXPECT(d.getCandidates(c, 0) == 0);
}

CM_TEST(feedback_frequency_accuracy_all_sample_rates) {
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 }) {
        for (double hz : { 157.0, 1234.5, 6789.0 }) {
            FeedbackDetector d;
            d.prepare(sr);
            Sim sim { d, sr };
            FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
            sim.run(1.5, toneInNoise(sr, hz, 0.1f, 0.05f, 11u));
            const int n = candidates(d, c);
            CM_EXPECT(n == 1);
            if (n >= 1) {
                CM_EXPECT_NEAR(c[0].frequencyHz, hz, 0.25 * d.getBinHz());
                CM_EXPECT_NEAR(c[0].persistenceSec, 1.45, 0.15); // persistence is in seconds at every rate
            }
        }
    }
}

CM_TEST(feedback_high_rates_decimate_without_aliasing) {
    for (double sr : { 88200.0, 96000.0, 192000.0 }) {
        FeedbackDetector d;
        d.prepare(sr);
        CM_EXPECT(d.getAnalysisSampleRate() <= 48000.0 && d.getAnalysisSampleRate() >= 44100.0);
        FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
        Sim sim { d, sr };
        sim.run(1.5, toneInNoise(sr, 33000.0, 0.3f, 0.003f, 8u)); // would alias to ~11..15 kHz without the filter
        CM_EXPECT(candidates(d, c) == 0);
        sim.run(1.5, toneInNoise(sr, 17000.0, 0.3f, 0.003f, 8u)); // top of the analysed band still works
        CM_EXPECT(candidates(d, c) == 1);
        CM_EXPECT_NEAR(c[0].frequencyHz, 17000.0, 0.25 * d.getBinHz());
    }
}

CM_TEST(feedback_fifo_overflow_never_blocks_and_recovers) {
    const double sr = 48000.0;
    FeedbackDetector d;
    d.prepare(sr);
    CM_EXPECT(d.getFifoCapacitySamples() >= static_cast<int>(sr));
    Noise noise(5u);
    std::vector<float> buf(4096);
    const auto t0 = std::chrono::steady_clock::now();
    // 30 s of audio, never analysed; includes one huge push.
    for (int b = 0; b < 350; ++b) {
        for (auto& x : buf) x = 0.3f * noise.next();
        d.pushSamples(buf.data(), static_cast<int>(buf.size()));
    }
    std::vector<float> huge(static_cast<size_t>(sr * 3), 0.1f);
    d.pushSamples(huge.data(), static_cast<int>(huge.size()));
    const double pushMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CM_EXPECT(pushMs < 2000.0);
    d.analyse(); // drains at most the FIFO capacity
    CM_EXPECT(d.getDroppedSamples() > 0); // counted when the consumer notices the overrun
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    CM_EXPECT(candidates(d, c) == 0);
    Sim sim { d, sr };
    sim.run(2.0, toneInNoise(sr, 2500.0, 0.1f, 0.05f, 3u));
    CM_EXPECT(candidates(d, c) == 1);
    CM_EXPECT_NEAR(c[0].frequencyHz, 2500.0, d.getBinHz());
    // Non-finite input is harmless.
    float bad[4] = { NAN, INFINITY, -INFINITY, 0.0f };
    d.pushSamples(bad, 4);
    d.pushSamples(nullptr, 10);
    d.analyse();
    float spec[64];
    d.getSpectrumDb(spec, 64);
    for (float v : spec) CM_EXPECT(std::isfinite(v));
}

CM_TEST(feedback_reset_clears_everything) {
    const double sr = 48000.0;
    FeedbackDetector d;
    d.prepare(sr);
    Sim sim { d, sr };
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    sim.run(1.5, toneInNoise(sr, 1000.0, 0.1f, 0.05f, 1u));
    CM_EXPECT(candidates(d, c) == 1);
    d.reset();
    CM_EXPECT(candidates(d, c) == 0);
    float spec[100];
    d.getSpectrumDb(spec, 100);
    for (float v : spec) CM_EXPECT(v == FeedbackDetector::kFloorDb);
    d.analyse();
    CM_EXPECT(candidates(d, c) == 0);
    // Deterministic after reset: same input => same result.
    Sim sim2 { d, sr };
    sim2.run(1.5, toneInNoise(sr, 1000.0, 0.1f, 0.05f, 1u));
    FeedbackDetector fresh;
    fresh.prepare(sr);
    Sim sim3 { fresh, sr };
    sim3.run(1.5, toneInNoise(sr, 1000.0, 0.1f, 0.05f, 1u));
    FeedbackCandidate c2[FeedbackDetector::kMaxCandidates];
    CM_EXPECT(candidates(d, c) == 1 && candidates(fresh, c2) == 1);
    CM_EXPECT(c[0].frequencyHz == c2[0].frequencyHz && c[0].severity == c2[0].severity);
}

CM_TEST(feedback_display_spectrum_shows_tone) {
    const double sr = 48000.0;
    FeedbackDetector d;
    d.prepare(sr);
    float spec[200];
    d.getSpectrumDb(spec, 200);
    for (float v : spec) CM_EXPECT(v == FeedbackDetector::kFloorDb);
    Sim sim { d, sr };
    sim.run(0.5, toneInNoise(sr, 1000.0, 0.1f, 0.0f, 1u));
    d.getSpectrumDb(spec, 200);
    int best = 0;
    for (int i = 1; i < 200; ++i) if (spec[i] > spec[best]) best = i;
    const double lo = 20.0 * std::pow(1000.0, best / 200.0), hi = 20.0 * std::pow(1000.0, (best + 1) / 200.0);
    CM_EXPECT(lo <= 1010.0 && hi >= 990.0);
    CM_EXPECT_NEAR(spec[best], -20.0, 1.5); // 0.1 amplitude sine == -20 dBFS
}

CM_TEST(feedback_concurrent_push_analyse_and_gui_reads) {
    const double sr = 48000.0;
    FeedbackDetector d;
    d.prepare(sr);
    std::atomic<bool> done { false };
    // "Audio" thread: pushes 4 s of a 1 kHz tone in noise as fast as it can (bursty, may overflow the FIFO).
    std::thread audio([&] {
        Noise n(17u);
        std::vector<float> blk(128);
        long long pos = 0;
        for (int b = 0; b < static_cast<int>(4.0 * sr / 128); ++b) {
            for (auto& x : blk) { x = 0.1f * static_cast<float>(std::sin(kTwoPiD * 1000.0 * static_cast<double>(pos++) / sr)) + 0.05f * n.next(); }
            d.pushSamples(blk.data(), 128);
            if (b % 64 == 0) std::this_thread::yield();
        }
        done.store(true);
    });
    // Worker thread: analyses continuously.
    std::thread worker([&] { while (!done.load()) { d.analyse(); std::this_thread::yield(); } d.analyse(); });
    // GUI thread: reads snapshots.
    FeedbackCandidate c[FeedbackDetector::kMaxCandidates];
    float spec[128];
    int reads = 0;
    while (!done.load()) {
        const int n = d.getCandidates(c, FeedbackDetector::kMaxCandidates);
        CM_EXPECT(n >= 0 && n <= FeedbackDetector::kMaxCandidates);
        for (int i = 0; i < n; ++i) CM_EXPECT(std::isfinite(c[i].frequencyHz) && c[i].severity >= 0.0f && c[i].severity <= 1.0f);
        d.getSpectrumDb(spec, 128);
        ++reads;
        std::this_thread::yield();
    }
    audio.join();
    worker.join();
    CM_EXPECT(reads > 0);
    for (float v : spec) CM_EXPECT(std::isfinite(v));
}
