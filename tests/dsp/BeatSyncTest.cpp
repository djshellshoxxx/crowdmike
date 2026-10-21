// BeatSync DSP tests: BeatTracker, TapTempo, MidiClockTempo, TriggerQuantiser (docs/BEAT_SYNC_SPEC.md section 12).
#include "DSP/BeatTracker.h"
#include "DSP/MidiClockTempo.h"
#include "DSP/TapTempo.h"
#include "DSP/TriggerQuantiser.h"
#include "FxTestUtil.h"
#include "TestHarness.h"
#include <cmath>
#include <vector>

using namespace crowdmike::dsp;

namespace {
enum class Pattern { Click, KickSnareHat, FourOnFloor };

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f; }
};

void addKick(std::vector<float>& x, size_t at, double sr, float amp) {
    const size_t len = static_cast<size_t>(0.14 * sr);
    double phase = 0.0;
    for (size_t i = 0; i < len && at + i < x.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const double f = 50.0 + 110.0 * std::exp(-t * 35.0);
        phase += 6.283185307179586 * f / sr;
        x[at + i] += amp * static_cast<float>(std::sin(phase) * std::exp(-t * 22.0));
    }
}
void addSnare(std::vector<float>& x, size_t at, double sr, float amp, Rng& rng) {
    const size_t len = static_cast<size_t>(0.12 * sr);
    for (size_t i = 0; i < len && at + i < x.size(); ++i) {
        const double t = static_cast<double>(i) / sr;
        const float env = static_cast<float>(std::exp(-t * 30.0));
        x[at + i] += amp * env * (0.7f * rng.next() + 0.3f * static_cast<float>(std::sin(6.283185307179586 * 190.0 * t)));
    }
}
void addHat(std::vector<float>& x, size_t at, double sr, float amp, Rng& rng) {
    const size_t len = static_cast<size_t>(0.03 * sr);
    float prev = 0.0f;
    for (size_t i = 0; i < len && at + i < x.size(); ++i) {
        const float n = rng.next();
        const float hp = n - prev;   // crude high-pass
        prev = n;
        x[at + i] += amp * static_cast<float>(std::exp(-static_cast<double>(i) / sr * 150.0)) * hp;
    }
}
void addClick(std::vector<float>& x, size_t at, double sr, float amp) {
    const size_t len = static_cast<size_t>(0.004 * sr);
    for (size_t i = 0; i < len && at + i < x.size(); ++i)
        x[at + i] += amp * static_cast<float>(std::sin(6.283185307179586 * 1800.0 * static_cast<double>(i) / sr) * std::exp(-static_cast<double>(i) / sr * 900.0));
}

// Returns the audio; `firstBeatSec` is where beat 0 (a downbeat hit) sits.
std::vector<float> makePattern(Pattern pat, double bpm, double sr, double seconds, double firstBeatSec, float noiseAmp, uint32_t seed) {
    std::vector<float> x(static_cast<size_t>(seconds * sr), 0.0f);
    Rng rng(seed);
    const double beat = 60.0 / bpm;
    for (int b = 0; firstBeatSec + b * beat < seconds - 0.2; ++b) {
        const size_t at = static_cast<size_t>((firstBeatSec + b * beat) * sr);
        const size_t half = static_cast<size_t>((firstBeatSec + (b + 0.5) * beat) * sr);
        switch (pat) {
            case Pattern::Click: addClick(x, at, sr, 0.8f); break;
            case Pattern::KickSnareHat:
                if (b % 2 == 0) addKick(x, at, sr, 0.8f); else addSnare(x, at, sr, 0.7f, rng);
                addHat(x, half, sr, 0.25f, rng);
                break;
            case Pattern::FourOnFloor:
                addKick(x, at, sr, 0.8f);
                addHat(x, half, sr, 0.35f, rng);
                break;
        }
    }
    if (noiseAmp > 0.0f) { Rng nr(seed ^ 0x9e3779b9u); for (auto& v : x) v += noiseAmp * nr.next(); }
    return x;
}

BeatEstimate run(const std::vector<float>& x, double sr, int block, bool analyseEveryBlock) {
    BeatTracker bt;
    bt.prepare(sr);
    for (size_t pos = 0; pos < x.size(); pos += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), x.size() - pos));
        bt.pushSamples(x.data() + pos, n);
        if (analyseEveryBlock) bt.analyse();
    }
    bt.analyse();
    return bt.getEstimate();
}

