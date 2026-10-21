#include "RecTestUtil.h"
#include <filesystem>
#include "Recording/PlaybackEngine.h"
#include "Recording/AudioFileLoader.h"

using namespace crowdmike::rec;
using PE = PlaybackEngine;

namespace {

struct Bus {
    std::vector<float> L[PE::kNumDestinations], R[PE::kNumDestinations];
    float* l[PE::kNumDestinations] {};
    float* r[PE::kNumDestinations] {};
    explicit Bus(int n) {
        for (int i = 0; i < PE::kNumDestinations; ++i) {
            L[i].assign((size_t) n, 0.0f); R[i].assign((size_t) n, 0.0f);
            l[i] = L[i].data(); r[i] = R[i].data();
        }
    }
    void clear() { for (int i = 0; i < PE::kNumDestinations; ++i) { std::fill(L[i].begin(), L[i].end(), 0.0f); std::fill(R[i].begin(), R[i].end(), 0.0f); } }
};

/** Renders `total` samples; returns destination `dest` (L, and R in *right). */
std::vector<float> render(PE& pe, int total, int block, int dest = 0, bool mute = false, std::vector<float>* right = nullptr,
                          std::vector<float>* allOthers = nullptr) {
    Bus bus(block);
    std::vector<float> out;
    for (int done = 0; done < total; done += block) {
        const int n = std::min(block, total - done);
        bus.clear();
        pe.process(bus.l, bus.r, n, mute);
        out.insert(out.end(), bus.L[dest].begin(), bus.L[dest].begin() + n);
        if (right != nullptr) right->insert(right->end(), bus.R[dest].begin(), bus.R[dest].begin() + n);
        if (allOthers != nullptr)
            for (int d = 0; d < PE::kNumDestinations; ++d)
                if (d != dest)
                    for (int i = 0; i < n; ++i) allOthers->push_back(std::abs(bus.L[d][(size_t) i]) + std::abs(bus.R[d][(size_t) i]));
    }
    return out;
}

juce::File makeClip(const juce::File& dir, const juce::String& name, double sr, int frames, int ch,
                    const std::function<float(int, int)>& fn, int bits = 32) {
    juce::AudioBuffer<float> b(ch, frames);
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < frames; ++i) b.setSample(c, i, fn(i, c));
    auto f = dir.getChildFile(name);
    CM_EXPECT(rectest::writeWav(f, b, sr, bits));
    return f;
}

bool loadAndWait(PE& pe, int slot, const juce::File& f) {
    pe.loadClip(slot, f);
    return pe.waitForLoads(10000) && pe.getClipInfo(slot).loaded;
}

float rampX(int i) { return (float) (i + 1) / 1000.0f; }

} // namespace

CM_TEST(Playback_DecodeAndResample44kInto48k) {
    rectest::TempDir tmp("pb");
    const double f0 = 1000.0;
    auto file = makeClip(tmp.dir, "sine44.wav", 44100.0, 44100, 1,
                         [&](int i, int) { return 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f0 * i / 44100.0); });
    PE pe;
    pe.prepare(48000.0, 512);
    CM_EXPECT(loadAndWait(pe, 0, file));
    const auto info = pe.getClipInfo(0);
    CM_EXPECT_NEAR(info.durationSeconds, 1.0, 1e-4);
    CM_EXPECT_NEAR(info.sourceSampleRate, 44100.0, 0.0);
    CM_EXPECT(info.name == "sine44");
    pe.trigger(0);
    const auto out = render(pe, 48000 + 1024, 512);
    double maxErr = 0.0;
    for (int k = 480; k < 47000; ++k)
        maxErr = std::max(maxErr, std::abs(out[(size_t) k] - 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * f0 * k / 48000.0)));
    CM_EXPECT_NEAR(maxErr, 0.0, 2e-3);
    for (int k = 48000; k < 48000 + 1024; ++k) CM_EXPECT(out[(size_t) k] == 0.0f);
    // Down-sampling (96k file into 48k engine) keeps the level of an in-band tone.
    auto file96 = makeClip(tmp.dir, "sine96.wav", 96000.0, 96000, 1,
                           [&](int i, int) { return 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * f0 * i / 96000.0); });
    juce::AudioBuffer<float> in(1, 96000), outBuf;
    AudioFileLoader::Result r;
    juce::String err;
    CM_EXPECT(AudioFileLoader::load(file96, 48000.0, 2, 0, 0, r, err));
    CM_EXPECT(r.audio.getNumSamples() == 48000);
    double e2 = 0.0;
    for (int k = 1000; k < 47000; ++k)
        e2 = std::max(e2, std::abs(r.audio.getSample(0, k) - 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * f0 * k / 48000.0)));
    CM_EXPECT_NEAR(e2, 0.0, 3e-3);
}

