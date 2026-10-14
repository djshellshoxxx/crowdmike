#include "Compressor.h"
#include "DspUtil.h"
#include "../Core/ParamTables.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
using namespace crowdmike::params;

float Compressor::computeGainDb(float levelDb, float thrDb, float ratio, float kneeDb) noexcept {
    const float slope = 1.0f / std::max(ratio, 1.0f) - 1.0f;
    const float over = levelDb - thrDb;
    if (kneeDb > 0.0f && 2.0f * std::fabs(over) <= kneeDb) {
        const float t = over + 0.5f * kneeDb;
        return slope * t * t / (2.0f * kneeDb);
    }
    return over <= 0.0f ? 0.0f : slope * over;
}

void Compressor::prepare(double sampleRate, int /*maxBlock*/, int channels) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    scHpf.assign(static_cast<size_t>(numChannels), Biquad {});
    sThr.prepare(sr, 20.0f);
    sRatio.prepare(sr, 20.0f);
    sKnee.prepare(sr, 20.0f);
    sMakeup.prepare(sr, 20.0f);
    sMix.prepare(sr, 20.0f);
    sOn.prepare(sr, 10.0f);
    reset();
}

void Compressor::reset() noexcept {
    for (auto& f : scHpf) f.reset();
    first = true;
    settledOff = true;
    env = 0.0f;
    appliedScHz = attackMs = releaseMs = -1.0f;
    sOn.snap(0.0f);
    grMeter.store(0.0f, std::memory_order_relaxed);
}

void Compressor::process(float* const* ch, int nch, int n, const float* p) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;

    const bool on = util::readBool(p, CompP::On, false);
    const float thr = util::readParam(p, CompP::Thr, -18.0f, -80.0f, 0.0f);
    const float ratio = util::readParam(p, CompP::Ratio, 2.5f, 1.0f, 100.0f);
    const float atk = util::readParam(p, CompP::Attack, 10.0f, 0.01f, 2000.0f);
    const float rel = util::readParam(p, CompP::Release, 120.0f, 1.0f, 10000.0f);
    const float knee = util::readParam(p, CompP::Knee, 6.0f, 0.0f, 48.0f);
    const bool autoMakeup = util::readBool(p, CompP::AutoMakeup, false);
    const float makeup = autoMakeup ? -thr * (1.0f - 1.0f / ratio) * 0.5f
                                    : util::readParam(p, CompP::Makeup, 0.0f, -24.0f, 48.0f);
    const float scHz = util::readParam(p, CompP::ScHpf, 80.0f, 10.0f, static_cast<float>(0.45 * sr));
    const float mix = util::readParam(p, CompP::Mix, 1.0f, 0.0f, 1.0f);

    if (first) {
        first = false;
        sOn.snap(on ? 1.0f : 0.0f);
    }
    sOn.setTarget(on ? 1.0f : 0.0f);
    if (!on && util::same(sOn.value(), 0.0f) && !sOn.isSmoothing()) {
        if (!settledOff) {
            settledOff = true;
            for (auto& f : scHpf) f.reset();
            env = 0.0f;
        }
        grMeter.store(0.0f, std::memory_order_relaxed);
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    if (settledOff) {   // (re)start: snap parameter smoothers, detector starts from rest
        settledOff = false;
        sThr.snap(thr); sRatio.snap(ratio); sKnee.snap(knee); sMakeup.snap(makeup); sMix.snap(mix);
        env = 0.0f;
    } else {
        sThr.setTarget(thr); sRatio.setTarget(ratio); sKnee.setTarget(knee); sMakeup.setTarget(makeup);
        sMix.setTarget(mix);
    }
    if (!util::same(scHz, appliedScHz)) {
        appliedScHz = scHz;
        const auto c = designHighpass(sr, scHz, 0.70710678f);
        for (auto& f : scHpf) f.setCoeffs(c);
    }
    if (!util::same(atk, attackMs)) { attackMs = atk; attCoef = msToCoef(atk, sr); }
    if (!util::same(rel, releaseMs)) { releaseMs = rel; relCoef = msToCoef(rel, sr); }

    float deepest = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float t = sThr.next(), r = sRatio.next(), k = sKnee.next(), mk = sMakeup.next(), mx = sMix.next();
        const float w = sOn.next();
        float peak = 0.0f;
        for (int c = 0; c < nch; ++c) {
            const float x = util::cleanSample(ch[c][i]);
            ch[c][i] = x;
            peak = std::max(peak, std::fabs(scHpf[static_cast<size_t>(c)].process(x)));
        }
        const float target = computeGainDb(gainToDb(peak), t, r, k);
        env = target + (target < env ? attCoef : relCoef) * (env - target);
        deepest = std::min(deepest, env);
        const float g = dbToGain(env + mk);
        for (int c = 0; c < nch; ++c) {
            const float x = ch[c][i];
            const float y = x + mx * (x * g - x);
            ch[c][i] = x + w * (y - x);
        }
    }
    for (auto& f : scHpf) f.flushDenormals();
    grMeter.store(deepest, std::memory_order_relaxed);
}
}
