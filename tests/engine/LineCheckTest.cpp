// Line Check probe + analyser on synthetic signals, apply/undo, engine wiring (docs/LINE_CHECK_SPEC.md section 8).
#include "TestHarness.h"
#include "Engine/LineCheck.h"
#include "Engine/LineCheckApply.h"
#include "PluginProcessor.h"
#include <cmath>
#include <functional>
#include <random>

using namespace crowdmike;
using namespace crowdmike::linecheck;

namespace {
using Gen = std::function<float(long)>;

// Deterministic gaussian noise generator with a given RMS in dBFS.
struct Noise {
    std::mt19937 rng;
    std::normal_distribution<float> dist { 0.0f, 1.0f };
    float sigma;
    Noise(unsigned seed, double rmsDb) : rng(seed), sigma(static_cast<float>(std::pow(10.0, rmsDb / 20.0))) {}
    float next() { return dist(rng) * sigma; }
};
double sineAt(double hz, double amp, long n, double sr) { return amp * std::sin(6.283185307179586 * hz * static_cast<double>(n) / sr); }

struct Run {
    std::vector<Result> results;
    std::vector<std::vector<Frame>> frames;
};
// Feeds `seconds` of audio generated per input into a probe bank in blocks of `block`, then analyses.
Run probe(double sr, int block, double seconds, const std::vector<Gen>& gens, const std::vector<InputContext>& ctx = {}, const Settings& st = {}) {
    const int ins = static_cast<int>(gens.size());
    ProbeBank bank;
    bank.prepare(sr, ins);
    Collector col;
    col.begin(bank, ins);
    std::vector<std::vector<float>> buf(static_cast<size_t>(ins), std::vector<float>(static_cast<size_t>(block)));
    std::vector<const float*> ptr(static_cast<size_t>(ins));
    for (int i = 0; i < ins; ++i) ptr[static_cast<size_t>(i)] = buf[static_cast<size_t>(i)].data();
    const long total = static_cast<long>(seconds * sr);
    for (long pos = 0; pos < total; pos += block) {
        const int n = static_cast<int>(std::min<long>(block, total - pos));
        for (int i = 0; i < ins; ++i) for (int s = 0; s < n; ++s) buf[static_cast<size_t>(i)][static_cast<size_t>(s)] = gens[static_cast<size_t>(i)](pos + s);
        bank.process(ptr.data(), ins, n);
        col.pull(bank);
    }
    Run r;
    r.frames = col.frames();
    std::vector<InputContext> c = ctx;
    c.resize(static_cast<size_t>(ins));
    r.results = Analyser::analyse(r.frames, c, st);
    return r;
}
Gen noiseGen(unsigned seed, double rmsDb, double dc = 0.0) {
    auto n = std::make_shared<Noise>(seed, rmsDb);
    auto last = std::make_shared<long>(-1);
    auto cur = std::make_shared<float>(0.0f);
    return [=](long i) { if (i != *last) { *cur = n->next(); *last = i; } return static_cast<float>(*cur + dc); };
}
Gen zero() { return [](long) { return 0.0f; }; }
constexpr double kSr = 48000.0;
}

CM_TEST(linecheck_dead_input) {
    const auto r = probe(kSr, 256, 10.0, { zero(), noiseGen(1, -26.0) });
    CM_EXPECT(r.results[0].verdict == Verdict::Dead);
    CM_EXPECT(!r.results[0].hasTrim && !r.results[0].suggestHpOn);
    CM_EXPECT(r.results[1].verdict == Verdict::Ok);
    CM_EXPECT(r.frames[0].size() == 100);
}

