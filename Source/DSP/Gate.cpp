#include "Gate.h"
#include "DspUtil.h"
#include "../Core/ParamTables.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
using namespace crowdmike::params;

void Gate::prepare(double sampleRate, int /*maxBlock*/, int channels) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    scHp.assign(static_cast<size_t>(numChannels), Biquad {});
    scLp.assign(static_cast<size_t>(numChannels), Biquad {});
    detCoef = msToCoef(30.0f, sr);
    sOn.prepare(sr, 10.0f);
    reset();
}

void Gate::reset() noexcept {
    for (auto& f : scHp) f.reset();
    for (auto& f : scLp) f.reset();
    first = true;
    settledOff = true;
    open = false;
    det = 0.0f;
    gainDb = 0.0f;
    holdLeft = 0;
    appliedHp = appliedLp = attackMs = releaseMs = -1.0f;
    sOn.snap(0.0f);
    grMeter.store(0.0f, std::memory_order_relaxed);
    openMeter.store(false, std::memory_order_relaxed);
}

void Gate::process(float* const* ch, int nch, int n, const float* p) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;

    const float nyq = static_cast<float>(0.45 * sr);
    const bool on = util::readBool(p, GateP::On, false);
    const float thr = util::readParam(p, GateP::Thr, -60.0f, -120.0f, 0.0f);
    const float range = util::readParam(p, GateP::Range, -24.0f, -120.0f, 0.0f);
    const float ratio = util::readParam(p, GateP::Ratio, 2.0f, 1.0f, 100.0f);
    const float atk = util::readParam(p, GateP::Attack, 5.0f, 0.01f, 1000.0f);
    const float holdMs = util::readParam(p, GateP::Hold, 50.0f, 0.0f, 10000.0f);
    const float rel = util::readParam(p, GateP::Release, 200.0f, 1.0f, 10000.0f);
    const float hyst = util::readParam(p, GateP::Hyst, 3.0f, 0.0f, 48.0f);
    const float hpHz = util::readParam(p, GateP::ScHpf, 100.0f, 10.0f, nyq);
    const float lpHz = util::readParam(p, GateP::ScLpf, 12000.0f, 10.0f, nyq);
    const int holdSamples = static_cast<int>(holdMs * 0.001 * sr);

    if (first) {
        first = false;
        sOn.snap(on ? 1.0f : 0.0f);
    }
    sOn.setTarget(on ? 1.0f : 0.0f);
    if (!on && util::same(sOn.value(), 0.0f) && !sOn.isSmoothing()) {
        if (!settledOff) {
            settledOff = true;
            for (auto& f : scHp) f.reset();
            for (auto& f : scLp) f.reset();
            det = 0.0f;
            gainDb = 0.0f;
            open = false;
            holdLeft = 0;
        }
        grMeter.store(0.0f, std::memory_order_relaxed);
        openMeter.store(true, std::memory_order_relaxed);
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    settledOff = false;
    if (!util::same(hpHz, appliedHp)) {
        appliedHp = hpHz;
        const auto c = designHighpass(sr, hpHz, 0.70710678f);
        for (auto& f : scHp) f.setCoeffs(c);
    }
    if (!util::same(lpHz, appliedLp)) {
        appliedLp = lpHz;
        const auto c = designLowpass(sr, lpHz, 0.70710678f);
        for (auto& f : scLp) f.setCoeffs(c);
    }
    if (!util::same(atk, attackMs)) { attackMs = atk; attCoef = msToCoef(atk, sr); }
    if (!util::same(rel, releaseMs)) { releaseMs = rel; relCoef = msToCoef(rel, sr); }
    const float closeThr = thr - hyst;
    const float slope = ratio - 1.0f;

    for (int i = 0; i < n; ++i) {
        float peak = 0.0f;
        for (int c = 0; c < nch; ++c) {
            const float x = util::cleanSample(ch[c][i]);
            ch[c][i] = x;
            const auto ci = static_cast<size_t>(c);
            peak = std::max(peak, std::fabs(scLp[ci].process(scHp[ci].process(x))));
        }
        det = std::max(peak, det * detCoef);
        const float lvl = gainToDb(det);
        if (lvl >= thr) {
            open = true;
            holdLeft = holdSamples;
        } else if (open) {
            if (lvl >= closeThr) holdLeft = holdSamples;
            else if (holdLeft > 0) --holdLeft;
            else open = false;
        }
        const float target = open ? 0.0f : std::min(0.0f, std::max(range, (lvl - thr) * slope));
        gainDb = target + (target > gainDb ? attCoef : relCoef) * (gainDb - target);
        const float g = dbToGain(gainDb);
        const float w = sOn.next();
        for (int c = 0; c < nch; ++c) {
            const float x = ch[c][i];
            ch[c][i] = x + w * (x * g - x);
        }
    }
    for (auto& f : scHp) f.flushDenormals();
    for (auto& f : scLp) f.flushDenormals();
    det = util::flushTiny(det);
    grMeter.store(gainDb, std::memory_order_relaxed);
    openMeter.store(open, std::memory_order_relaxed);
}
}