CM_TEST(Playback_LoopWrapIsSampleAccurate) {
    rectest::TempDir tmp("pb");
    auto file = makeClip(tmp.dir, "ramp.wav", 48000.0, 1000, 1, [](int i, int) { return rampX(i); });
    for (int block : { 1, 64, 333, 2048 }) {
        PE pe;
        pe.prepare(48000.0, 2048);
        CM_EXPECT(loadAndWait(pe, 3, file));
        PE::ClipSettings s;
        s.mode = PE::ClipSettings::Loop;
        s.startFrac = 0.2;
        s.endFrac = 0.7;
        pe.setSettings(3, s);
        pe.trigger(3);
        const auto out = render(pe, 3000, block);
        int bad = 0;
        for (int k = 256; k < 3000; ++k)
            if (out[(size_t) k] != rampX(200 + k % 500)) ++bad;
        CM_EXPECT(bad == 0);
        CM_EXPECT(out[0] > 0.0f && out[0] < rampX(200) * 0.01f);   // click-free start ramp
        CM_EXPECT(pe.getClipInfo(3).playing);
    }
}

CM_TEST(Playback_OneShotEndsCleanlyAndTriggerOffset) {
    rectest::TempDir tmp("pb");
    auto file = makeClip(tmp.dir, "const.wav", 48000.0, 1000, 1, [](int, int) { return 0.5f; });
    PE pe;
    pe.prepare(48000.0, 256);
    CM_EXPECT(loadAndWait(pe, 0, file));
    pe.trigger(0, 100);   // sample-accurate start inside the first block
    const auto out = render(pe, 2000, 256);
    CM_EXPECT(out[99] == 0.0f && out[100] > 0.0f);
    CM_EXPECT(out[600] == 0.5f);
    CM_EXPECT(out[1099] > 0.0f && out[1099] < 0.01f);   // fades out over >= 5 ms before the end
    for (int k = 1100; k < 2000; ++k) CM_EXPECT(out[(size_t) k] == 0.0f);
    CM_EXPECT(! pe.getClipInfo(0).playing);
}