CM_TEST(linecheck_ok_and_low_inputs_get_trim_to_target) {
    const auto r = probe(kSr, 256, 10.0, { noiseGen(2, -26.0), noiseGen(3, -45.0), noiseGen(4, -20.0) });
    CM_EXPECT(r.results[0].verdict == Verdict::Ok);
    CM_EXPECT(r.results[0].hasTrim);
    CM_EXPECT_NEAR(r.results[0].programRmsDb, -26.0, 0.4);
    CM_EXPECT_NEAR(r.results[0].proposedTrimDb, 6.0, 0.4);
    CM_EXPECT(!r.results[0].trimNoChange);
    CM_EXPECT(r.results[1].verdict == Verdict::Low);
    CM_EXPECT_NEAR(r.results[1].proposedTrimDb, 24.0, 1e-4);          // 25 dB wanted, clamped to +24
    CM_EXPECT(r.results[2].trimNoChange);                              // already at the target within 0.5 dB
    CM_EXPECT(r.results[0].noiseFloorDb <= r.results[0].programRmsDb + 1e-3f);
    Settings st;
    st.targetRmsDb = -18.0f;
    const auto r2 = probe(kSr, 256, 3.0, { noiseGen(2, -26.0) }, {}, st);
    CM_EXPECT_NEAR(r2.results[0].proposedTrimDb, 8.0, 0.5);
}

CM_TEST(linecheck_hot_and_clipping_never_raised) {
    const Gen sine08 = [](long n) { return static_cast<float>(sineAt(997.0, 0.8, n, kSr)); };
    const Gen clipped = [](long n) { return static_cast<float>(std::clamp(sineAt(997.0, 1.6, n, kSr), -1.0, 1.0)); };
    const Gen clicks = [](long n) { return (n % 1000 == 0) ? 1.0f : 0.0f; };
    std::vector<InputContext> ctx(3);
    ctx[2].trimDb = -10.0f;
    const auto r = probe(kSr, 128, 4.0, { sine08, clipped, clicks }, ctx);
    CM_EXPECT(r.results[0].verdict == Verdict::Hot);
    CM_EXPECT(r.results[0].proposedTrimDb < -10.0f);
    CM_EXPECT(r.results[1].verdict == Verdict::Clipping && r.results[1].clips > 0);
    CM_EXPECT(r.results[1].proposedTrimDb <= 0.0f);
    CM_EXPECT(r.results[2].verdict == Verdict::Clipping);
    CM_EXPECT(r.results[2].proposedTrimDb <= ctx[2].trimDb + 1e-4f);   // would be +10 by level alone: never raised
    for (const auto& x : r.results) CM_EXPECT(x.proposedTrimDb <= 24.0f && x.proposedTrimDb >= -24.0f);
}

CM_TEST(linecheck_headroom_limits_the_trim) {
    const Gen pulses = [](long n) { return (n % 100 == 0) ? 0.5f : 0.0f; };   // rms -26 dBFS, peak -6 dBFS
    const auto r = probe(kSr, 256, 3.0, { pulses });
    CM_EXPECT(r.results[0].verdict == Verdict::Ok);
    CM_EXPECT(r.results[0].limitedByHeadroom);
    CM_EXPECT_NEAR(r.results[0].proposedTrimDb, 5.0, 0.2);                // peak lands at -1 dBFS
    CM_EXPECT(r.results[0].peakDb + r.results[0].proposedTrimDb <= -0.99f);
}

CM_TEST(linecheck_dc_offset_flag) {
    const auto r = probe(kSr, 256, 4.0, { noiseGen(5, -26.0, 0.05), noiseGen(6, -26.0, 0.002), [](long) { return 0.05f; } });
    CM_EXPECT(r.results[0].dc);
    CM_EXPECT_NEAR(r.results[0].dcDb, -26.0, 0.3);
    CM_EXPECT(r.results[0].verdict == Verdict::Ok);
    CM_EXPECT(!r.results[1].dc);
    CM_EXPECT(r.results[2].dc && r.results[2].verdict == Verdict::Dead);   // pure DC is dead (no signal) but flagged
}

