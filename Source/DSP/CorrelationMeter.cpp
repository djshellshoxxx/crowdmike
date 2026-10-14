#include "CorrelationMeter.h"
#include <cmath>

namespace crowdmike::dsp {

void CorrelationMeter::prepare(double sampleRate, float timeConstantMs) noexcept {
    coef = msToCoef(std::max(1.0f, timeConstantMs), sampleRate > 0.0 ? sampleRate : 48000.0);
    reset();
}

void CorrelationMeter::reset() noexcept {
    ll = rr = lr = 0.0;
    correlation.store(0.0f, std::memory_order_relaxed);
    width.store(0.0f, std::memory_order_relaxed);
    sideToMidDb.store(-60.0f, std::memory_order_relaxed);
}

void CorrelationMeter::process(const float* l, const float* r, int n) noexcept {
    if (l == nullptr || r == nullptr || n <= 0) return;
    const double c = coef, ic = 1.0 - coef;
    double a = ll, b = rr, x = lr;
    for (int i = 0; i < n; ++i) {
        const double sl = sanitize(l[i]), sr = sanitize(r[i]);
        a = c * a + ic * sl * sl;
        b = c * b + ic * sr * sr;
        x = c * x + ic * sl * sr;
    }
    constexpr double tiny = 1.0e-30;
    if (a < tiny) a = 0.0;
    if (b < tiny) b = 0.0;
    if (std::fabs(x) < tiny) x = 0.0;
    ll = a; rr = b; lr = x;

    constexpr double silence = 1.0e-10; // ~-100 dBFS power
    float corr = 0.0f, w = 0.0f, smDb = -60.0f;
    if (a > silence && b > silence) corr = static_cast<float>(clampf(static_cast<float>(x / std::sqrt(a * b)), -1.0f, 1.0f));
    const double m2 = 0.25 * (a + b + 2.0 * x), s2 = 0.25 * (a + b - 2.0 * x);
    const double mm = std::max(0.0, m2), ss = std::max(0.0, s2);
    if (mm + ss > silence) {
        w = static_cast<float>(ss / (mm + ss));
        smDb = clampf(static_cast<float>(10.0 * std::log10(std::max(ss, 1.0e-30) / std::max(mm, 1.0e-30))), -60.0f, 60.0f);
    }
    correlation.store(corr, std::memory_order_relaxed);
    width.store(w, std::memory_order_relaxed);
    sideToMidDb.store(smDb, std::memory_order_relaxed);
}

} // namespace crowdmike::dsp