// Distance (seconds) of the estimated beat position to the nearest true beat.
double phaseErrorSec(const BeatEstimate& e, double sr, double bpm, double firstBeatSec) {
    const double beat = 60.0 / bpm;
    const double t = static_cast<double>(e.lastBeatSample) / sr - firstBeatSec;
    double r = std::fmod(t, beat);
    if (r < 0.0) r += beat;
    return std::min(r, beat - r);
}
}

CM_TEST(beatTrackerTempoOnSyntheticPatterns) {
    const double tempos[] = { 70.0, 100.0, 128.0, 140.0, 174.0 };
    const Pattern pats[] = { Pattern::Click, Pattern::KickSnareHat, Pattern::FourOnFloor };
    const char* names[] = { "click", "kick-snare-hat", "four-on-floor" };
    for (double bpm : tempos) {
        for (int p = 0; p < 3; ++p) {
            const double sr = 48000.0;
            const auto x = makePattern(pats[p], bpm, sr, 18.0, 0.31, 0.0f, 7u + static_cast<uint32_t>(p));
            const auto e = run(x, sr, 512, true);
            if (!(e.valid && std::fabs(e.bpm - bpm) <= 1.5))
                std::cerr << "  tempo " << bpm << " pattern " << names[p] << " -> valid=" << e.valid << " bpm=" << e.bpm << " conf=" << e.confidence << '\n';
            CM_EXPECT(e.valid);
            CM_EXPECT_NEAR(e.bpm, bpm, 1.5);
            CM_EXPECT(e.confidence > 0.35f);
            const double pe = phaseErrorSec(e, sr, bpm, 0.31);
            if (pe > 0.035) std::cerr << "  phase error " << pe << " s at " << bpm << " " << names[p] << '\n';
            CM_EXPECT(pe <= 0.035);
        }
    }
}

CM_TEST(beatTrackerTempoWithNoise) {
    const double tempos[] = { 70.0, 100.0, 128.0, 140.0, 174.0 };
    for (double bpm : tempos) {
        const double sr = 44100.0;
        // noise floor about -28 dBFS RMS-ish against 0.8-peak hits
        const auto x = makePattern(Pattern::KickSnareHat, bpm, sr, 20.0, 0.12, 0.07f, 99u);
        const auto e = run(x, sr, 256, true);
        if (!(e.valid && std::fabs(e.bpm - bpm) <= 1.5)) std::cerr << "  noisy tempo " << bpm << " -> valid=" << e.valid << " bpm=" << e.bpm << " conf=" << e.confidence << '\n';
        CM_EXPECT(e.valid);
        CM_EXPECT_NEAR(e.bpm, bpm, 1.5);
        CM_EXPECT(phaseErrorSec(e, sr, bpm, 0.12) <= 0.04);
    }
}

CM_TEST(beatTrackerOctaveHandling) {
    // 70 BPM click track must not be reported as 140; 174 four-on-floor must not be reported as 87.
    const double sr = 48000.0;
    {
        const auto e = run(makePattern(Pattern::Click, 70.0, sr, 18.0, 0.2, 0.0f, 1u), sr, 512, true);
        CM_EXPECT(e.valid && e.bpm < 100.0);
    }
    {
        const auto e = run(makePattern(Pattern::FourOnFloor, 174.0, sr, 18.0, 0.2, 0.0f, 2u), sr, 512, true);
        CM_EXPECT(e.valid && e.bpm > 150.0);
    }
    // Result is always inside 60..200 even for out-of-range material (240 BPM clicks fold into range).
    const auto fast = run(makePattern(Pattern::Click, 240.0, sr, 18.0, 0.2, 0.0f, 3u), sr, 512, true);
    if (fast.valid) CM_EXPECT(fast.bpm >= BeatTracker::kMinBpm && fast.bpm <= BeatTracker::kMaxBpm);
    const auto slow = run(makePattern(Pattern::Click, 45.0, sr, 24.0, 0.2, 0.0f, 4u), sr, 512, true);
    if (slow.valid) CM_EXPECT(slow.bpm >= BeatTracker::kMinBpm && slow.bpm <= BeatTracker::kMaxBpm);
}