CM_TEST(linecheck_hum_50_and_60_hz) {
    const auto hum = [](double hz, double amp, long n) { return static_cast<float>(sineAt(hz, amp, n, kSr) + 0.3 * sineAt(2.0 * hz, amp, n, kSr)); };
    Noise bg(9, -62.0);
    auto bgNoise = [bg](long) mutable { return bg.next(); };
    // speech-like: 1 s loud noise, 1 s quiet, with 60 Hz hum present all the time
    auto loudNoise = std::make_shared<Noise>(11, -22.0);
    auto last = std::make_shared<long>(-1);
    auto cur = std::make_shared<float>(0.0f);
    const Gen bursty = [=](long n) {
        if (n != *last) { *last = n; *cur = loudNoise->next(); }
        return static_cast<float>(((n / 48000) % 2 == 0 ? *cur : 0.0f) + sineAt(60.0, 0.01, n, kSr));
    };
    const auto r = probe(kSr, 512, 10.0, {
        [&](long n) { return hum(50.0, 0.01, n) + bgNoise(n); },
        [&](long n) { return hum(60.0, 0.01, n) + bgNoise(n); },
        noiseGen(12, -24.0),
        bursty });
    CM_EXPECT(r.results[0].hum && r.results[0].humHz == 50);
    CM_EXPECT(r.results[1].hum && r.results[1].humHz == 60);
    CM_EXPECT(!r.results[2].hum);
    CM_EXPECT(r.results[3].hum && r.results[3].humHz == 60);
    CM_EXPECT(r.results[0].suggestHpHz == kHpfHumHz);
    CM_EXPECT(r.results[2].suggestHpHz == kHpfDefaultHz);
    CM_EXPECT(r.results[0].humRatio > 0.9f);
}

CM_TEST(linecheck_polarity_pair) {
    auto shared = std::make_shared<Noise>(21, -24.0);
    auto last = std::make_shared<long>(-1);
    auto cur = std::make_shared<float>(0.0f);
    const auto src = [=](long n) { if (n != *last) { *last = n; *cur = shared->next(); } return *cur; };
    auto x = std::make_shared<std::vector<float>>();
    {   Noise nz(21, -24.0); for (long i = 0; i < static_cast<long>(4 * kSr); ++i) x->push_back(nz.next()); }
    (void) src;
    const Gen a = [x](long n) { return (*x)[static_cast<size_t>(n)]; };
    const Gen inverted = [x](long n) { return -(*x)[static_cast<size_t>(n)]; };
    const Gen mostly = [x](long n) { return -0.9f * (*x)[static_cast<size_t>(n)] + 0.1f * (*x)[static_cast<size_t>((n * 7) % 190000)]; };
    {
        const auto r = probe(kSr, 256, 4.0, { a, inverted });
        CM_EXPECT(!r.results[0].polarityMismatch);
        CM_EXPECT(r.results[1].polarityMismatch && r.results[1].polarityPartner == 0);
        CM_EXPECT(r.results[0].correlationNext < -0.95f);
    }
    {   // polarity switch of input 2 already set: pair is effectively in phase
        std::vector<InputContext> ctx(2);
        ctx[1].polarityInverted = true;
        const auto r = probe(kSr, 256, 4.0, { a, inverted }, ctx);
        CM_EXPECT(!r.results[1].polarityMismatch);
        CM_EXPECT(r.results[0].correlationNext > 0.95f);
    }
    {   const auto r = probe(kSr, 256, 4.0, { a, mostly });
        CM_EXPECT(r.results[1].polarityMismatch); }
    {   const auto r = probe(kSr, 256, 4.0, { a, noiseGen(99, -24.0) });                // independent sources
        CM_EXPECT(!r.results[1].polarityMismatch);
        CM_EXPECT(std::abs(r.results[0].correlationNext) < 0.2f); }
    {   const auto r = probe(kSr, 256, 4.0, { a, zero() });                              // dead partner: never flagged
        CM_EXPECT(!r.results[1].polarityMismatch); }
    {   const auto r = probe(kSr, 256, 4.0, { a, [x](long n) { return -0.2f * (*x)[static_cast<size_t>(n)]; } });   // 14 dB apart
        CM_EXPECT(!r.results[1].polarityMismatch); }
}

