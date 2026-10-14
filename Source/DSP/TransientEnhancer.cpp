#include "TransientEnhancer.h"
#include "DspUtil.h"
#include "../Core/ParamTables.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
using namespace crowdmike::params;

void TransientEnhancer::prepare(double sampleRate, int /*maxBlock*/, int channels) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    cFastAtt = msToCoef(1.0f, sr);
    cSlowAtt = msToCoef(25.0f, sr);
    cRelA = msToCoef(80.0f, sr);
    cRelFast = msToCoef(40.0f, sr);
    cRelSlow = msToCoef(300.0f, sr);
    cPre = msToCoef(20.0f, sr);
    sOn.prepare(sr, 10.0f);
    sMix.prepare(sr, 20.0f);
    reset();
}

void TransientEnhancer::reset() noexcept {
    first = true;
    settledOff = true;
    fastA = slowA = fastR = slowR = pre = 0.0f;
    sOn.snap(0.0f);
    gainMeter.store(0.0f, std::memory_order_relaxed);
}

void TransientEnhancer::process(float* const* ch, int nch, int n, const float* p) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;

    const bool on = util::readBool(p, TransP::On, false);
    const float aGain = util::readParam(p, TransP::AttackGain, 0.0f, -24.0f, 24.0f);
    const float sGain = util::readParam(p, TransP::SustainGain, 0.0f, -24.0f, 24.0f);
    const float sens = util::readParam(p, TransP::Sens, 0.5f, 0.0f, 1.0f);
    const float mix = util::readParam(p, TransP::Mix, 1.0f, 0.0f, 1.0f);

    if (first) {
        first = false;
        sOn.snap(on ? 1.0f : 0.0f);
    }
    sOn.setTarget(on ? 1.0f : 0.0f);
    if (!on && util::same(sOn.value(), 0.0f) && !sOn.isSmoothing()) {
        if (!settledOff) {
            settledOff = true;
            fastA = slowA = fastR = slowR = pre = 0.0f;
        }
        gainMeter.store(0.0f, std::memory_order_relaxed);
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    if (settledOff) {
        settledOff = false;
        sMix.snap(mix);
    } else {
        sMix.setTarget(mix);
    }
    const float invRef = 1.0f / (9.0f - 7.0f * sens);
    constexpr float kFloor = 1.0e-5f;   // -100 dBFS
    float gDb = 0.0f;
    for (int i = 0; i < n; ++i) {
        float v = 0.0f;
        for (int c = 0; c < nch; ++c) {
            const float x = util::cleanSample(ch[c][i]);
            ch[c][i] = x;
            v = std::max(v, std::fabs(x));
        }
        pre = std::max(v, pre * cPre);   // ripple-free level: instant attack, 20 ms release
        v = pre + kFloor;
        fastA = v + (v > fastA ? cFastAtt : cRelA) * (fastA - v);
        slowA = v + (v > slowA ? cSlowAtt : cRelA) * (slowA - v);
        fastR = v + (v > fastR ? cFastAtt : cRelFast) * (fastR - v);
        slowR = v + (v > slowR ? cFastAtt : cRelSlow) * (slowR - v);
        const float attackDiff = 20.0f * std::log10(fastA / slowA);
        const float sustainDiff = 20.0f * std::log10(slowR / fastR);
        gDb = aGain * clampf(attackDiff * invRef, 0.0f, 1.0f) + sGain * clampf(sustainDiff * invRef, 0.0f, 1.0f);
        gDb = clampf(gDb, -18.0f, 18.0f);
        const float g = dbToGain(gDb);
        const float w = sOn.next(), m = sMix.next();
        for (int c = 0; c < nch; ++c) {
            const float x = ch[c][i];
            const float y = x + m * (x * g - x);
            ch[c][i] = x + w * (y - x);
        }
    }
    pre = util::flushTiny(pre);
    gainMeter.store(gDb, std::memory_order_relaxed);
}
}
