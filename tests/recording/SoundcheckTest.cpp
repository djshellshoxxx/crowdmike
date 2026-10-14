#include "RecTestUtil.h"
#include "Recording/SoundcheckPlayer.h"
#include "Recording/RecorderEngine.h"

using namespace crowdmike::rec;
using SP = SoundcheckPlayer;

namespace {
constexpr float kLive = 0.9f;
constexpr double kSr = 48000.0;
constexpr int kBlock = 480;

float sig(int input, std::int64_t i) { return 0.05f + 0.01f * (float) input + 0.2f * (float) (i % 97) / 97.0f; }

struct Inputs {
    std::vector<float> buf[SP::kMaxInputs];
    float* ptr[SP::kMaxInputs] {};
    Inputs() { for (int i = 0; i < SP::kMaxInputs; ++i) { buf[i].assign(kBlock, kLive); ptr[i] = buf[i].data(); } }
    void live() { for (auto& b : buf) std::fill(b.begin(), b.end(), kLive); }
};

/** Processes `blocks` blocks; returns input `ch` as seen by the engine after process(). */
std::vector<float> run(SP& sp, int blocks, int ch, std::vector<std::int64_t>* blockStartPos = nullptr) {
    Inputs in;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        in.live();
        if (blockStartPos != nullptr) blockStartPos->push_back((std::int64_t) std::llround(sp.getPositionSeconds() * kSr));
        sp.process(in.ptr, SP::kMaxInputs, kBlock);
        out.insert(out.end(), in.buf[ch].begin(), in.buf[ch].end());
    }
    return out;
}

juce::File makeFile(const juce::File& dir, const juce::String& name, int ch, int frames, int firstInput, double sr = kSr) {
    juce::AudioBuffer<float> b(ch, frames);
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < frames; ++i) b.setSample(c, i, sig(firstInput + c, i));
    auto f = dir.getChildFile(name);
    CM_EXPECT(rectest::writeWav(f, b, sr));
    return f;
}
} // namespace

CM_TEST(Soundcheck_LockedByDefaultArmFadesIn) {
    rectest::TempDir tmp("sc");
    SP sp;
    sp.prepare(kSr, kBlock);
    CM_EXPECT(sp.loadFiles({ { 0, makeFile(tmp.dir, "in0.wav", 1, 4 * 48000, 0) }, { 3, makeFile(tmp.dir, "in34.wav", 2, 4 * 48000, 3) } }));
    CM_EXPECT(sp.waitForLoad(10000));
    const auto st = sp.getLoadState();
    CM_EXPECT(st.loaded && st.numInputs == 3);
    CM_EXPECT_NEAR(sp.getDurationSeconds(), 4.0, 1e-9);
    CM_EXPECT(sp.isLocked());
    CM_EXPECT(! sp.arm());   // locked: cannot be activated
    sp.play();
    auto out = run(sp, 10, 0);
    for (auto v : out) CM_EXPECT(v == kLive);   // untouched
    CM_EXPECT(! sp.isReplacingInputs());

    sp.setLocked(false);
    CM_EXPECT(! sp.isReplacingInputs());   // unlocking alone does nothing
    sp.stop();
    run(sp, 1, 0);
    sp.seekSeconds(0.0);
    CM_EXPECT(sp.arm());
    sp.play();
    out = run(sp, 300, 0);   // 3 s
    CM_EXPECT(sp.isReplacingInputs());
    // Fade-in >= 2 s: about 40 % after 1 s, still below full level at 1.9 s.
    const auto ratio = [&](int f) { return out[(size_t) f] / sig(0, f); };
    CM_EXPECT_NEAR(ratio(48000), 48000.0 / (2.5 * 48000.0), 0.01);
    CM_EXPECT(ratio((int) (1.9 * 48000)) < 0.8f);
    for (int f = 1; f < 2 * 48000; f += 997) CM_EXPECT(ratio(f) >= ratio(f > 997 ? f - 997 : 0) - 1e-6f);
    int bad = 0;
    for (int f = (int) (2.6 * 48000); f < 3 * 48000; ++f)
        if (out[(size_t) f] != sig(0, f)) ++bad;
    CM_EXPECT(bad == 0);   // then exact
    // Mapped stereo second channel, unmapped inputs are silent while armed.
    Inputs in;
    sp.process(in.ptr, SP::kMaxInputs, kBlock);
    const auto p = (std::int64_t) (3 * 48000);
    CM_EXPECT(in.buf[4][0] == sig(4, p) && in.buf[3][0] == sig(3, p));
    CM_EXPECT(in.buf[1][0] == 0.0f && in.buf[15][0] == 0.0f);
    // Locking takes effect immediately.
    sp.setLocked(true);
    in.live();
    sp.process(in.ptr, SP::kMaxInputs, kBlock);
    CM_EXPECT(! sp.isReplacingInputs() && in.buf[0][10] == kLive && ! sp.isArmed());
}