CM_TEST(linecheck_probe_is_block_size_and_rate_independent) {
    const auto gens = [] { return std::vector<Gen> { noiseGen(31, -27.0, 0.003), noiseGen(32, -30.0) }; };
    const auto ref = probe(kSr, 1, 1.5, gens());
    for (int block : { 7, 64, 2048 }) {
        const auto r = probe(kSr, block, 1.5, gens());
        for (size_t i = 0; i < 2; ++i) {
            CM_EXPECT(r.results[i].frames == ref.results[i].frames);
            CM_EXPECT(r.results[i].programRmsDb == ref.results[i].programRmsDb);
            CM_EXPECT(r.results[i].peakDb == ref.results[i].peakDb);
            CM_EXPECT(r.results[i].dcDb == ref.results[i].dcDb);
            CM_EXPECT(r.results[i].noiseFloorDb == ref.results[i].noiseFloorDb);
        }
        CM_EXPECT(r.results[0].correlationNext == ref.results[0].correlationNext);
    }
    for (double sr : { 44100.0, 96000.0, 192000.0 }) {
        const Gen h = [sr](long n) { return static_cast<float>(sineAt(50.0, 0.01, n, sr)); };
        const auto r = probe(sr, 512, 1.0, { h });
        CM_EXPECT(r.results[0].hum && r.results[0].humHz == 50);
        CM_EXPECT_NEAR(r.results[0].programRmsDb, -43.0, 0.2);
        CM_EXPECT(r.frames[0].size() == 10);
    }
    ProbeBank off;
    off.prepare(kSr, 2);
    std::vector<float> a(256, 0.5f);
    const float* p[2] = { a.data(), a.data() };
    off.process(p, 2, 256);
    Frame f;
    CM_EXPECT(!off.pop(0, f));                                           // disarmed: nothing recorded
    off.start();
    for (int i = 0; i < 200; ++i) off.process(p, 2, 256);
    CM_EXPECT(off.pop(0, f) && f.n == 4800);
    off.stop();
    const auto gen = off.generation();
    off.start();
    CM_EXPECT(off.generation() != gen);
}

CM_TEST(linecheck_probe_ring_overflow_counts_drops_and_stays_valid) {
    ProbeBank bank;
    bank.prepare(kSr, 1);
    bank.start();
    std::vector<float> a(4800, 0.1f);
    const float* p[1] = { a.data() };
    for (int i = 0; i < 300; ++i) bank.process(p, 1, 4800);               // 300 frames, never consumed
    CM_EXPECT(bank.dropped() == static_cast<std::uint64_t>(300 - kRingFrames));
    int got = 0;
    Frame f;
    while (bank.pop(0, f)) ++got;
    CM_EXPECT(got == kRingFrames);
}

CM_TEST(linecheck_analyser_handles_no_frames_and_nan) {
    const auto none = Analyser::analyse({ {}, {} }, {});
    CM_EXPECT(none.size() == 2 && !none[0].valid && none[0].verdict == Verdict::Dead && !none[0].hasTrim);
    const Gen bad = [](long n) { return n % 5 == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.05f * static_cast<float>(std::sin(0.1 * static_cast<double>(n))); };
    const auto r = probe(kSr, 256, 2.0, { bad });
    CM_EXPECT(std::isfinite(r.results[0].programRmsDb) && std::isfinite(r.results[0].proposedTrimDb));
}

