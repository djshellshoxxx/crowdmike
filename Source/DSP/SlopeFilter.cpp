#include "SlopeFilter.h"
#include "DspUtil.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
double SlopeFilter::butterworthQ(int poles, int section) noexcept {
    const double theta = 3.14159265358979323846 * (2.0 * section + 1.0) / (2.0 * poles);
    return 1.0 / (2.0 * std::cos(theta));
}

void SlopeFilter::prepare(double sampleRate, int /*maxBlock*/, int channels, bool highpass) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    isHighpass = highpass;
    numChannels = std::max(0, channels);
    for (auto& b : bank) b.prepare(numChannels);
    enableGain.prepare(sr, 20.0f);
    logHz.prepare(sr, 50.0f);
    xfade.prepare(sr, 30.0f);
    reset();
}

void SlopeFilter::reset() noexcept {
    for (auto& b : bank) b.reset();
    first = true;
    xfading = false;
    settledOff = false;
    cur = 0;
    countdown = 0;
    enableGain.snap(0.0f);
    logHz.snap(10.0f);
    xfade.snap(0.0f);
}

void SlopeFilter::designBank(int b, int slope, float hz) noexcept {
    const int sections = slope + 1;
    const int poles = 2 * sections;
    bank[b].setNumSections(sections);
    for (int k = 0; k < sections; ++k) {
        const auto q = static_cast<float>(butterworthQ(poles, k));
        bank[b].setSection(k, isHighpass ? designHighpass(sr, hz, q) : designLowpass(sr, hz, q));
    }
}

void SlopeFilter::controlTick() noexcept {
    if (logHz.isSmoothing()) {
        const float h = std::exp2(logHz.skip(kControlInterval));
        if (!util::same(h, appliedHz)) {
            appliedHz = h;
            designBank(cur, bankSlope[cur], h);
            if (xfading) designBank(1 - cur, bankSlope[1 - cur], h);
        }
    }
    if (xfading && !xfade.isSmoothing()) {
        cur = 1 - cur;
        xfading = false;
    }
    if (!xfading && targetSlope != bankSlope[cur]) {
        const int o = 1 - cur;
        bank[o].reset();
        bankSlope[o] = targetSlope;
        designBank(o, targetSlope, appliedHz);
        xfade.snap(0.0f);
        xfade.setTarget(1.0f);
        xfading = true;
    }
}

void SlopeFilter::process(float* const* ch, int nch, int n, bool enabled, float hz, int slopeIndex) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    const float maxHz = static_cast<float>(std::min(0.45 * sr, 30000.0));
    const float h = util::isFiniteBits(hz) ? clampf(hz, 10.0f, maxHz) : appliedHz;
    const int slope = std::clamp(slopeIndex, 0, 3);
    const float lh = std::log2(h);
    targetSlope = slope;

    if (first) {
        first = false;
        enableGain.snap(enabled ? 1.0f : 0.0f);
        settledOff = true;   // forces the (re)start path below when enabled
        if (!enabled) { for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n); return; }
    }
    enableGain.setTarget(enabled ? 1.0f : 0.0f);
    if (!enabled && util::same(enableGain.value(), 0.0f) && !enableGain.isSmoothing()) {
        if (!settledOff) {
            settledOff = true;
            bank[0].reset();
            bank[1].reset();
            xfading = false;
        }
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    if (settledOff) {
        settledOff = false;
        logHz.snap(lh);
        appliedHz = h;
        cur = 0;
        bankSlope[0] = slope;
        bank[0].reset();
        bank[1].reset();
        designBank(0, slope, h);
        xfading = false;
        countdown = 0;
    } else {
        logHz.setTarget(lh);
    }

    float eg[kControlInterval], xf[kControlInterval], dry[kControlInterval], other[kControlInterval];
    int pos = 0;
    while (pos < n) {
        if (countdown <= 0) { controlTick(); countdown = kControlInterval; }
        const int seg = std::min(n - pos, countdown);
        const bool fullyOn = !enableGain.isSmoothing() && util::same(enableGain.value(), 1.0f);
        for (int i = 0; i < seg; ++i) {
            eg[i] = enableGain.next();
            xf[i] = xfading ? xfade.next() : 0.0f;
        }
        for (int c = 0; c < nch; ++c) {
            float* d = ch[c] + pos;
            util::cleanBuffer(d, seg);
            std::copy(d, d + seg, dry);
            bank[cur].process(c, d, seg);
            if (xfading) {
                std::copy(dry, dry + seg, other);
                bank[1 - cur].process(c, other, seg);
                for (int i = 0; i < seg; ++i) d[i] += xf[i] * (other[i] - d[i]);
            }
            if (!fullyOn)
                for (int i = 0; i < seg; ++i) d[i] = dry[i] + eg[i] * (d[i] - dry[i]);
        }
        countdown -= seg;
        pos += seg;
    }
}
}
