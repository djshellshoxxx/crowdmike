#include "SpectrumAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::dsp {

void SpectrumAnalyzer::prepare(double sr, float releaseMs) {
    std::scoped_lock lock(workMutex);
    sampleRate = sr > 0.0 ? sr : 48000.0;
    const double hopSec = kHop / sampleRate;
    releaseCoefDb = static_cast<float>(std::exp(-hopSec / (std::max(1.0f, releaseMs) * 0.001)));
    fifo.prepare(static_cast<int>(std::ceil(sampleRate * 0.5)) + kFftSize);
    fft = std::make_unique<juce::dsp::FFT>(kFftOrder);
    detail::makeHann(window, kFftSize);
    double wsum = 0.0;
    for (float w : window) wsum += w;
    ampScale = static_cast<float>(2.0 / wsum);
    accum.assign(kFftSize, 0.0f);
    fftBuf.assign(2 * kFftSize, 0.0f);
    drainBuf.assign(4096, 0.0f);
    workDb.assign(static_cast<size_t>(getNumFftBins()), kFloorDb);
    accumFill = 0;
    std::scoped_lock s(snapMutex);
    snapDb.assign(static_cast<size_t>(getNumFftBins()), kFloorDb);
}

void SpectrumAnalyzer::reset() {
    std::scoped_lock lock(workMutex);
    fifo.clear();
    std::fill(accum.begin(), accum.end(), 0.0f);
    std::fill(workDb.begin(), workDb.end(), kFloorDb);
    accumFill = 0;
    std::scoped_lock s(snapMutex);
    std::fill(snapDb.begin(), snapDb.end(), kFloorDb);
}

void SpectrumAnalyzer::pushSamples(const float* mono, int n) noexcept { fifo.push(mono, n); }

void SpectrumAnalyzer::analyse() {
    std::scoped_lock lock(workMutex);
    if (fft == nullptr) return;
    int remaining = fifo.getNumReady();
    const int keep = kFftSize + kMaxFramesPerAnalyse * kHop; // display only needs the newest frames
    if (remaining > keep) {
        fifo.skip(remaining - keep);
        remaining = keep;
        accumFill = 0;
    }
    bool anyFrame = false;
    while (remaining > 0) {
        const int n = fifo.pop(drainBuf.data(), std::min(remaining, static_cast<int>(drainBuf.size())));
        if (n <= 0) break;
        remaining -= n;
        int i = 0;
        while (i < n) {
            const int take = std::min(n - i, kFftSize - accumFill);
            std::copy(drainBuf.begin() + i, drainBuf.begin() + i + take, accum.begin() + accumFill);
            accumFill += take;
            i += take;
            if (accumFill == kFftSize) {
                processFrame();
                anyFrame = true;
                std::copy(accum.begin() + kHop, accum.end(), accum.begin());
                accumFill = kFftSize - kHop;
            }
        }
    }
    if (anyFrame) {
        std::scoped_lock s(snapMutex);
        std::copy(workDb.begin(), workDb.end(), snapDb.begin());
    }
}

void SpectrumAnalyzer::processFrame() {
    for (int i = 0; i < kFftSize; ++i) fftBuf[static_cast<size_t>(i)] = accum[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
    std::fill(fftBuf.begin() + kFftSize, fftBuf.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform(fftBuf.data(), true);
    for (int k = 0; k < getNumFftBins(); ++k) {
        const float a = fftBuf[static_cast<size_t>(k)] * ampScale;
        const float db = (std::isfinite(a) && a > 1.0e-6f) ? std::max(kFloorDb, 20.0f * std::log10(a)) : kFloorDb;
        float& s = workDb[static_cast<size_t>(k)];
        s = db >= s ? db : releaseCoefDb * s + (1.0f - releaseCoefDb) * db;
    }
}

void SpectrumAnalyzer::getSpectrumDb(float* out, int numBins, float minHz, float maxHz) const {
    if (out == nullptr || numBins <= 0) return;
    minHz = std::max(1.0f, minHz);
    maxHz = std::max(minHz * 1.001f, maxHz);
    std::scoped_lock s(snapMutex);
    detail::mapToLogBins(snapDb.empty() ? nullptr : snapDb.data(), static_cast<int>(snapDb.size()),
                         sampleRate / kFftSize, out, numBins, minHz, maxHz, kFloorDb);
}

float SpectrumAnalyzer::getPeakFrequencyHz() const {
    std::scoped_lock s(snapMutex);
    const int n = static_cast<int>(snapDb.size());
    if (n < 4) return 0.0f;
    const double binHz = sampleRate / kFftSize;
    int best = -1;
    float bestDb = kFloorDb + 1.0f;
    for (int k = std::max(1, static_cast<int>(std::ceil(20.0 / binHz))); k < n - 1; ++k)
        if (snapDb[static_cast<size_t>(k)] > bestDb) { bestDb = snapDb[static_cast<size_t>(k)]; best = k; }
    if (best < 0) return 0.0f;
    const float off = detail::parabolicOffset(snapDb[static_cast<size_t>(best - 1)], snapDb[static_cast<size_t>(best)],
                                              snapDb[static_cast<size_t>(best + 1)]);
    return static_cast<float>((static_cast<double>(best) + off) * binHz);
}

} // namespace crowdmike::dsp
