#include "TestHarness.h"
#include "FxTestUtil.h"
#include "DSP/ReverseChunk.h"
#include <functional>
#include <memory>

using namespace crowdmike;
using namespace crowdmike::dsp;
namespace RP = params::ReverseP;

namespace {
std::vector<float> revParams(int mode = 0, float chunkSec = 0.5f, float xfMs = 5.0f) {
    auto p = fxtest::defaults(RP::defs);
    p[RP::Mode] = static_cast<float>(mode); p[RP::ChunkSec] = chunkSec; p[RP::Crossfade] = xfMs;
    p[RP::TrigMode] = 0.0f; p[RP::Retrigger] = 0.0f; p[RP::Trigger] = 0.0f; p[RP::Mix] = 1.0f;
    return p;
}

// Streams whole input buffers through a ReverseChunk in successive calls.
struct Rig {
    std::unique_ptr<ReverseChunk> rc = std::make_unique<ReverseChunk>();
    std::vector<float> inL, inR, preL, preR, outL, outR;
    int pos = 0;
    double sr = 48000.0;
    Rig(double rate, int maxBlock, std::vector<float> l, std::vector<float> r) : inL(std::move(l)), inR(std::move(r)), sr(rate) {
        rc->prepare(rate, maxBlock);
        outL.assign(inL.size(), 0.0f); outR.assign(inR.size(), 0.0f);
    }
    // ctxAt(blockStart) lets tests provide a moving playhead.
    void run(int count, int block, const float* p, std::function<FxContext(int)> ctxAt = nullptr) {
        const int end = std::min(pos + count, static_cast<int>(inL.size()));
        while (pos < end) {
            const int n = std::min(block, end - pos);
            const FxContext ctx = ctxAt ? ctxAt(pos) : FxContext {};
            const float* pl = preL.empty() ? nullptr : preL.data() + pos;
            const float* pr = preR.empty() ? nullptr : preR.data() + pos;
            rc->process(inL.data() + pos, inR.data() + pos, pl, pr, outL.data() + pos, outR.data() + pos, n, p, ctx);
            pos += n;
        }
    }
};

std::vector<float> ramp(int n, float scale = 1.0f) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = static_cast<float>(i) * scale;
    return v;
}
}

CM_TEST(reverse_triggered_outputs_exact_reverse_order) {
    for (double sr : { 44100.0, 48000.0, 96000.0 }) {
        for (int linked = 1; linked >= 0; --linked) {
            const int T = static_cast<int>(sr);
            const int len = static_cast<int>(std::lround(0.5 * sr));
            const int xf = static_cast<int>(std::lround(0.005 * sr));
            const int rOff = linked ? 0 : len / 2;
            const int total = T + len + 2000;
            Rig rig(sr, 512, ramp(total), ramp(total));
            auto p = revParams(0, 0.5f, 5.0f);
            p[RP::StereoLink] = static_cast<float>(linked);
            rig.run(T, 100, p.data());
            CM_EXPECT(fxtest::maxAbs(rig.outL, 0, static_cast<size_t>(T)) == 0.0f);
            p[RP::Trigger] = 1.0f;
            rig.run(total - T, 128, p.data());
            int bad = 0;
            for (int k = xf; k < len - xf; ++k) {
                bad += rig.outL[static_cast<size_t>(T + k)] != static_cast<float>(T - 1 - k);
                bad += rig.outR[static_cast<size_t>(T + k)] != static_cast<float>(T - 1 - k - rOff);
            }
            CM_EXPECT(bad == 0);
            for (int k = 0; k < xf; ++k) {   // fades only attenuate
                CM_EXPECT(std::fabs(rig.outL[static_cast<size_t>(T + k)]) <= static_cast<float>(T - 1 - k));
                CM_EXPECT(std::fabs(rig.outL[static_cast<size_t>(T + len - 1 - k)]) <= static_cast<float>(T - len + k) + 0.5f);
            }
            CM_EXPECT(fxtest::maxAbs(rig.outL, static_cast<size_t>(T + len)) == 0.0f);   // wet only during playback
            CM_EXPECT(rig.rc->getTriggerCount() == 1u);
            CM_EXPECT(rig.rc->getLastTriggerSample() == T);
        }
    }
}