CM_TEST(beatTrackerDeterministicAndBlockSizeIndependent) {
    const double sr = 48000.0;
    const auto x = makePattern(Pattern::KickSnareHat, 128.0, sr, 14.0, 0.25, 0.02f, 5u);
    const auto a = run(x, sr, 64, true);
    const auto b = run(x, sr, 2048, false);   // analyse() only once at the end
    const auto c = run(x, sr, 64, true);
    CM_EXPECT(a.valid && b.valid);
    CM_EXPECT(a.bpm == c.bpm && a.lastBeatSample == c.lastBeatSample && a.confidence == c.confidence);
    CM_EXPECT_NEAR(a.bpm, b.bpm, 1.0e-9);
    CM_EXPECT(a.lastBeatSample == b.lastBeatSample);
}

CM_TEST(beatTrackerSilenceHoldsLockAndDropsConfidence) {
    const double sr = 48000.0;
    auto x = makePattern(Pattern::KickSnareHat, 120.0, sr, 12.0, 0.25, 0.0f, 6u);
    BeatTracker bt;
    bt.prepare(sr);
    bt.pushSamples(x.data(), static_cast<int>(x.size()));
    bt.analyse();
    const auto locked = bt.getEstimate();
    CM_EXPECT(locked.valid);
    std::vector<float> silence(static_cast<size_t>(8.0 * sr), 0.0f);
    bt.pushSamples(silence.data(), static_cast<int>(silence.size()));
    bt.analyse();
    const auto after = bt.getEstimate();
    CM_EXPECT(after.valid);
    CM_EXPECT_NEAR(after.bpm, locked.bpm, 0.5);   // held (a tail of the music may still nudge it slightly)
    CM_EXPECT(after.confidence < 0.2f);
    CM_EXPECT(std::isfinite(after.bpm) && std::isfinite(after.confidence));
}

CM_TEST(beatTrackerRobustToGarbageInput) {
    const double sr = 96000.0;
    BeatTracker bt;
    bt.prepare(sr);
    std::vector<float> junk(static_cast<size_t>(10.0 * sr));
    Rng r(3u);
    for (size_t i = 0; i < junk.size(); ++i) junk[i] = (i % 1000 == 0) ? std::nanf("") : (i % 777 == 0 ? INFINITY : r.next() * 4.0f);
    for (size_t pos = 0; pos < junk.size(); pos += 1000) bt.pushSamples(junk.data() + pos, 1000);
    bt.analyse();
    const auto e = bt.getEstimate();
    CM_EXPECT(std::isfinite(e.bpm) && std::isfinite(e.confidence) && std::isfinite(e.periodSamples));
    if (e.valid) CM_EXPECT(e.bpm >= 60.0 && e.bpm <= 200.0);
    CM_EXPECT(e.confidence >= 0.0f && e.confidence <= 1.0f);
    bt.reset();
    bt.analyse();
    CM_EXPECT(!bt.getEstimate().valid);
}

CM_TEST(beatTrackerSampleRates) {
    const double rates[] = { 44100.0, 88200.0, 96000.0, 192000.0 };
    for (double sr : rates) {
        const auto x = makePattern(Pattern::FourOnFloor, 124.0, sr, 14.0, 0.2, 0.0f, 11u);
        const auto e = run(x, sr, 512, true);
        CM_EXPECT(e.valid);
        CM_EXPECT_NEAR(e.bpm, 124.0, 1.5);
    }
}

// ------------------------------------------------------------------------------------------------ tap tempo
CM_TEST(tapTempoBasics) {
    TapTempo t;
    CM_EXPECT(!t.hasBpm());
    t.tap(10.0);
    CM_EXPECT(!t.hasBpm());
    t.tap(10.5);
    CM_EXPECT(t.hasBpm());
    CM_EXPECT_NEAR(t.bpm(), 120.0, 1.0e-9);
    for (int i = 2; i < 6; ++i) t.tap(10.0 + 0.5 * i);
    CM_EXPECT_NEAR(t.bpm(), 120.0, 1.0e-9);
    CM_EXPECT(t.tapCount() == 6);
}

