#include "LoudnessMeter.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
float powerToDb(double meanSquare, double offset) noexcept {
    if (!(meanSquare > 1.0e-12)) return LoudnessMeter::kFloorDb;
    return std::max(LoudnessMeter::kFloorDb, static_cast<float>(offset + 10.0 * std::log10(meanSquare)));
}
}

void LoudnessMeter::design() noexcept {
    {   // stage 1: high shelf (+4 dB at ~1.68 kHz)
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / sr);
        const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& f : shelf) {
            f.b0 = (Vh + Vb * K / Q + K * K) / a0;
            f.b1 = 2.0 * (K * K - Vh) / a0;
            f.b2 = (Vh - Vb * K / Q + K * K) / a0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    {   // stage 2: RLB high-pass (~38 Hz)
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        for (auto& f : rlb) {
            f.b0 = 1.0; f.b1 = -2.0; f.b2 = 1.0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
}

void LoudnessMeter::prepare(double sampleRate) noexcept {
    sr = sampleRate >= 8000.0 ? sampleRate : 48000.0;
    blockLen = std::max(1, static_cast<int>(std::lround(sr * 0.1)));
    design();
    reset();
}

void LoudnessMeter::reset() noexcept {
    for (auto& f : shelf) f.clear();
    for (auto& f : rlb) f.clear();
    fill = 0;
    sumK = sumRaw = 0.0;
    ringK.fill(0.0);
    ringRaw.fill(0.0);
    ringPos = ringCount = 0;
    shortLufs.store(kFloorDb, std::memory_order_relaxed);
    momentaryDb.store(kFloorDb, std::memory_order_relaxed);
}

void LoudnessMeter::closeBlock() noexcept {
    ringK[static_cast<size_t>(ringPos)] = sumK / blockLen;
    ringRaw[static_cast<size_t>(ringPos)] = sumRaw / blockLen;
    ringPos = (ringPos + 1) % kShortBlocks;
    ringCount = std::min(ringCount + 1, kShortBlocks);
    sumK = sumRaw = 0.0;
    fill = 0;

    double k = 0.0;
    for (int i = 0; i < ringCount; ++i) k += ringK[static_cast<size_t>(i)];   // order is irrelevant for a mean
    shortLufs.store(powerToDb(k / ringCount, -0.691), std::memory_order_relaxed);
    const int m = std::min(ringCount, kMomentaryBlocks);
    double raw = 0.0;
    for (int i = 1; i <= m; ++i) raw += ringRaw[static_cast<size_t>((ringPos - i + kShortBlocks) % kShortBlocks)];
    momentaryDb.store(powerToDb(raw / m, 0.0), std::memory_order_relaxed);
}

void LoudnessMeter::push(const float* left, const float* right, int n) noexcept {
    if (left == nullptr || n <= 0) return;
    if (right == nullptr) right = left;
    for (int i = 0; i < n; ++i) {
        const double l = std::isfinite(left[i]) ? static_cast<double>(left[i]) : 0.0;
        const double r = std::isfinite(right[i]) ? static_cast<double>(right[i]) : 0.0;
        const double kl = rlb[0].process(shelf[0].process(l));
        const double kr = rlb[1].process(shelf[1].process(r));
        sumK += 0.5 * (kl * kl + kr * kr);
        sumRaw += 0.5 * (l * l + r * r);
        if (++fill >= blockLen) closeBlock();
    }
}
}