CM_TEST(Playback_ReverseTrimGainFades) {
    rectest::TempDir tmp("pb");
    auto file = makeClip(tmp.dir, "ramp.wav", 48000.0, 1000, 1, [](int i, int) { return rampX(i); });
    PE pe;
    pe.prepare(48000.0, 128);
    CM_EXPECT(loadAndWait(pe, 1, file));
    PE::ClipSettings s;
    s.mode = PE::ClipSettings::Loop;
    s.reverse = true;
    pe.setSettings(1, s);
    pe.trigger(1);
    auto out = render(pe, 2500, 128);
    int bad = 0;
    for (int k = 256; k < 2500; ++k)
        if (out[(size_t) k] != rampX(999 - k % 1000)) ++bad;
    CM_EXPECT(bad == 0);

    // Reverse + trim: plays [300, 600) backwards.
    s.startFrac = 0.3; s.endFrac = 0.6;
    pe.setSettings(1, s);
    pe.trigger(1);
    out = render(pe, 1000, 128);
    bad = 0;
    for (int k = 300; k < 1000; ++k)
        if (out[(size_t) k] != rampX(599 - k % 300)) ++bad;
    CM_EXPECT(bad == 0);

    // Gain + fade-in.
    auto cfile = makeClip(tmp.dir, "c.wav", 48000.0, 48000, 1, [](int, int) { return 0.5f; });
    CM_EXPECT(loadAndWait(pe, 2, cfile));
    PE::ClipSettings g;
    g.gainDb = -6.0f;
    g.fadeInMs = 10.0f;   // 480 frames
    g.fadeOutMs = 20.0f;
    pe.setSettings(2, g);
    pe.stopAll();
    render(pe, 1024, 128);
    pe.trigger(2);
    out = render(pe, 2048, 128);
    const float gl = std::pow(10.0f, -6.0f / 20.0f);
    double maxErr = 0.0;
    for (int k = 0; k < 480; ++k) maxErr = std::max(maxErr, (double) std::abs(out[(size_t) k] - 0.5f * gl * (float) k / 480.0f));
    CM_EXPECT_NEAR(maxErr, 0.0, 1e-6);
    CM_EXPECT_NEAR(out[1000], 0.5 * gl, 1e-6);
    // Stop honours the fade-out (960 frames) and ends at silence.
    pe.stop(2);
    out = render(pe, 2048, 128);
    CM_EXPECT(out[480] > 0.1f * gl && out[480] < 0.5f * gl);
    for (int k = 970; k < 2048; ++k) CM_EXPECT(out[(size_t) k] == 0.0f);
}

CM_TEST(Playback_RoutingAndStereo) {
    rectest::TempDir tmp("pb");
    auto st = makeClip(tmp.dir, "st.wav", 48000.0, 4800, 2, [](int, int c) { return c == 0 ? 0.25f : -0.5f; });
    PE pe;
    pe.prepare(48000.0, 64);
    CM_EXPECT(loadAndWait(pe, 5, st));
    CM_EXPECT(pe.getClipInfo(5).numChannels == 2);
    for (int route : { 0, 3, 9 }) {
        PE::ClipSettings s;
        s.mode = PE::ClipSettings::Loop;
        s.route = route;
        pe.setSettings(5, s);
        pe.trigger(5);
        render(pe, 512, 64);   // let route crossfades settle
        std::vector<float> right, others;
        const auto left = render(pe, 512, 64, route, false, &right, &others);
        CM_EXPECT(left[100] == 0.25f && right[100] == -0.5f);
        float o = 0.0f;
        for (auto v : others) o = std::max(o, v);
        CM_EXPECT(o == 0.0f);
    }
    // Null destinations are tolerated.
    float* l[PE::kNumDestinations] {};
    std::vector<float> only(64, 0.0f);
    l[9] = only.data();
    pe.process(l, nullptr, 64, false);
    pe.process(nullptr, nullptr, 64, false);
    CM_EXPECT(only[10] == 0.25f);
}

CM_TEST(Playback_PanicMuteRampsOutAndBlocksTriggers) {
    rectest::TempDir tmp("pb");
    auto file = makeClip(tmp.dir, "c.wav", 48000.0, 9600, 1, [](int, int) { return 0.5f; });
    PE pe;
    pe.prepare(48000.0, 512);
    CM_EXPECT(loadAndWait(pe, 0, file));
    PE::ClipSettings s;
    s.mode = PE::ClipSettings::Loop;
    pe.setSettings(0, s);
    pe.trigger(0);
    render(pe, 1024, 512);
    auto out = render(pe, 512, 512, 0, true);
    CM_EXPECT(out[0] > 0.45f);                   // ramp, not a hard cut
    for (int k = 250; k < 512; ++k) CM_EXPECT(out[(size_t) k] == 0.0f);   // gone within ~5 ms
    for (int k = 1; k < 241; ++k) CM_EXPECT(out[(size_t) k] <= out[(size_t) k - 1]);
    CM_EXPECT(! pe.getClipInfo(0).playing);
    pe.trigger(0);
    out = render(pe, 512, 512, 0, true);
    for (auto v : out) CM_EXPECT(v == 0.0f);
    pe.trigger(0);
    out = render(pe, 512, 512, 0, false);
    CM_EXPECT(out[400] == 0.5f);
}