CM_TEST(Soundcheck_SeekLoopStopDisarm) {
    rectest::TempDir tmp("sc");
    SP sp;
    sp.prepare(kSr, kBlock);
    sp.loadFiles({ { 2, makeFile(tmp.dir, "a.wav", 1, 3 * 48000, 2, 44100.0) } });   // sample-rate mismatch
    CM_EXPECT(sp.waitForLoad(10000));
    CM_EXPECT_NEAR(sp.getDurationSeconds(), 3.0 * 48000.0 / 44100.0, 1e-4);   // resampled 44.1k -> 48k
    sp.setArmFadeSeconds(0.5);   // clamped to the 2 s minimum
    sp.setLocked(false);
    CM_EXPECT(sp.arm());
    run(sp, 300, 2);   // fade complete (paused: silence)
    sp.play();
    run(sp, 10, 2);
    sp.seekSeconds(1.0);
    run(sp, 1, 2);   // de-click out, jump, de-click in
    const auto p0 = (std::int64_t) std::llround(sp.getPositionSeconds() * kSr);
    CM_EXPECT(p0 == 48000 + 240 + 1 || p0 == 48000 + 240 || p0 == 48000 + 239);
    std::vector<std::int64_t> starts;
    auto out = run(sp, 2, 2, &starts);
    CM_EXPECT(starts[0] == p0 && starts[1] == p0 + kBlock);
    CM_EXPECT(out[0] != 0.0f);

    // Loop range 0.5 .. 0.6 s: position stays inside and output repeats the region.
    sp.setLoopRange(0.5, 0.6, true);
    starts.clear();
    out = run(sp, 200, 2, &starts);
    for (size_t b = 1; b < starts.size(); ++b) CM_EXPECT(starts[b] >= 24000 && starts[b] <= 28800);
    // Periodicity: away from the wrap crossfade, frames 4800 apart are identical.
    int checked = 0, bad = 0;
    for (size_t b = 20; b + 11 < starts.size(); ++b)
        for (int i = 0; i < kBlock; ++i) {
            const auto pos = starts[b] + i;
            if (pos < 24000 + 300 || pos >= 28800) continue;
            ++checked;
            if (out[b * kBlock + (size_t) i] != out[(b + 10) * kBlock + (size_t) i]) ++bad;   // 10 blocks = 4800 frames
        }
    CM_EXPECT(checked > 1000 && bad == 0);

    // Stop: fades out (250 ms), then silence and rewind to the loop start.
    sp.stop();
    out = run(sp, 40, 2);
    CM_EXPECT(out[100] != 0.0f);
    for (size_t i = 12100; i < out.size(); ++i) CM_EXPECT(out[i] == 0.0f);
    CM_EXPECT(! sp.isPlaying());
    CM_EXPECT_NEAR(sp.getPositionSeconds(), 0.5, 1e-9);

    // Disarm: fade out, then live inputs pass untouched.
    sp.disarm();
    run(sp, 60, 2);
    CM_EXPECT(! sp.isReplacingInputs());
    out = run(sp, 1, 2);
    CM_EXPECT(out[5] == kLive);
}