CM_TEST(reverse_pre_source_selection) {
    const int T = 48000, len = 24000, total = T + len + 100;
    Rig rig(48000.0, 512, ramp(total), ramp(total));
    rig.preL = ramp(total, 2.0f); rig.preR = ramp(total, 3.0f);
    auto p = revParams(0, 0.5f, 5.0f);
    p[RP::Source] = 1.0f;
    rig.run(T, 64, p.data());
    p[RP::Trigger] = 1.0f;
    rig.run(total - T, 64, p.data());
    CM_EXPECT(rig.outL[static_cast<size_t>(T + 1000)] == 2.0f * static_cast<float>(T - 1 - 1000));
    CM_EXPECT(rig.outR[static_cast<size_t>(T + 1000)] == 3.0f * static_cast<float>(T - 1 - 1000));
}

CM_TEST(reverse_chunk_length_change_mid_play_keeps_indices) {
    const int T = 48000 * 2, len = 48000, xf = 240, total = T + len + 500;
    Rig rig(48000.0, 256, ramp(total), ramp(total));
    auto p = revParams(0, 1.0f, 5.0f);
    rig.run(T, 256, p.data());
    p[RP::Trigger] = 1.0f;
    rig.run(10000, 256, p.data());
    p[RP::ChunkSec] = 0.25f;       // mid-play change
    p[RP::Crossfade] = 300.0f;
    rig.run(total - rig.pos, 256, p.data());
    int bad = 0;
    for (int k = xf; k < len - xf; ++k) bad += rig.outL[static_cast<size_t>(T + k)] != static_cast<float>(T - 1 - k);
    CM_EXPECT(bad == 0);
}

CM_TEST(reverse_repeat_mode_decays_and_ends) {
    const int T = 48000, len = 24000, xf = 240, total = T + 16 * len;
    Rig rig(48000.0, 512, ramp(total), ramp(total));
    auto p = revParams(2, 0.5f, 5.0f);
    p[RP::Decay] = 0.5f;
    rig.run(T, 64, p.data());
    p[RP::Trigger] = 1.0f;
    rig.run(total - T, 64, p.data());
    // pass m starts at T + m*(len - xf) and plays the same reversed chunk at gain 0.5^m (from the hold buffer m >= 1).
    for (int m = 0; m < 4; ++m) {
        int bad = 0;
        const float g = std::pow(0.5f, static_cast<float>(m));
        for (int k = xf; k < len - xf; ++k)
            bad += rig.outL[static_cast<size_t>(T + m * (len - xf) + k)] != g * static_cast<float>(T - 1 - k);
        CM_EXPECT(bad == 0);
    }
    // 0.5^13 >= 1e-4 > 0.5^14: 14 passes, then digital silence.
    const size_t lastPassMid = static_cast<size_t>(T + 13 * (len - xf) + len / 2);
    CM_EXPECT(rig.outL[lastPassMid] != 0.0f);
    CM_EXPECT(fxtest::maxAbs(rig.outL, static_cast<size_t>(T + 13 * (len - xf) + len)) == 0.0f);
    CM_EXPECT(rig.rc->getTriggerCount() == 1u);
}

