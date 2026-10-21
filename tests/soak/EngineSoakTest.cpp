// Nightly engine soak: max-channel stress and compressed-time long run (TESTING_SPEC section 12).
#include "SoakSupport.h"
#include <iostream>

using namespace soak;

namespace {

constexpr int kBlockSizes[] = { 32, 64, 128, 256, 512, 1024, 2048, 1, 480 };

struct RunReport {
    bool finite = true;
    float peak = 0.0f;
    std::uint64_t audioThreadAllocs = 0;
    double audioSeconds = 0.0, wallSeconds = 0.0;
};

/** Drives `seconds` of audio through the processor, wiggling parameters twice per audio-second. The first
    `warmupSeconds` are excluded from the audio-thread allocation count. */
RunReport drive(CrowdMikeAudioProcessor& p, int channels, double sr, double seconds, double warmupSeconds,
                bool varyBlocks, std::function<void(double)> everyHalfSecond = {}) {
    RunReport r;
    Source src;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(channels, 2048);
    const auto wall0 = nowSec();
    std::int64_t frames = 0, nextTick = 0;
    const auto total = (std::int64_t) (seconds * sr), warm = (std::int64_t) (warmupSeconds * sr);
    std::size_t bi = 0;
    while (frames < total) {
        const int n = varyBlocks ? kBlockSizes[bi++ % (sizeof(kBlockSizes) / sizeof(int))] : 256;
        buf.setSize(channels, n, false, false, true);
        src.fill(buf, sr);
        if (frames >= nextTick) {
            nextTick += (std::int64_t) (0.5 * sr);
            if (everyHalfSecond) everyHalfSecond((double) frames / sr);
        }
        const auto a0 = threadAllocCount();
        p.processBlock(buf, midi);
        if (frames >= warm) r.audioThreadAllocs += threadAllocCount() - a0;
        const auto s = scan(buf);
        r.finite = r.finite && s.finite;
        r.peak = std::max(r.peak, s.peak);
        frames += n;
    }
    r.audioSeconds = (double) frames / sr;
    r.wallSeconds = nowSec() - wall0;
    return r;
}

void wiggle(CrowdMikeAudioProcessor& p, double t, int inputs) {
    const int k = (int) (t * 2.0);
    const juce::String in = "input" + juce::String(1 + k % inputs) + ".";
    setParam(p, in + "trimDb", (float) (-6.0 + 6.0 * std::sin(t * 0.3)));
    setParam(p, in + "pan", (float) std::sin(t * 0.17));
    setParam(p, "fx.delay.timeMs", (float) (200.0 + 150.0 * std::sin(t * 0.11)));
    setParam(p, "fx.tape.wow", (float) (0.5 + 0.5 * std::sin(t * 0.07)));
    setParam(p, "master.width", (float) (1.0 + 0.5 * std::sin(t * 0.05)));
    if (k % 20 == 0) setParam(p, "fx.echo.trigger", 1.0f);
    if (k % 20 == 1) setParam(p, "fx.echo.trigger", 0.0f);
}

void maxChannelStress(double sr) {
    CrowdMikeAudioProcessor p;
    CM_EXPECT(setLayout(p, 16, 16));
    p.prepareToPlay(sr, 2048);
    enableEverything(p, 16);
    const double seconds = envSeconds("CROWDMIKE_SOAK_STRESS_SECONDS", 120.0);
    const auto r = drive(p, 16, sr, seconds, 2.0, true, [&](double t) { wiggle(p, t, 16); });
    const double rt = r.audioSeconds / r.wallSeconds;
    std::cout << "  stress 16x16 @" << sr << " Hz: " << r.audioSeconds << " s audio in " << r.wallSeconds
              << " s wall (" << rt << "x realtime), peak " << r.peak << ", audio-thread allocs " << r.audioThreadAllocs << '\n';
    CM_EXPECT(p.getActiveInputChannelCount() == 16 && p.getActiveOutputChannelCount() == 16);
    CM_EXPECT(r.finite);
    CM_EXPECT(r.peak < 4.0f);
    CM_EXPECT(r.audioThreadAllocs == 0);
    CM_EXPECT(rt > 1.0);   // 16x16 with every effect must still run faster than real time on the CI runner
}

} // namespace

CM_TEST(Soak_Stress_16x16_AllFx_48k) { maxChannelStress(48000.0); }
CM_TEST(Soak_Stress_16x16_AllFx_96k) { maxChannelStress(96000.0); }

/** >= 10 minutes of audio time (CROWDMIKE_SOAK_LONG_SECONDS, default 600) with memory-growth assertions on the
    process allocation count and resident set size, sampled every 30 audio-seconds after a warm-up. */
CM_TEST(Soak_LongRun_NoMemoryGrowth) {
    constexpr double sr = 48000.0;
    CrowdMikeAudioProcessor p;
    CM_EXPECT(setLayout(p, 16, 16));
    p.prepareToPlay(sr, 1024);
    enableEverything(p, 16);

    const double total = envSeconds("CROWDMIKE_SOAK_LONG_SECONDS", 600.0);
    const double warm = std::min(60.0, total * 0.2);
    const double slice = 30.0;
    struct Sample { double t; std::int64_t outstanding, rss; };
    std::vector<Sample> samples;
    std::uint64_t audioAllocs = 0;
    bool finite = true;
    double done = 0.0;
    int scene = 0;
    while (done < total) {
        const double len = std::min(slice, total - done);
        const auto r = drive(p, 16, sr, len, std::min(warm - done, len) > 0.0 ? std::min(warm - done, len) : 0.0, true,
                             [&](double t) { wiggle(p, t + done, 16); });
        audioAllocs += r.audioThreadAllocs;
        finite = finite && r.finite;
        done += len;
        p.recallScene(scene++ % 4);   // control-thread churn: scene recall must not leak
        if (done >= warm) samples.push_back({ done, globalAllocStats().outstanding(), residentBytes() });
    }
    CM_EXPECT(finite);
    CM_EXPECT(audioAllocs == 0);
    CM_EXPECT(samples.size() >= 2);
    if (samples.size() >= 2) {
        const auto& a = samples.front();
        const auto& b = samples.back();
        std::cout << "  long run " << done << " s audio: outstanding allocs " << a.outstanding << " -> " << b.outstanding
                  << ", RSS " << a.rss / 1048576 << " -> " << b.rss / 1048576 << " MiB\n";
        CM_EXPECT(b.outstanding - a.outstanding <= 512);                 // no per-block / per-slice leak
        if (a.rss > 0 && b.rss > 0) CM_EXPECT(b.rss - a.rss <= (48ll << 20));   // allocator noise allowance
    }
}