CM_TEST(Playback_InvalidFilesReportErrors) {
    rectest::TempDir tmp("pb");
    auto bad = tmp.dir.getChildFile("corrupt.wav");
    juce::MemoryBlock junk(4096);
    juce::Random rnd(7);
    for (size_t i = 0; i < junk.getSize(); ++i) static_cast<char*>(junk.getData())[i] = (char) rnd.nextInt(256);
    bad.replaceWithData(junk.getData(), junk.getSize());
    // Truncated: valid header but data chunk cut short.
    auto good = makeClip(tmp.dir, "good.wav", 48000.0, 48000, 1, [](int, int) { return 0.1f; });
    juce::MemoryBlock gm;
    good.loadFileAsData(gm);
    auto trunc = tmp.dir.getChildFile("trunc.wav");
    trunc.replaceWithData(gm.getData(), 30);
    // Longer than the 24 h limit is rejected from the header alone: a WAV header that declares 25 h at 8 kHz.
    auto longF = makeClip(tmp.dir, "long.wav", 8000.0, 8000, 1, [](int, int) { return 0.0f; }, 16);
    {
        juce::MemoryBlock lm;
        longF.loadFileAsData(lm);
        auto* d = static_cast<unsigned char*>(lm.getData());
        const juce::uint32 dataBytes = (juce::uint32) (25.0 * 3600 * 8000 * 2);   // < 4 GB
        size_t dataAt = 40;   // locate the 'data' chunk size field
        for (size_t k = 12; k + 8 < lm.getSize(); ++k)
            if (std::memcmp(d + k, "data", 4) == 0) { dataAt = k + 4; break; }
        for (int k = 0; k < 4; ++k) d[dataAt + (size_t) k] = (unsigned char) ((dataBytes >> (8 * k)) & 0xff);
        longF.replaceWithData(lm.getData(), lm.getSize());
        std::error_code ec;
        std::filesystem::resize_file(longF.getFullPathName().toStdString(), dataAt + 4 + dataBytes, ec);   // sparse
    }

    PE pe;
    pe.prepare(48000.0, 256);
    pe.loadClip(0, bad);
    pe.loadClip(1, tmp.dir.getChildFile("missing.wav"));
    pe.loadClip(2, longF);
    pe.loadClip(3, trunc);
    CM_EXPECT(pe.waitForLoads(20000));
    for (int s = 0; s < 4; ++s) {
        const auto i = pe.getClipInfo(s);
        CM_EXPECT(! i.loaded && ! i.loading && i.error.isNotEmpty());
    }
    CM_EXPECT(pe.getClipInfo(2).error.contains("too long"));
    CM_EXPECT(pe.getClipInfo(1).error.contains("not found"));
    pe.trigger(0);
    auto out = render(pe, 512, 256);
    for (auto v : out) CM_EXPECT(v == 0.0f);
    float mn[8], mx[8];
    CM_EXPECT(! pe.getWaveform(0, mn, mx, 8));
}