CM_TEST(reverse_retrigger_behaviours) {
    const int T = 48000, len = 24000, xf = 240, total = T + 3 * len;
    auto runCase = [&](int mode, int retrig, Rig& rig) {
        auto p = revParams(mode, 0.5f, 5.0f);
        p[RP::Retrigger] = static_cast<float>(retrig);
        p[RP::Decay] = 1.0f;
        rig.run(T, 64, p.data());
        p[RP::Trigger] = 1.0f;
        rig.run(4800, 64, p.data());
        p[RP::Trigger] = 0.0f;
        rig.run(64, 64, p.data());
        p[RP::Trigger] = 1.0f;     // second press at T + 4864
        rig.run(64, 64, p.data());
        return p;
    };
    {   // Restart: new chunk starts immediately
        Rig rig(48000.0, 64, ramp(total), ramp(total));
        auto p = runCase(0, 0, rig);
        CM_EXPECT(rig.rc->getTriggerCount() == 2u);
        CM_EXPECT(rig.rc->getLastTriggerSample() == T + 4864);
        rig.run(total - rig.pos, 64, p.data());
        // after the restart fade-in, the new chunk plays in exact reverse
        const int T2 = T + 4864;
        CM_EXPECT(rig.outL[static_cast<size_t>(T2 + 5000)] == static_cast<float>(T2 - 1 - 5000));
    }
    {   // Ignore
        Rig rig(48000.0, 64, ramp(total), ramp(total));
        auto p = runCase(0, 1, rig);
        rig.run(total - rig.pos, 64, p.data());
        CM_EXPECT(rig.rc->getTriggerCount() == 1u);
        CM_EXPECT(rig.outL[static_cast<size_t>(T + 10000)] == static_cast<float>(T - 1 - 10000));
    }
    {   // Queue: fires when the current voice reaches its fade-out, overlapping by xf
        Rig rig(48000.0, 64, ramp(total), ramp(total));
        auto p = runCase(0, 2, rig);
        CM_EXPECT(rig.rc->getTriggerCount() == 1u);
        CM_EXPECT(rig.rc->getViz().pending);
        rig.run(total - rig.pos, 64, p.data());
        CM_EXPECT(rig.rc->getTriggerCount() == 2u);
        CM_EXPECT(rig.rc->getLastTriggerSample() == T + len - xf);
        CM_EXPECT(!rig.rc->getViz().pending);
    }
    {   // Repeat + Ignore: a second trigger stops the (endless, Decay = 1) loop
        Rig rig(48000.0, 64, ramp(total), ramp(total));
        auto p = runCase(2, 1, rig);
        rig.run(total - rig.pos, 64, p.data());
        CM_EXPECT(fxtest::maxAbs(rig.outL, static_cast<size_t>(T + 4928 + xf + 64)) == 0.0f);
    }
}

CM_TEST(reverse_crossfades_produce_no_clicks_or_invalid_samples) {
    const double sr = 48000.0;
    const int total = static_cast<int>(sr * 4);
    const float sineStep = 0.5f * 2.0f * 3.14159265f * 100.0f / 48000.0f;
    auto sig = fxtest::sine(total, sr, 100.0, 0.5f);
    struct Case { int mode; float chunk; float xf; int retrig; };
    const Case cases[] = { { 1, 0.25f, 5.0f, 0 }, { 1, 0.3f, 500.0f, 0 }, { 0, 0.25f, 5.0f, 0 }, { 0, 0.25f, 20.0f, 2 },
                           { 2, 0.25f, 5.0f, 0 }, { 2, 0.2f, 5.0f, 2 } };
    for (const auto& c : cases) {
        Rig rig(sr, 240, sig, sig);
        auto p = revParams(c.mode, c.chunk, c.xf);
        p[RP::Retrigger] = static_cast<float>(c.retrig);
        p[RP::Decay] = 0.8f;
        // Trigger presses every 0.1 s (rising edge each 4800 samples).
        for (int b = 0; b < total / 240; ++b) {
            p[RP::Trigger] = (b % 20) < 10 && b >= 20 ? 1.0f : 0.0f;
            rig.run(240, 240, p.data());
        }
        CM_EXPECT(fxtest::allFinite(rig.outL) && fxtest::allFinite(rig.outR));
        CM_EXPECT(fxtest::maxJump(rig.outL) < 5.0f * sineStep);
        CM_EXPECT(fxtest::maxJump(rig.outR) < 5.0f * sineStep);
        CM_EXPECT(fxtest::maxAbs(rig.outL) <= 1.0f);   // overlapping equal-power fades never exceed 2x the input peak
    }
}