CM_TEST(linecheck_choose_template_for_channel_counts) {
    CM_EXPECT(chooseTemplate(1, 2) == 1);
    CM_EXPECT(chooseTemplate(2, 2) == 2);
    CM_EXPECT(chooseTemplate(3, 2) == 3);
    CM_EXPECT(chooseTemplate(4, 8) == 3);
    CM_EXPECT(chooseTemplate(6, 4) == 4);
    CM_EXPECT(chooseTemplate(6, 2) == 3);
    CM_EXPECT(chooseTemplate(8, 8) == 8);
    CM_EXPECT(chooseTemplate(16, 16) == 8);
    CM_EXPECT(chooseTemplate(8, 4) == 4);
}

namespace {
float getParam(CrowdMikeAudioProcessor& p, const juce::String& id) {
    auto* q = p.getParameters().getParameter(id);
    return q != nullptr ? q->convertFrom0to1(q->getValue()) : -999.0f;
}
void setParam(CrowdMikeAudioProcessor& p, const juce::String& id, float actual) {
    auto* q = p.getParameters().getParameter(id);
    q->setValueNotifyingHost(q->convertTo0to1(actual));
}
bool layout(CrowdMikeAudioProcessor& p, int in, int out) {
    auto l = p.getBusesLayout();
    l.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(in);
    l.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(out);
    return p.setBusesLayout(l);
}
void flush(CrowdMikeAudioProcessor& p) { (void) p.getParameters().copyState(); }   // APVTS mirrors params into its ValueTree (and the undo history) on flush
ApplyTarget targetFor(CrowdMikeAudioProcessor& p) {
    return { p.getUndoManager(),
             [&p](const juce::String& id, float v) { p.getHub().setParamActual(ParamBank::indexForId(id), v); },
             [&p](const juce::String& id) { return getParam(p, id); } };
}
}

CM_TEST(linecheck_apply_all_per_input_and_undo) {
    CrowdMikeAudioProcessor p;
    CM_EXPECT(layout(p, 4, 4));
    p.prepareToPlay(kSr, 256);
    const Gen clicks = [](long n) { return (n % 1000 == 0) ? 1.0f : 0.0f; };
    std::vector<InputContext> ctx(4);
    ctx[3].trimDb = -10.0f;
    setParam(p, "input4.trimDb", -10.0f);
    const auto run = probe(kSr, 256, 3.0, { noiseGen(41, -26.0), noiseGen(42, -14.0), zero(), clicks }, ctx);
    auto t = targetFor(p);
    flush(p);
    p.getUndoManager().clearUndoHistory();
    const int writes = applyProposals(t, run.results, { 0, 1, 2, 3 });
    CM_EXPECT(writes >= 6);
    CM_EXPECT_NEAR(getParam(p, "input1.trimDb"), 6.0, 0.5);
    CM_EXPECT_NEAR(getParam(p, "input2.trimDb"), -6.0, 0.5);
    CM_EXPECT_NEAR(getParam(p, "input3.trimDb"), 0.0, 1e-4);              // dead input untouched
    CM_EXPECT(getParam(p, "input3.highPassEnabled") < 0.5f);
    CM_EXPECT_NEAR(getParam(p, "input4.trimDb"), -10.0, 0.11);            // clipping input never raised
    CM_EXPECT(getParam(p, "input1.highPassEnabled") > 0.5f);
    CM_EXPECT_NEAR(getParam(p, "input1.highPassHz"), 100.0, 0.6);
    flush(p);
    CM_EXPECT(p.getUndoManager().undo());                                // ONE undo restores every input
    CM_EXPECT_NEAR(getParam(p, "input1.trimDb"), 0.0, 1e-3);
    CM_EXPECT_NEAR(getParam(p, "input2.trimDb"), 0.0, 1e-3);
    CM_EXPECT(getParam(p, "input1.highPassEnabled") < 0.5f);
    CM_EXPECT(getParam(p, "input2.highPassEnabled") < 0.5f);
    CM_EXPECT_NEAR(getParam(p, "input4.trimDb"), -10.0, 0.11);
    // per-input apply touches only that input and is its own undo step
    const int one = applyProposals(t, run.results, { 1 });
    CM_EXPECT(one >= 3);
    CM_EXPECT_NEAR(getParam(p, "input2.trimDb"), -6.0, 0.5);
    CM_EXPECT_NEAR(getParam(p, "input1.trimDb"), 0.0, 1e-3);
    flush(p);
    CM_EXPECT(p.getUndoManager().undo());
    CM_EXPECT_NEAR(getParam(p, "input2.trimDb"), 0.0, 1e-3);
    // the live trim is re-checked at apply time for clipping inputs: lowering the live trim first is respected
    setParam(p, "input4.trimDb", -14.0f);
    applyProposals(t, run.results, { 3 });
    CM_EXPECT(getParam(p, "input4.trimDb") <= -13.9f);
    // polarity is applied only when explicitly requested
    auto withPol = run.results;
    withPol[1].polarityMismatch = true;
    ApplyOptions onlyPol; onlyPol.trim = false; onlyPol.hpf = false; onlyPol.polarity = true;
    applyProposals(t, withPol, { 1 }, onlyPol);
    CM_EXPECT(getParam(p, "input2.polarity") > 0.5f);
    applyProposals(t, withPol, { 0, 1 }, ApplyOptions {});
    CM_EXPECT(getParam(p, "input2.polarity") > 0.5f);                    // default options leave polarity alone
}

