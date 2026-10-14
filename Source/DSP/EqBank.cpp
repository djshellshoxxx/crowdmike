#include "EqBank.h"
#include "DspUtil.h"
#include "../Core/ParamTables.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
using namespace crowdmike::params;

void EqBank::prepare(double sampleRate, int /*maxBlock*/, int channels, int maxNumBands) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    maxBands = std::max(0, maxNumBands);
    bands.assign(static_cast<size_t>(maxBands), Band {});
    filters.assign(static_cast<size_t>(maxBands) * static_cast<size_t>(numChannels), Biquad {});
    for (auto& b : bands) {
        b.wet.prepare(sr, 20.0f);
        b.logHz.prepare(sr, 50.0f);
        b.logQ.prepare(sr, 50.0f);
        b.gainDb.prepare(sr, 30.0f);
    }
    reset();
}

void EqBank::reset() noexcept {
    for (auto& f : filters) f.reset();
    for (auto& b : bands) {
        b.type = b.reqType = 0;
        b.reqOn = false;
        b.active = false;
        b.dirty = true;
        b.wet.snap(0.0f);
        b.logHz.snap(10.0f);
        b.logQ.snap(0.0f);
        b.gainDb.snap(0.0f);
    }
    first = true;
    bypassed = false;
    countdown = 0;
}

void EqBank::designBand(Band& b) noexcept {
    const float hz = std::exp2(b.logHz.value());
    const float q = std::exp2(b.logQ.value());
    const float g = b.gainDb.value();
    switch (b.type) {
        case EqNotch: b.coeffs = designNotch(sr, hz, q, -std::fabs(g)); break;
        case EqLowShelf: b.coeffs = designLowShelf(sr, hz, q, g); break;
        case EqHighShelf: b.coeffs = designHighShelf(sr, hz, q, g); break;
        default: b.coeffs = designBell(sr, hz, q, g); break;
    }
    b.dirty = false;
}

void EqBank::settleBand(int bi) noexcept {
    Band& B = bands[static_cast<size_t>(bi)];
    if (!util::same(B.wet.value(), 0.0f) || B.wet.isSmoothing()) return;
    B.type = B.reqType;
    B.logHz.snap(std::log2(B.reqHz));
    B.logQ.snap(std::log2(B.reqQ));
    B.gainDb.snap(B.reqGain);
    B.dirty = true;
    if (B.active) {
        for (int c = 0; c < numChannels; ++c) filters[static_cast<size_t>(bi * numChannels + c)].reset();
        B.active = false;
    }
    if (B.reqOn) {
        B.wet.setTarget(1.0f);
        B.active = true;
        designBand(B);
    }
}

void EqBank::process(float* const* ch, int nch, int n, const float* bandParams, int numBands) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    numBands = std::clamp(numBands, 0, maxBands);
    const float maxHz = static_cast<float>(std::min(0.45 * sr, 30000.0));

    bool anyWork = false;
    for (int bi = 0; bi < maxBands; ++bi) {
        Band& B = bands[static_cast<size_t>(bi)];
        B.reqOn = false;
        if (bandParams != nullptr && bi < numBands) {
            const float* bp = bandParams + bi * kEqBandStride;
            B.reqOn = util::readBool(bp, EqOn, false);
            B.reqType = util::readChoice(bp, EqType, 0, 0, 3);
            B.reqHz = util::readParam(bp, EqHz, 1000.0f, 10.0f, maxHz);
            B.reqGain = util::readParam(bp, EqGain, 0.0f, -30.0f, 30.0f);
            B.reqQ = util::readParam(bp, EqQ, 1.0f, 0.05f, 100.0f);
        }
        if (first) {
            B.wet.snap(0.0f);
            settleBand(bi);
            if (B.reqOn) B.wet.snap(1.0f);
        }
        if (B.active) {
            // a running band follows parameter changes (glides) and fades out when switched off or retyped
            B.logHz.setTarget(std::log2(B.reqHz));
            B.logQ.setTarget(std::log2(B.reqQ));
            B.gainDb.setTarget(B.reqGain);
            B.wet.setTarget((B.reqOn && B.reqType == B.type) ? 1.0f : 0.0f);
        }
        anyWork = anyWork || B.active || B.reqOn;
    }
    first = false;

    if (!anyWork) {
        bypassed = true;
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
        return;
    }
    if (bypassed) {   // leaving the idle state (only ever caused by a parameter change): restart the tick grid
        bypassed = false;
        countdown = 0;
    }

    float w[kControlInterval];
    int pos = 0;
    while (pos < n) {
        if (countdown <= 0) {
            for (int bi = 0; bi < maxBands; ++bi) {
                Band& B = bands[static_cast<size_t>(bi)];
                settleBand(bi);
                if (!B.active) continue;
                if (B.logHz.isSmoothing() || B.logQ.isSmoothing() || B.gainDb.isSmoothing()) {
                    B.logHz.skip(kControlInterval);
                    B.logQ.skip(kControlInterval);
                    B.gainDb.skip(kControlInterval);
                    B.dirty = true;
                }
                if (B.dirty) designBand(B);
            }
            countdown = kControlInterval;
        }
        const int seg = std::min(n - pos, countdown);
        for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c] + pos, seg);
        for (int bi = 0; bi < maxBands; ++bi) {
            Band& B = bands[static_cast<size_t>(bi)];
            if (!B.active) continue;
            const bool fullyOn = !B.wet.isSmoothing() && util::same(B.wet.value(), 1.0f);
            for (int i = 0; i < seg; ++i) w[i] = B.wet.next();
            for (int c = 0; c < nch; ++c) {
                Biquad& f = filters[static_cast<size_t>(bi * numChannels + c)];
                f.setCoeffs(B.coeffs);
                float* d = ch[c] + pos;
                if (fullyOn) {
                    f.process(d, seg);
                } else {
                    for (int i = 0; i < seg; ++i) {
                        const float y = f.process(d[i]);
                        d[i] += w[i] * (y - d[i]);
                    }
                }
            }
        }
        countdown -= seg;
        pos += seg;
    }
    for (int bi = 0; bi < maxBands; ++bi)
        if (bands[static_cast<size_t>(bi)].active)
            for (int c = 0; c < nch; ++c) filters[static_cast<size_t>(bi * numChannels + c)].flushDenormals();
}
}