CM_TEST(reverse_continuous_mode_is_continuous) {
    const double sr = 48000.0;
    const int total = static_cast<int>(sr * 3);
    fxtest::Lcg rng;
    std::vector<float> noise(static_cast<size_t>(total));
    for (auto& v : noise) v = 0.3f * rng.next();
    Rig rig(sr, 512, noise, noise);
    auto p = revParams(1, 0.25f, 10.0f);
    rig.run(total, 512, p.data());
    CM_EXPECT(fxtest::maxAbs(rig.outL, 0, 12000) == 0.0f);   // first chunk still being captured
    for (int w = 12000; w + 480 < total; w += 480)
        CM_EXPECT(fxtest::energy(rig.outL, static_cast<size_t>(w), static_cast<size_t>(w + 480)) > 0.1);
    const auto viz = rig.rc->getViz();
    CM_EXPECT(viz.state == 2 || viz.state == 3);
}

CM_TEST(reverse_trigger_modes_repeating_transient_beat) {
    const double sr = 48000.0;
    {   // Repeating: every len - xf after the first full chunk
        const int total = 48000;
        Rig rig(sr, 64, ramp(total), ramp(total));
        auto p = revParams(0, 0.25f, 5.0f);
        p[RP::TrigMode] = 1.0f;
        rig.run(total, 64, p.data());
        CM_EXPECT(rig.rc->getTriggerCount() == 4u);
        CM_EXPECT(rig.rc->getLastTriggerSample() == 12000 + 3 * (12000 - 240));
    }
    {   // Transient: quiet bed, burst at 1 s
        const int total = 96000;
        fxtest::Lcg rng;
        std::vector<float> sig(static_cast<size_t>(total));
        for (int i = 0; i < total; ++i)
            sig[static_cast<size_t>(i)] = (i >= 48000 && i < 72000 ? 0.5f : 0.001f) * rng.next();
        Rig rig(sr, 64, sig, sig);
        auto p = revParams(0, 0.25f, 5.0f);
        p[RP::TrigMode] = 2.0f;
        rig.run(total, 64, p.data());
        CM_EXPECT(rig.rc->getTriggerCount() == 1u);
        CM_EXPECT(rig.rc->getLastTriggerSample() >= 48000 && rig.rc->getLastTriggerSample() <= 48000 + 240);
    }
    {   // Beat-sync at 120 bpm, 1 beat, playhead starting at ppq 0.5
        const int total = 120000;
        const double bpm = 120.0;
        auto ctxAt = [&](int pos) {
            FxContext c; c.hasTempo = true; c.playing = true; c.bpm = bpm;
            c.ppqPosition = 0.5 + pos * bpm / 60.0 / sr;
            return c;
        };
        for (int div : { 0, 2 }) {
            Rig rig(sr, 64, ramp(total), ramp(total));
            auto p = revParams(0, 0.25f, 5.0f);
            p[RP::TrigMode] = 3.0f; p[RP::BeatDiv] = static_cast<float>(div);
            rig.run(total, 64, p.data(), ctxAt);
            if (div == 0) {
                CM_EXPECT(rig.rc->getTriggerCount() == 5u);   // ppq 1,2,3,4,5
                CM_EXPECT(std::llabs(rig.rc->getLastTriggerSample() - 108000) <= 1);
            } else {
                CM_EXPECT(rig.rc->getTriggerCount() == 1u);   // ppq 4 only (1 bar)
                CM_EXPECT(std::llabs(rig.rc->getLastTriggerSample() - 84000) <= 1);
            }
        }
        // Block size does not change beat trigger timing.
        Rig a(sr, 1024, ramp(total), ramp(total));
        auto p = revParams(0, 0.25f, 5.0f);
        p[RP::TrigMode] = 3.0f; p[RP::BeatDiv] = 0.0f;
        a.run(total, 1000, p.data(), ctxAt);
        CM_EXPECT(a.rc->getTriggerCount() == 5u);
        CM_EXPECT(std::llabs(a.rc->getLastTriggerSample() - 108000) <= 1);
        // No tempo / not playing: falls back to manual (no automatic triggers).
        Rig b(sr, 64, ramp(total), ramp(total));
        b.run(total, 64, p.data(), [&](int pos) { auto c = ctxAt(pos); c.hasTempo = false; return c; });
        CM_EXPECT(b.rc->getTriggerCount() == 0u);
        Rig c(sr, 64, ramp(total), ramp(total));
        c.run(total, 64, p.data(), [&](int pos) { auto x = ctxAt(pos); x.playing = false; return x; });
        CM_EXPECT(c.rc->getTriggerCount() == 0u);
    }
}