CM_TEST(linecheck_engine_probe_reads_raw_input_before_trim) {
    const auto measure = [](float trimDb) {
        CrowdMikeAudioProcessor p;
        CM_EXPECT(layout(p, 2, 2));
        p.prepareToPlay(kSr, 256);
        setParam(p, "input1.trimDb", trimDb);
        auto& probeBank = p.getEngine().lineProbe();
        Collector col;
        col.begin(probeBank, 2);
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer midi;
        Noise nz(77, -24.0);
        for (int blk = 0; blk < 10 * 188; ++blk) {
            for (int s = 0; s < 256; ++s) { b.setSample(0, s, nz.next()); b.setSample(1, s, 0.0f); }
            p.processBlock(b, midi);
            col.pull(probeBank);
        }
        probeBank.stop();
        return Analyser::analyse(col.frames(), std::vector<InputContext>(2));
    };
    const auto a = measure(0.0f), b = measure(-12.0f);
    CM_EXPECT(a[0].frames >= 90);
    CM_EXPECT_NEAR(a[0].programRmsDb, -24.0, 0.4);
    CM_EXPECT_NEAR(b[0].programRmsDb, a[0].programRmsDb, 1e-4);          // trim changes nothing: the probe is pre-trim
    CM_EXPECT(a[1].verdict == Verdict::Dead);
}

CM_TEST(linecheck_quick_start_applies_template_and_defaults_in_one_undo) {
    CrowdMikeAudioProcessor p;
    CM_EXPECT(layout(p, 6, 4));
    p.prepareToPlay(kSr, 256);
    const int id = chooseTemplate(6, 4);
    CM_EXPECT(id == 4);
    p.getUndoManager().clearUndoHistory();
    p.applyWiringTemplate(id);
    auto t = targetFor(p);
    flush(p);
    CM_EXPECT(applyQuickStartDefaults(t, 6) > 0);
    for (int i = 1; i <= 6; ++i) {
        CM_EXPECT(getParam(p, "input" + juce::String(i) + ".highPassEnabled") > 0.5f);
        CM_EXPECT(getParam(p, "input" + juce::String(i) + ".highPassHz") >= 99.0f);
    }
    CM_EXPECT(p.getRouting().getAssign(0, 0) > 0.0f);                   // template wiring is in place
    flush(p);
    CM_EXPECT(p.getUndoManager().undo());
    CM_EXPECT(getParam(p, "input6.highPassEnabled") < 0.5f);
    CM_EXPECT(getParam(p, "input1.highPassEnabled") < 0.5f);
}