CM_TEST(tapTempoKeepsOnlyLastEightTaps) {
    TapTempo t;
    double now = 0.0;
    for (int i = 0; i < 20; ++i) { t.tap(now); now += 0.6; }
    CM_EXPECT(t.tapCount() == TapTempo::kMaxTaps);
    CM_EXPECT_NEAR(t.bpm(), 100.0, 1.0e-9);
}

CM_TEST(tapTempoResetsAfterGap) {
    TapTempo t;
    t.tap(0.0); t.tap(0.5); t.tap(1.0);
    CM_EXPECT_NEAR(t.bpm(), 120.0, 1.0e-9);
    t.tap(1.0 + 2.6);          // gap > 2.5 s: new sequence, last tempo held
    CM_EXPECT(t.tapCount() == 1);
    CM_EXPECT_NEAR(t.bpm(), 120.0, 1.0e-9);
    t.tap(3.6 + 0.75);
    CM_EXPECT(t.tapCount() == 2);
    CM_EXPECT_NEAR(t.bpm(), 80.0, 1.0e-9);
    // 2.5 s exactly is still part of the sequence
    TapTempo u;
    u.tap(0.0); u.tap(2.5);
    CM_EXPECT(u.tapCount() == 2);
    CM_EXPECT_NEAR(u.bpm(), 24.0, 1.0e-9);
}

CM_TEST(tapTempoRejectsOutliersAndBounce) {
    TapTempo t;
    const double beat = 0.5;
    double now = 0.0;
    for (int i = 0; i < 6; ++i) { t.tap(now); now += beat; }
    CM_EXPECT_NEAR(t.bpm(), 120.0, 1.0e-9);
    // a late sloppy tap (+0.18 s) then back on grid: the bad interval pair is rejected as outliers
    const double sloppy = now + 0.18;
    t.tap(sloppy);
    t.tap(now + beat);
    CM_EXPECT(std::fabs(t.bpm() - 120.0) < 4.0);
    // contact bounce 50 ms after a tap is ignored
    const int before = t.tapCount();
    CM_EXPECT(!t.tap(now + beat + 0.05));
    CM_EXPECT(t.tapCount() == before);
    // non-finite and backwards times never corrupt the state
    CM_EXPECT(!t.tap(std::nan("")));
    CM_EXPECT(std::isfinite(t.bpm()));
}

CM_TEST(tapTempoFollowsTempoChange) {
    TapTempo t;
    double now = 0.0;
    for (int i = 0; i < 5; ++i) { t.tap(now); now += 0.5; }
    // switch to 90 BPM (0.667 s): after a few taps the estimate moves to the new tempo
    for (int i = 0; i < 6; ++i) { now += 0.1667; t.tap(now); now += 0.0; now += 0.5; }
    CM_EXPECT(t.bpm() < 100.0);
    CM_EXPECT(t.bpm() >= TapTempo::kMinBpm && t.bpm() <= TapTempo::kMaxBpm);
}

CM_TEST(tapTempoClampsToRange) {
    TapTempo t;
    t.tap(0.0); t.tap(0.21);   // ~285 BPM
    CM_EXPECT(t.bpm() <= TapTempo::kMaxBpm);
    TapTempo s;
    s.tap(0.0); s.tap(2.4);    // 25 BPM
    CM_EXPECT(s.bpm() >= TapTempo::kMinBpm);
}

// ------------------------------------------------------------------------------------------------ MIDI clock
CM_TEST(midiClockTempoMath) {
    const double sr = 48000.0;
    for (double bpm : { 40.0, 90.0, 120.0, 174.0, 250.0 }) {
        MidiClockTempo c;
        c.prepare(sr);
        c.onStart();
        const double tick = 60.0 * sr / (bpm * 24.0);
        for (int i = 0; i < 120; ++i) c.onTick(static_cast<std::int64_t>(std::llround(1000.0 + i * tick)));
        CM_EXPECT(c.hasTempo());
        CM_EXPECT_NEAR(c.bpm(), bpm, 0.05);
        CM_EXPECT(c.isRunning());
        CM_EXPECT(c.tickCount() == 119);
        // ppq: 119 ticks after start
        CM_EXPECT_NEAR(c.ppqAt(c.lastTickPos()), 119.0 / 24.0, 1.0e-9);
    }
}