CM_TEST(Soundcheck_LoadsRecorderSessionWithOriginalMapping) {
    rectest::TempDir tmp("scsess");
    RecorderEngine rec;
    rec.prepare(kSr, 256, RecorderEngine::kDefaultFifoFrames, false);
    rec.setTrack(0, { 5, 1, "Crowd L", true });     // raw input 5
    rec.setTrack(1, { 2, 2, "Crowd Pair", true });  // raw inputs 2/3
    rec.setTrack(2, { 40, 1, "Master", true });     // not an input id -> first free input
    RecorderEngine::RecordOptions o;
    o.folder = tmp.dir;
    o.format = RecorderEngine::RecordOptions::Wav32f;
    o.maxFileSeconds = 1.3;   // parts must be concatenated back
    CM_EXPECT(rec.startRecording(o));
    std::vector<float> d0(256), d1(256);
    for (int b = 0; b < 3 * 48000 / 256 + 1; ++b) {
        const auto pos = rec.getFramePosition();
        for (int t = 0; t < 3; ++t) {
            if (! rec.isTrackActive(t)) continue;
            for (int i = 0; i < 256; ++i) { d0[(size_t) i] = rectest::ramp(t, pos + i, 0); d1[(size_t) i] = rectest::ramp(t, pos + i, 1); }
            const float* p[2] = { d0.data(), d1.data() };
            rec.pushTap(t, p, 2, 256);
        }
        rec.advance(256);
        if (b % 8 == 0) rec.drainForTesting();
    }
    rec.stopRecordingNow();
    SessionInfo info;
    CM_EXPECT(RecorderEngine::readSession(rec.getLastSessionFile(), info));
    CM_EXPECT(info.filesForTrack(0).size() == 3);

    SP sp;
    sp.prepare(kSr, kBlock);
    CM_EXPECT(sp.loadSession(rec.getLastSessionFile()));
    CM_EXPECT(sp.waitForLoad(10000));
    const auto st = sp.getLoadState();
    CM_EXPECT(st.loaded && st.numInputs == 4 && st.error.isEmpty());
    CM_EXPECT_NEAR(sp.getDurationSeconds(), (double) info.totalFrames / kSr, 1e-9);
    const auto map = sp.getInputMapping();
    auto find = [&](int input) { for (auto& m : map) if (m.inputIndex == input) return m; return SP::InputMapping {}; };
    CM_EXPECT(find(5).sourceId == 5 && find(5).name == "Crowd L");
    CM_EXPECT(find(2).sourceId == 2 && find(2).channel == 0 && find(3).channel == 1);
    CM_EXPECT(find(0).sourceId == 40 && find(0).track == 2);

    sp.setLocked(false);
    CM_EXPECT(sp.arm());
    sp.play();
    Inputs in;
    int bad = 0, checked = 0;
    for (int b = 0; b < 300; ++b) {
        in.live();
        sp.process(in.ptr, SP::kMaxInputs, kBlock);
        if (b * kBlock < (int) (2.6 * kSr)) continue;
        for (int i = 0; i < kBlock; ++i) {
            const std::int64_t f = info.startFrame + (std::int64_t) b * kBlock + i;
            if (f - info.startFrame >= info.totalFrames) break;
            ++checked;
            bad += in.buf[5][(size_t) i] != rectest::ramp(0, f, 0);
            bad += in.buf[2][(size_t) i] != rectest::ramp(1, f, 0);
            bad += in.buf[3][(size_t) i] != rectest::ramp(1, f, 1);
            bad += in.buf[0][(size_t) i] != rectest::ramp(2, f, 0);
            bad += in.buf[1][(size_t) i] != 0.0f;
        }
    }
    CM_EXPECT(checked > 10000 && bad == 0);
    // Invalid session file.
    SP sp2;
    sp2.prepare(kSr, kBlock);
    CM_EXPECT(! sp2.loadSession(tmp.dir.getChildFile("nope.json")));
    CM_EXPECT(sp2.getLoadState().error.isNotEmpty());
    CM_EXPECT(! sp2.arm());
}
