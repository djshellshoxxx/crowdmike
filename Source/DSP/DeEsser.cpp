#include "DeEsser.h"
#include "DspUtil.h"
#include "../Core/ParamTables.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
using namespace crowdmike::params;
namespace { constexpr float kBandQ = 1.4f; constexpr int kCtrl = 16; }

void DeEsser::prepare(double sampleRate, int /*maxBlock*/, int channels) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    bp.assign(static_cast<size_t>(numChannels), Biquad {});
    sOn.prepare(sr, 10.0f);
    sListen.prepare(sr, 10.0f);
    sMode.prepare(sr, 10.0f);
    logHz.prepare(sr, 50.0f);
    reset();
}

void DeEsser::reset() noexcept {
    for (auto& f : bp) f.reset();
    first = true;
    settledOff = true;
    countdown = 0;
    env = 0.0f;
    attackMs = releaseMs = -1.0f;
    sOn.snap(0.0f);
    grMeter.store(0.0f, std::memory_order_relaxed);
}

void DeEsser::process(float* const* ch, int nch, int n, const float* p) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;

    const bool on = util::readBool(p, DeEssP::On, false);
    const float hz = util::readParam(p, DeEssP::Freq, 6500.0f, 500.0f, static_cast<float>(std::min(0.45 * sr, 20000.0)));
    const float thr = util::readParam(p, DeEssP::Thr, -24.0f, -80.0f, 0.0f);
    const float ratio = util::readParam(p, DeEssP::Ratio, 3.0f, 1.0f, 100.0f);
    const float range = util::readParam(p, DeEssP::Range, 12.0f, 0.0f, 48.0f);
    const float atk = util::readParam(p, DeEssP::Attack, 1.0f, 0.01f, 1000.0f);
    const float rel = util::readParam(p, DeEssP::Release, 60.0f, 1.0f, 5000.0f);
    const bool listen = util::readBool(p, DeEssP::Listen, false);
    const int mode = util::readChoice(p, DeEssP::Mode, 0, 0, 1);

    if (first) {
        first = false;
        sOn.snap(on ? 1.0f : 0.0f);
    }
    sOn.setTarget(on ? 1.0f : 0.0f);
    if (!on && util::same(sOn.value(), 0.0f) && !sOn.isSmoothing()) {
        if (!settledOff) {
            settledOff = true;
            for (auto& f : bp) f.reset();
            env = 0.0f;
        }
        grMeter.store(0.0f, std::memory_order_relaxed);
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    if (settledOff) {
        settledOff = false;
        logHz.snap(std::log2(hz));
        sListen.snap(listen ? 1.0f : 0.0f);
        sMode.snap(static_cast<float>(mode));
        const auto c = designBandpass(sr, hz, kBandQ);
        for (auto& f : bp) f.setCoeffs(c);
        env = 0.0f;
        countdown = kCtrl;
    } else {
        logHz.setTarget(std::log2(hz));
        sListen.setTarget(listen ? 1.0f : 0.0f);
        sMode.setTarget(static_cast<float>(mode));
    }
    if (!util::same(atk, attackMs)) { attackMs = atk; attCoef = msToCoef(atk, sr); }
    if (!util::same(rel, releaseMs)) { releaseMs = rel; relCoef = msToCoef(rel, sr); }
    const float slope = 1.0f - 1.0f / ratio;

    float deepest = 0.0f;
    float band[64];   // per-channel band sample of the current frame (channels are capped below)
    const int frameCh = std::min(nch, 64);
    for (int i = 0; i < n; ++i) {
        if (--countdown <= 0) {
            countdown = kCtrl;
            if (logHz.isSmoothing()) {
                const auto c = designBandpass(sr, std::exp2(logHz.skip(kCtrl)), kBandQ);
                for (auto& f : bp) f.setCoeffs(c);
            }
        }
        float peak = 0.0f;
        for (int c = 0; c < frameCh; ++c) {
            const float x = util::cleanSample(ch[c][i]);
            ch[c][i] = x;
            band[c] = bp[static_cast<size_t>(c)].process(x);
            peak = std::max(peak, std::fabs(band[c]));
        }
        const float over = gainToDb(peak) - thr;
        const float target = over > 0.0f ? -std::min(range, over * slope) : 0.0f;
        env = target + (target < env ? attCoef : relCoef) * (env - target);
        deepest = std::min(deepest, env);
        const float g = dbToGain(env);
        const float w = sOn.next(), l = sListen.next(), m = sMode.next();
        for (int c = 0; c < frameCh; ++c) {
            const float x = ch[c][i];
            const float split = x - (1.0f - g) * band[c];
            const float wide = x * g;
            float y = split + m * (wide - split);
            y += l * (band[c] - y);
            ch[c][i] = x + w * (y - x);
        }
    }
    for (auto& f : bp) f.flushDenormals();
    grMeter.store(deepest, std::memory_order_relaxed);
}
}