CM_TEST(midiClockJitterAndTempoChange) {
    const double sr = 44100.0;
    MidiClockTempo c;
    c.prepare(sr);
    c.onStart();
    Rng rng(5u);
    double t = 500.0;
    const double tick120 = 60.0 * sr / (120.0 * 24.0);
    for (int i = 0; i < 200; ++i) {
        t += tick120;
        c.onTick(static_cast<std::int64_t>(t + rng.next() * 0.0008 * sr));   // +/- 0.8 ms jitter
    }
    CM_EXPECT_NEAR(c.bpm(), 120.0, 0.5);
    // tempo change to 150: smoothing follows within a beat or two, snaps for a big jump
    const double tick150 = 60.0 * sr / (150.0 * 24.0);
    for (int i = 0; i < 96; ++i) { t += tick150; c.onTick(static_cast<std::int64_t>(t)); }
    CM_EXPECT_NEAR(c.bpm(), 150.0, 0.5);
}

CM_TEST(midiClockTransport) {
    MidiClockTempo c;
    c.prepare(48000.0);
    c.onStart();
    for (int i = 0; i < 25; ++i) c.onTick(1000 + i * 1000);
    CM_EXPECT(c.isRunning());
    const auto ticksRun = c.tickCount();
    c.onStop();
    CM_EXPECT(!c.isRunning());
    c.onTick(1000 + 25 * 1000);
    CM_EXPECT(c.tickCount() == ticksRun);        // stopped: position holds
    c.onContinue();
    CM_EXPECT(c.isRunning());
    c.onTick(1000 + 26 * 1000);
    CM_EXPECT(c.tickCount() == ticksRun + 1);
    c.onStart();
    CM_EXPECT(c.tickCount() == 0);
    // a clock with no Start message at all is treated as running
    MidiClockTempo f;
    f.prepare(48000.0);
    for (int i = 0; i < 10; ++i) f.onTick(i * 1000);
    CM_EXPECT(f.isRunning());
    // long gap clears the history but keeps the tempo
    MidiClockTempo g;
    g.prepare(48000.0);
    g.onStart();
    for (int i = 0; i < 30; ++i) g.onTick(1000 + i * 1000);
    const double held = g.bpm();
    g.onTick(1000 + 29 * 1000 + 100000);   // > 0.5 s later
    CM_EXPECT_NEAR(g.bpm(), held, 1.0e-9);
    CM_EXPECT(g.hasTempo());
}

CM_TEST(midiClockPpqIsMonotonicAndBounded) {
    MidiClockTempo c;
    c.prepare(48000.0);
    c.onStart();
    for (int i = 0; i < 48; ++i) c.onTick(1000 + i * 1000);   // 125 BPM
    double prev = -1.0;
    for (std::int64_t p = 1000; p < 1000 + 60 * 1000; p += 137) {
        const double q = c.ppqAt(p);
        CM_EXPECT(q >= prev - 1.0e-12);
        CM_EXPECT(std::isfinite(q));
        prev = q;
    }
    // never extrapolates beyond one tick past the last one
    CM_EXPECT(c.ppqAt(c.lastTickPos() + 10000000) <= (c.tickCount() + 1.0) / 24.0 + 1.0e-12);
}

// ------------------------------------------------------------------------------------------------ quantiser
CM_TEST(quantiserGridOffsetIsSampleExact) {
    const double sr = 48000.0, bpm = 120.0;       // 24000 samples per beat
    const double bps = bpm / 60.0 / sr;
    // block starts 1000 samples before beat 4 (ppq 3.9583333)
    const double ppq0 = 4.0 - 1000.0 * bps;
    const int off = findGridOffset(ppq0, bpm, sr, 2048, 1.0);
    CM_EXPECT(off == 1000);
    CM_EXPECT(ppq0 + off * bps >= 4.0 - 1.0e-9);
    CM_EXPECT(ppq0 + (off - 1) * bps < 4.0);
    // exactly on a grid line: fires at offset 0
    CM_EXPECT(findGridOffset(8.0, bpm, sr, 64, 4.0) == 0);
    // grid not inside the block
    CM_EXPECT(findGridOffset(4.2, bpm, sr, 512, 1.0) == -1);
    // sweep every offset for several tempos / sample rates
    for (double r : { 44100.0, 48000.0, 96000.0 }) {
        for (double b : { 70.0, 100.0, 128.0, 174.0 }) {
            const double bs = b / 60.0 / r;
            for (int want = 0; want < 700; want += 37) {
                const double p0 = 12.0 - want * bs + 0.3 * bs;      // grid line falls between sample (want-1) and want
                const int o = findGridOffset(p0, b, r, 1024, 4.0);
                CM_EXPECT(o == want);
            }
        }
    }
}

