#include "Biquad.h"
#include "DspUtil.h"
#include <cmath>
#include <complex>

namespace crowdmike::dsp {
namespace {
constexpr double kPiD = 3.14159265358979323846;

struct Prep { double w0, cw, sw, alpha, oneMinusCos; };

Prep prep(double sr, float hz, float q) noexcept {
    if (!(sr > 0.0) || !std::isfinite(sr)) sr = 48000.0;
    double f = std::isfinite(hz) ? static_cast<double>(hz) : 1000.0;
    f = std::clamp(f, 1.0, 0.49 * sr);
    double qq = std::isfinite(q) ? static_cast<double>(q) : 0.7071;
    qq = std::clamp(qq, 0.01, 1000.0);
    Prep p {};
    p.w0 = 2.0 * kPiD * f / sr;
    p.cw = std::cos(p.w0);
    p.sw = std::sin(p.w0);
    p.alpha = p.sw / (2.0 * qq);
    const double h = std::sin(0.5 * p.w0);
    p.oneMinusCos = 2.0 * h * h;   // accurate 1 - cos(w0) for tiny w0
    return p;
}

BiquadCoeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
    const double inv = 1.0 / a0;
    BiquadCoeffs c;
    c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv; c.a1 = a1 * inv; c.a2 = a2 * inv;
    return c;
}

double safeDb(float db, double lo, double hi) noexcept {
    return std::isfinite(db) ? std::clamp(static_cast<double>(db), lo, hi) : 0.0;
}

std::complex<double> response(const BiquadCoeffs& c, double sr, double hz) noexcept {
    const double w = 2.0 * kPiD * hz / sr;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}
}

BiquadCoeffs designLowpass(double sr, float hz, float q) noexcept {
    const auto p = prep(sr, hz, q);
    const double b = 0.5 * p.oneMinusCos;
    return normalise(b, p.oneMinusCos, b, 1.0 + p.alpha, -2.0 * p.cw, 1.0 - p.alpha);
}

BiquadCoeffs designHighpass(double sr, float hz, float q) noexcept {
    const auto p = prep(sr, hz, q);
    const double onePlusCos = 2.0 - p.oneMinusCos;
    const double b = 0.5 * onePlusCos;
    return normalise(b, -onePlusCos, b, 1.0 + p.alpha, -2.0 * p.cw, 1.0 - p.alpha);
}

BiquadCoeffs designBandpass(double sr, float hz, float q) noexcept {
    const auto p = prep(sr, hz, q);
    return normalise(p.alpha, 0.0, -p.alpha, 1.0 + p.alpha, -2.0 * p.cw, 1.0 - p.alpha);
}

BiquadCoeffs designBell(double sr, float hz, float q, float gainDb) noexcept {
    const auto p = prep(sr, hz, q);
    const double A = std::pow(10.0, safeDb(gainDb, -60.0, 60.0) / 40.0);
    return normalise(1.0 + p.alpha * A, -2.0 * p.cw, 1.0 - p.alpha * A, 1.0 + p.alpha / A, -2.0 * p.cw,
                     1.0 - p.alpha / A);
}

BiquadCoeffs designNotch(double sr, float hz, float q, float depthDb) noexcept {
    const auto p = prep(sr, hz, q);
    const double d = safeDb(depthDb, -200.0, 0.0);
    const double g = d <= -199.0 ? 0.0 : std::pow(10.0, d / 20.0);
    return normalise(1.0 + p.alpha * g, -2.0 * p.cw, 1.0 - p.alpha * g, 1.0 + p.alpha, -2.0 * p.cw, 1.0 - p.alpha);
}

BiquadCoeffs designLowShelf(double sr, float hz, float q, float gainDb) noexcept {
    const auto p = prep(sr, hz, q);
    const double A = std::pow(10.0, safeDb(gainDb, -60.0, 60.0) / 40.0);
    const double s = 2.0 * std::sqrt(A) * p.alpha;
    const double ap1 = A + 1.0, am1 = A - 1.0;
    return normalise(A * (ap1 - am1 * p.cw + s), 2.0 * A * (am1 - ap1 * p.cw), A * (ap1 - am1 * p.cw - s),
                     ap1 + am1 * p.cw + s, -2.0 * (am1 + ap1 * p.cw), ap1 + am1 * p.cw - s);
}

BiquadCoeffs designHighShelf(double sr, float hz, float q, float gainDb) noexcept {
    const auto p = prep(sr, hz, q);
    const double A = std::pow(10.0, safeDb(gainDb, -60.0, 60.0) / 40.0);
    const double s = 2.0 * std::sqrt(A) * p.alpha;
    const double ap1 = A + 1.0, am1 = A - 1.0;
    return normalise(A * (ap1 + am1 * p.cw + s), -2.0 * A * (am1 + ap1 * p.cw), A * (ap1 + am1 * p.cw - s),
                     ap1 - am1 * p.cw + s, 2.0 * (am1 - ap1 * p.cw), ap1 - am1 * p.cw - s);
}

BiquadCoeffs designAllpass(double sr, float hz, float q) noexcept {
    const auto p = prep(sr, hz, q);
    return normalise(1.0 - p.alpha, -2.0 * p.cw, 1.0 + p.alpha, 1.0 + p.alpha, -2.0 * p.cw, 1.0 - p.alpha);
}

double biquadMagnitudeDb(const BiquadCoeffs& c, double sr, double hz) noexcept {
    return 20.0 * std::log10(std::max(std::abs(response(c, sr, hz)), 1.0e-15));
}

double biquadPhase(const BiquadCoeffs& c, double sr, double hz) noexcept { return std::arg(response(c, sr, hz)); }

void Biquad::process(float* data, int n) noexcept {
    for (int i = 0; i < n; ++i) data[i] = process(data[i]);
}

void Biquad::flushDenormals() noexcept {
    s1 = util::flushTiny(s1);
    s2 = util::flushTiny(s2);
}

void BiquadCascade::prepare(int numChannels) {
    channels = std::max(0, numChannels);
    state.assign(static_cast<size_t>(channels) * kMaxSections * 2, 0.0);
}

void BiquadCascade::reset() noexcept { std::fill(state.begin(), state.end(), 0.0); }

void BiquadCascade::setNumSections(int n) noexcept { sections = std::clamp(n, 0, kMaxSections); }

void BiquadCascade::setSection(int i, const BiquadCoeffs& c) noexcept {
    if (i >= 0 && i < kMaxSections) coeffs[i] = c;
}

void BiquadCascade::process(int channel, float* data, int n) noexcept {
    if (channel < 0 || channel >= channels) return;
    double* st = state.data() + static_cast<size_t>(channel) * kMaxSections * 2;
    for (int s = 0; s < sections; ++s) {
        const BiquadCoeffs k = coeffs[s];
        double s1 = st[2 * s], s2 = st[2 * s + 1];
        for (int i = 0; i < n; ++i) {
            const double x = data[i];
            const double y = k.b0 * x + s1;
            s1 = k.b1 * x - k.a1 * y + s2;
            s2 = k.b2 * x - k.a2 * y;
            data[i] = static_cast<float>(y);
        }
        st[2 * s] = util::flushTiny(s1);
        st[2 * s + 1] = util::flushTiny(s2);
    }
}
}
