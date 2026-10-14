#include "AllpassPhase.h"
#include "DspUtil.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
void AllpassPhase::prepare(double sampleRate, int /*maxBlock*/, int channels) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    for (auto& b : bank) b.prepare(numChannels);
    logHz.prepare(sr, 50.0f);
    xfade.prepare(sr, 30.0f);
    reset();
}

void AllpassPhase::reset() noexcept {
    for (auto& b : bank) { b.reset(); b.setNumSections(0); }
    cur = 0;
    countdown = 0;
    xfading = false;
    first = true;
    logHz.snap(std::log2(500.0f));
    xfade.snap(0.0f);
}

void AllpassPhase::designBank(int b, float hz) noexcept {
    const auto c = designAllpass(sr, hz, 0.70710678f);
    for (int k = 0; k < bank[b].getNumSections(); ++k) bank[b].setSection(k, c);
}

void AllpassPhase::controlTick() noexcept {
    if (logHz.isSmoothing()) {
        const float h = std::exp2(logHz.skip(kControlInterval));
        if (!util::same(h, appliedHz)) {
            appliedHz = h;
            designBank(0, h);
            designBank(1, h);
        }
    }
    if (xfading && !xfade.isSmoothing()) {
        bank[cur].reset();
        cur = 1 - cur;
        xfading = false;
    }
    if (!xfading && targetStages != bank[cur].getNumSections()) {
        const int o = 1 - cur;
        bank[o].reset();
        bank[o].setNumSections(targetStages);
        designBank(o, appliedHz);
        xfade.snap(0.0f);
        xfade.setTarget(1.0f);
        xfading = true;
    }
}

void AllpassPhase::process(float* const* ch, int nch, int n, int stages, float hz) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    const float maxHz = static_cast<float>(std::min(0.45 * sr, 30000.0));
    const float h = util::isFiniteBits(hz) ? clampf(hz, 10.0f, maxHz) : appliedHz;
    targetStages = std::clamp(stages, 0, kMaxStages);
    if (first) {
        first = false;
        logHz.snap(std::log2(h));
        appliedHz = h;
        bank[0].setNumSections(targetStages);
        designBank(0, h);
        cur = 0;
    }
    const bool settledBypass = !xfading && bank[cur].getNumSections() == 0 && targetStages == 0;
    if (settledBypass) {
        logHz.snap(std::log2(h));
        appliedHz = h;
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    logHz.setTarget(std::log2(h));

    float xf[kControlInterval], dry[kControlInterval], other[kControlInterval];
    int pos = 0;
    while (pos < n) {
        if (countdown <= 0) { controlTick(); countdown = kControlInterval; }
        const int seg = std::min(n - pos, countdown);
        if (xfading)
            for (int i = 0; i < seg; ++i) xf[i] = xfade.next();
        for (int c = 0; c < nch; ++c) {
            float* d = ch[c] + pos;
            util::cleanBuffer(d, seg);
            if (xfading) std::copy(d, d + seg, dry);
            bank[cur].process(c, d, seg);
            if (xfading) {
                std::copy(dry, dry + seg, other);
                bank[1 - cur].process(c, other, seg);
                for (int i = 0; i < seg; ++i) d[i] += xf[i] * (other[i] - d[i]);
            }
        }
        countdown -= seg;
        pos += seg;
    }
}
}