CM_TEST(quantiserStateMachine) {
    TriggerQuantiser q;
    QuantiseTiming t;
    t.mode = static_cast<int>(QuantiseMode::Quarter);
    t.bpm = 120.0; t.hasTempo = true; t.playing = true; t.sampleRate = 48000.0;
    const double bps = 120.0 / 60.0 / 48000.0;
    const int block = 512;
    // idle: nothing pending
    t.ppq = 0.3;
    CM_EXPECT(q.process(false, t, block).kind == QuantiseResult::Kind::None);
    // request at ppq 0.3: waits for beat 1 at sample (0.7/bps)
    CM_EXPECT(q.process(true, t, block).kind == QuantiseResult::Kind::None);
    CM_EXPECT(q.isPending());
    int fired = -1;
    long absoluteFire = -1;
    for (int b = 1; b < 100 && fired < 0; ++b) {
        t.ppq = 0.3 + b * block * bps;
        const auto r = q.process(false, t, block);
        if (r.kind == QuantiseResult::Kind::Fire) { fired = r.offset; absoluteFire = static_cast<long>(b) * block + r.offset; }
    }
    CM_EXPECT(fired >= 0 && fired < block);
    CM_EXPECT(!q.isPending());
    const double expected = std::ceil((1.0 - 0.3) / bps - 1.0e-4);
    CM_EXPECT_NEAR(static_cast<double>(absoluteFire), expected, 1.0);

    // Off => passthrough; unusable tempo => passthrough; pending released when quantising turns off
    t.mode = 0;
    CM_EXPECT(q.process(true, t, block).kind == QuantiseResult::Kind::Passthrough);
    t.mode = 3;
    q.process(true, t, block);
    CM_EXPECT(q.isPending());
    t.hasTempo = false;
    const auto rel = q.process(false, t, block);
    CM_EXPECT(rel.kind == QuantiseResult::Kind::Fire && rel.offset == 0);
    t.hasTempo = true; t.playing = false;
    CM_EXPECT(q.process(true, t, block).kind == QuantiseResult::Kind::Passthrough);
    t.playing = true; t.bpm = std::nan("");
    CM_EXPECT(q.process(true, t, block).kind == QuantiseResult::Kind::Passthrough);
}

CM_TEST(quantiserGridSizesAndTimeout) {
    CM_EXPECT_NEAR(gridBeatsFor(1, 4), 1.0, 0.0);
    CM_EXPECT_NEAR(gridBeatsFor(2, 4), 2.0, 0.0);
    CM_EXPECT_NEAR(gridBeatsFor(3, 4), 4.0, 0.0);
    CM_EXPECT_NEAR(gridBeatsFor(3, 3), 3.0, 0.0);
    CM_EXPECT_NEAR(gridBeatsFor(0, 4), 0.0, 0.0);
    // a stalled transport (ppq never advances) cannot hold a trigger forever
    TriggerQuantiser q;
    QuantiseTiming t;
    t.mode = 3; t.bpm = 120.0; t.hasTempo = true; t.playing = true; t.ppq = 0.5; t.sampleRate = 48000.0;
    q.process(true, t, 512);
    bool fired = false;
    for (int i = 0; i < 2000 && !fired; ++i) fired = q.process(false, t, 512).kind == QuantiseResult::Kind::Fire;
    CM_EXPECT(fired);
    // coalescing: several requests while pending fire once
    TriggerQuantiser c;
    t.ppq = 0.5;
    c.process(true, t, 512);
    c.process(true, t, 512);
    int fires = 0;
    for (int i = 0; i < 400; ++i) { t.ppq = 0.5 + (i + 1) * 512 * (2.0 / 48000.0); if (c.process(false, t, 512).kind == QuantiseResult::Kind::Fire) ++fires; }
    CM_EXPECT(fires == 1);
}
