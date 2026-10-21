#include "GainRider.h"
#include "DspUtil.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
void GainRider::prepare(double sampleRate, int /*maxBlock*/, int channels) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    slowCoef = msToCoef(1500.0f, sr);
    fastCoef = msToCoef(50.0f, sr);
    glideCoef = msToCoef(2000.0f, sr);
    maxStep = static_cast<float>(3.0 / sr);   // 3 dB per second
    sOn.prepare(sr, 50.0f);
    reset();
}

void GainRider::reset() noexcept {
    first = true;
    settledOff = true;
    slowPow = fastPow = 0.0;
    gainDb = 0.0f;
    warm = false;
    sOn.snap(0.0f);
    gainMeter.store(0.0f, std::memory_order_relaxed);
}

namespace { constexpr double kDropPow = 15.848931924611133; }   // 12 dB in power

void GainRider::process(float* const* ch, int nch, int n, bool enabled) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    if (first) {
        first = false;
        sOn.snap(enabled ? 1.0f : 0.0f);
    }
    sOn.setTarget(enabled ? 1.0f : 0.0f);
    if (!enabled && util::same(sOn.value(), 0.0f) && !sOn.isSmoothing()) {
        if (!settledOff) {
            settledOff = true;
            slowPow = fastPow = 0.0;
            gainDb = 0.0f;
            warm = false;
        }
        gainMeter.store(0.0f, std::memory_order_relaxed);
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    settledOff = false;
    const double invCh = 1.0 / nch;
    const double sc = slowCoef, fc = fastCoef;
    const float freezePow = dbToGain(kFreezeDb) * dbToGain(kFreezeDb);
    for (int i = 0; i < n; ++i) {
        double pw = 0.0;
        for (int c = 0; c < nch; ++c) {
            const float x = util::cleanSample(ch[c][i]);
            ch[c][i] = x;
            pw += static_cast<double>(x) * x;
        }
        pw *= invCh;
        slowPow = pw + sc * (slowPow - pw);
        fastPow = pw + fc * (fastPow - pw);
        if (!warm && fastPow >= freezePow) { warm = true; slowPow = fastPow; }   // seed the slow RMS (no bogus boost)
        // freeze below -60 dBFS and while the level has suddenly dropped > 12 dB below the slow average (crowd stops)
        if (fastPow >= freezePow && fastPow * kDropPow >= slowPow) {
            const float levelDb = 10.0f * std::log10(static_cast<float>(std::max(slowPow, 1.0e-12)));
            const float desired = clampf(targetDb - levelDb, -kMaxGainDb, kMaxGainDb);
            const float next = desired + glideCoef * (gainDb - desired);
            gainDb += clampf(next - gainDb, -maxStep, maxStep);
        }
        const float g = dbToGain(gainDb);
        const float w = sOn.next();
        for (int c = 0; c < nch; ++c) {
            const float x = ch[c][i];
            ch[c][i] = x + w * (x * g - x);
        }
    }
    if (slowPow < 1.0e-30) slowPow = 0.0;
    if (fastPow < 1.0e-30) fastPow = 0.0;
    gainMeter.store(gainDb, std::memory_order_relaxed);
}
}