CM_TEST(reverse_viz_sanity) {
    const int T = 48000, total = T + 30000;
    Rig rig(48000.0, 64, ramp(total), ramp(total));
    auto p = revParams(0, 0.5f, 5.0f);
    rig.run(T, 64, p.data());
    auto v = rig.rc->getViz();
    CM_EXPECT(v.state == 0 && !v.pending && v.wet01 == 0.0f);
    const float cap = static_cast<float>(rig.rc->getCapacitySamples());
    CM_EXPECT_NEAR(v.writeHead01, static_cast<float>(T) / cap, 1e-6);
    p[RP::Trigger] = 1.0f;
    rig.run(2000, 64, p.data());
    const auto a = rig.rc->getViz();
    rig.run(2000, 64, p.data());
    const auto b = rig.rc->getViz();
    CM_EXPECT(a.state == 2 && b.state == 2);
    CM_EXPECT_NEAR(a.storedLen01, 24000.0f / cap, 1e-6);
    CM_EXPECT_NEAR(a.storedStart01, static_cast<float>(T - 24000) / cap, 1e-6);
    CM_EXPECT(b.playHead01 < a.playHead01);                  // moves backward
    CM_EXPECT_NEAR(a.playHead01 - b.playHead01, 2000.0f / cap, 1e-5);
    CM_EXPECT(b.writeHead01 > a.writeHead01);                // capture continues
    CM_EXPECT_NEAR(a.crossfade01, 240.0f / cap, 1e-6);
    CM_EXPECT_NEAR(a.wet01, 1.0, 1e-6);
    for (float f : { a.writeHead01, a.playHead01, a.storedStart01, a.storedLen01, a.crossfade01, a.wet01 })
        CM_EXPECT(f >= 0.0f && f <= 1.0f);
    rig.run(total - rig.pos, 64, p.data());
    CM_EXPECT(rig.rc->getViz().state == 0);
}

CM_TEST(reverse_block_size_independence_and_safety) {
    const double sr = 48000.0;
    const int total = 48000 * 2;
    fxtest::Lcg rng;
    std::vector<float> sig(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) sig[static_cast<size_t>(i)] = ((i / 6000) % 3 == 0 ? 0.6f : 0.01f) * rng.next();
    for (int mode : { 0, 1, 2 }) {
        auto p = revParams(mode, 0.3f, 15.0f);
        p[RP::TrigMode] = 2.0f; p[RP::Retrigger] = 2.0f; p[RP::StereoLink] = 0.0f; p[RP::Decay] = 0.7f;
        Rig ref(sr, total, sig, sig);
        ref.run(total, total, p.data());
        for (int b : { 1, 64, 512, 2048 }) {
            Rig r(sr, b, sig, sig);
            r.run(total, b, p.data());
            CM_EXPECT(fxtest::maxDiff(r.outL, ref.outL) <= 1e-5f && fxtest::maxDiff(r.outR, ref.outR) <= 1e-5f);
        }
    }
    // Non-finite input / params, bypass, and a 192 kHz smoke run.
    auto bad = sig;
    for (size_t i = 0; i < bad.size(); i += 333) bad[i] = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 7; i < bad.size(); i += 501) bad[i] = -std::numeric_limits<float>::infinity();
    Rig r(192000.0, 512, bad, bad);
    auto p = revParams(1, 0.25f, 5.0f);
    r.run(total / 2, 512, p.data());
    std::vector<float> nanP(RP::Count, std::numeric_limits<float>::quiet_NaN());
    r.run(total / 4, 512, nanP.data());
    CM_EXPECT(fxtest::allFinite(r.outL) && fxtest::allFinite(r.outR));
    p[RP::Bypass] = 1.0f;
    const int at = r.pos;
    r.run(total - at, 512, p.data());
    CM_EXPECT(fxtest::maxAbs(r.outL, static_cast<size_t>(at + 2048 + 512)) == 0.0f);
    CM_EXPECT(r.rc->getViz().state == 0);
}
