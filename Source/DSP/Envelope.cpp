#include "Envelope.h"
#include "DspUtil.h"

namespace crowdmike::dsp {
float EnvelopeFollower::process(float x) noexcept {
    x = util::cleanSample(x);
    const float v = mode == Mode::Rms ? x * x : std::fabs(x);
    const float c = v > state ? att : rel;
    state = v + c * (state - v);
    state = util::flushTiny(state);
    return getValue();
}

float EnvelopeFollower::processBlock(const float* x, int n) noexcept {
    for (int i = 0; i < n; ++i) process(x[i]);
    return getValue();
}

void DensityFollower::prepare(double sampleRate, int /*maxBlock*/) noexcept {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    rmsCoef = msToCoef(300.0f, sr);
    attCoef16 = std::pow(msToCoef(1500.0f, sr), 16.0f);
    relCoef16 = std::pow(msToCoef(4000.0f, sr), 16.0f);
    reset();
}

void DensityFollower::reset() noexcept {
    power = 0.0;
    tick = 0;
    lastDb = -120.0f;
    value = 0.0f;
    density.store(0.0f, std::memory_order_relaxed);
    levelDb.store(-120.0f, std::memory_order_relaxed);
}

void DensityFollower::process(const float* const* ch, int nch, int n) noexcept {
    if (ch == nullptr || nch <= 0 || n <= 0) return;
    const double invCh = 1.0 / nch;
    const double rc = rmsCoef;
    float db = -120.0f;
    for (int i = 0; i < n; ++i) {
        double p = 0.0;
        for (int c = 0; c < nch; ++c) {
            const double x = util::cleanSample(ch[c][i]);
            p += x * x;
        }
        p *= invCh;
        power = p + rc * (power - p);
        // The slow stages run on a decimated control rate (every 16 samples) to keep the per-sample cost minimal;
        // the 1.5 s / 4 s time constants make the decimation inaudible and irrelevant.
        if (++tick >= 16) {
            tick = 0;
            lastDb = 10.0f * std::log10(static_cast<float>(std::max(power, 1.0e-12)));
            float norm = clampf((lastDb - floorDb) / (ceilDb - floorDb), 0.0f, 1.0f);
            norm = norm * norm * (3.0f - 2.0f * norm);
            value = norm + (norm > value ? attCoef16 : relCoef16) * (value - norm);
        }
    }
    db = lastDb;
    if (power < 1.0e-30) power = 0.0;
    density.store(clampf(value, 0.0f, 1.0f), std::memory_order_relaxed);
    levelDb.store(db, std::memory_order_relaxed);
}
}