CM_TEST(Playback_ManyClipsUnicodeWaveformAndSwap) {
    rectest::TempDir tmp("pb");
    PE pe;
    pe.prepare(48000.0, 256);
    for (int i = 0; i < PE::kMaxClips; ++i) {
        const auto name = juce::String::fromUTF8("Klatsch_\xe6\x97\xa5\xe6\x9c\xac_\xc3\xbc_") + juce::String(i) + ".wav";
        auto f = makeClip(tmp.dir, name, i % 2 == 0 ? 48000.0 : 44100.0, 12000, 1 + i % 2,
                          [i](int k, int) { return (k == 5000 ? 0.8f : (k == 7000 ? -0.3f : 0.01f * (float) (i + 1))); });
        pe.loadClip(i, f);
    }
    CM_EXPECT(pe.waitForLoads(20000));
    for (int i = 0; i < PE::kMaxClips; ++i) {
        const auto info = pe.getClipInfo(i);
        CM_EXPECT(info.loaded && info.error.isEmpty());
        CM_EXPECT(info.name.contains(juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac")));
        pe.trigger(i);
    }
    float mn[64], mx[64];
    CM_EXPECT(pe.getWaveform(0, mn, mx, 64));
    float gmx = -1, gmn = 1;
    for (int p = 0; p < 64; ++p) { gmx = std::max(gmx, mx[p]); gmn = std::min(gmn, mn[p]); }
    CM_EXPECT_NEAR(gmx, 0.8, 1e-6);
    CM_EXPECT_NEAR(gmn, -0.3, 1e-6);
    auto out = render(pe, 2048, 256);
    double sum = 0.0;
    for (int i = 0; i < PE::kMaxClips; ++i) sum += 0.01 * (i + 1);
    CM_EXPECT_NEAR(out[1000], sum, 1e-4);
    // Replace a playing clip (swap while playing) and unload others; old data is released off the audio thread.
    auto other = makeClip(tmp.dir, "other.wav", 48000.0, 4800, 1, [](int, int) { return 0.0f; });
    for (int round = 0; round < 3; ++round) {
        CM_EXPECT(loadAndWait(pe, 0, other));
        render(pe, 512, 256);
        for (int i = 1; i < PE::kMaxClips; i += 2) pe.unloadClip(i);
        render(pe, 512, 256);
        pe.releaseRetired();
    }
    CM_EXPECT(! pe.getClipInfo(1).loaded);
    CM_EXPECT(pe.getClipInfo(2).loaded);
    pe.releaseResources();
}

namespace {
std::vector<float> renderBed(unsigned seed, float lowpass, int seconds) {
    rectest::TempDir tmp("bed");
    auto f = makeClip(tmp.dir, "crowd.wav", 48000.0, 4 * 48000, 1,
                      [](int i, int) { return 0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 200.0 * i / 48000.0); });
    PE pe;
    pe.prepare(48000.0, 480);
    CM_EXPECT(loadAndWait(pe, 7, f));
    PE::BedSettings b;
    b.slot = 7; b.enabled = true; b.minLoopSec = 0.5; b.maxLoopSec = 1.0; b.crossfadeSec = 0.2;
    b.levelVariationDb = 3.0f; b.lowpassHz = lowpass; b.gainDb = 0.0f; b.route = 2; b.seed = seed;
    pe.setBed(b);
    return render(pe, seconds * 48000, 480, 2);
}
} // namespace

CM_TEST(Playback_CrowdBedDeterministicSeamlessBounded) {
    const auto a = renderBed(42, 0.0f, 10);
    const auto b = renderBed(42, 0.0f, 10);
    const auto c = renderBed(43, 0.0f, 10);
    CM_EXPECT(a == b);    // deterministic
    CM_EXPECT(a != c);    // seed matters
    double maxStep = 0.0, peak = 0.0;
    for (size_t i = 1; i < a.size(); ++i) {
        maxStep = std::max(maxStep, (double) std::abs(a[i] - a[i - 1]));
        peak = std::max(peak, (double) std::abs(a[i]));
    }
    // Plain 200 Hz sine at 0.5 changes by <= 0.0131 per sample; allow level drift + equal-power bulge, no seams.
    CM_EXPECT(maxStep < 0.04);
    CM_EXPECT(peak <= 0.5 * std::sqrt(2.0) * std::pow(10.0, 3.0 / 20.0) + 1e-3);
    CM_EXPECT(peak > 0.2);   // it actually plays (after the >= 1 s fade-in)
    const auto lp = renderBed(42, 300.0f, 4);
    double lpPeak = 0.0;
    for (auto v : lp) { CM_EXPECT(std::isfinite(v)); lpPeak = std::max(lpPeak, (double) std::abs(v)); }
    CM_EXPECT(lpPeak < peak);
}
