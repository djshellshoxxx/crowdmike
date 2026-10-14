#include "FeedbackDetector.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::dsp {

namespace {
constexpr float kStartPromDb = 12.0f;   // prominence to start a track
constexpr float kKeepPromDb = 9.0f;     // hysteresis: prominence to keep a track alive
constexpr float kMaxWidthBins = 3.5f;   // -6 dB width; a stationary Hann-windowed sine is ~2.0 bins
constexpr float kMinLevelDb = -90.0f;
constexpr int kMedianInner = 4;         // neighbourhood excludes +-3 bins
constexpr int kMedianOuter = 19;
constexpr float kMatchTolBins = 1.0f;
constexpr float kDipDb = 15.0f;       // level drop that counts as "went away"
constexpr float kMinDriftBins = 0.35f, kMaxDriftBins = 1.0f;
constexpr float kAttackSec = 0.06f, kReleaseSec = 0.02f, kGrowthSec = 0.5f;

float frameCoef(float hopSec, float tauSec) noexcept { return std::exp(-hopSec / tauSec); }
float powToDb(float p) noexcept { return p > 1.0e-12f ? 10.0f * std::log10(p) : FeedbackDetector::kFloorDb; }
}

void FeedbackDetector::prepare(double sr) {
    std::scoped_lock lock(workMutex);
    sampleRate = sr > 0.0 ? sr : 48000.0;
    // Keep the analysis rate at 44.1/48 kHz so a 4096-point frame always spans ~85-93 ms (bin ~11 Hz) and the
    // tracker behaves identically at 88.2/96/176.4/192 kHz.
    decimation = 1;
    while (sampleRate / decimation > 50000.0 && decimation < 8) decimation *= 2;
    analysisRate = sampleRate / decimation;
    decimPhase = 0;
    decimPos = 0;
    if (decimation > 1) {
        // Blackman-windowed sinc lowpass, -6 dB at 0.92 x analysis Nyquist (~22 kHz); flat to ~18 kHz, >70 dB
        // rejection of everything that would alias into the analysed band.
        const int taps = 32 * decimation + 1;
        const double fc = 0.46 * analysisRate / sampleRate; // normalised to the input rate (cycles/sample)
        decimTaps.assign(static_cast<size_t>(taps), 0.0f);
        double sum = 0.0;
        const int mid = taps / 2;
        for (int i = 0; i < taps; ++i) {
            const double x = i - mid;
            const double sinc = x == 0 ? 2.0 * fc : std::sin(2.0 * 3.14159265358979323846 * fc * x) / (3.14159265358979323846 * x);
            const double w = 0.42 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (taps - 1))
                           + 0.08 * std::cos(4.0 * 3.14159265358979323846 * i / (taps - 1));
            decimTaps[static_cast<size_t>(i)] = static_cast<float>(sinc * w);
            sum += sinc * w;
        }
        for (auto& t : decimTaps) t = static_cast<float>(t / sum);
        decimHist.assign(static_cast<size_t>(2 * taps), 0.0f);
    } else {
        decimTaps.clear();
        decimHist.clear();
    }
    hopSec = static_cast<float>(kHop / analysisRate);
    attackCoef = frameCoef(hopSec, kAttackSec);
    releaseCoef = frameCoef(hopSec, kReleaseSec);
    growthCoef = frameCoef(hopSec, kGrowthSec);
    maxMissSec = std::max(0.07, 1.6 * hopSec);
    numBins = kFftSize / 2 + 1;
    const double binHz = analysisRate / kFftSize;
    // The neighbourhood median is one-sided near the band edges; loBin >= 5 keeps the +-1 bin interpolation and the
    // excluded +-3 bins inside the spectrum (at 192 kHz the lowest detectable peak is therefore ~234 Hz).
    loBin = std::max(5, static_cast<int>(std::ceil(40.0 / binHz)));
    hiBin = std::min(numBins - 10, static_cast<int>(std::floor(std::min(18000.0, 0.45 * analysisRate) / binHz)));

    fifo.prepare(static_cast<int>(std::ceil(sampleRate * 1.05)) + kFftSize); // >= 1 s of audio
    fft = std::make_unique<juce::dsp::FFT>(kFftOrder);
    detail::makeHann(window, kFftSize);
    double wsum = 0.0;
    for (float w : window) wsum += w;
    ampScale = static_cast<float>(2.0 / wsum);
    accum.assign(kFftSize, 0.0f);
    fftBuf.assign(2 * kFftSize, 0.0f);
    smoothPow.assign(static_cast<size_t>(numBins), 0.0f);
    specDb.assign(static_cast<size_t>(numBins), kFloorDb);
    drainBuf.assign(8192, 0.0f);
    medianScratch.assign(2 * kMedianOuter + 2, 0.0f);
    peaks.clear();
    peaks.reserve(static_cast<size_t>(numBins / 2 + 1));
    tracks.clear();
    tracks.reserve(kMaxTracks);
    {
        std::scoped_lock s(snapMutex);
        snapDb.assign(static_cast<size_t>(numBins), kFloorDb);
        snapCount = 0;
    }
    resetAnalysisState();
}

void FeedbackDetector::resetAnalysisState() {
    std::fill(accum.begin(), accum.end(), 0.0f);
    std::fill(smoothPow.begin(), smoothPow.end(), 0.0f);
    std::fill(specDb.begin(), specDb.end(), kFloorDb);
    accumFill = 0;
    std::fill(decimHist.begin(), decimHist.end(), 0.0f);
    decimPhase = 0;
    decimPos = 0;
    havePrevFrame = false;
    nowSec = 0.0;
    peaks.clear();
    tracks.clear();
}

void FeedbackDetector::reset() {
    std::scoped_lock lock(workMutex);
    fifo.clear();
    fifo.resetDroppedCount();
    resetAnalysisState();
    std::scoped_lock s(snapMutex);
    std::fill(snapDb.begin(), snapDb.end(), kFloorDb);
    snapCount = 0;
}

void FeedbackDetector::pushSamples(const float* mono, int n) noexcept { fifo.push(mono, n); }

void FeedbackDetector::analyse() {
    std::scoped_lock lock(workMutex);
    if (fft == nullptr) return;
    // Drain only what is queued now so a fast producer cannot keep us here forever.
    int remaining = fifo.getNumReady();
    bool anyFrame = false;
    while (remaining > 0) {
        const int n = fifo.pop(drainBuf.data(), std::min(remaining, static_cast<int>(drainBuf.size())));
        if (n <= 0) break;
        remaining -= n;
        const int m = decimate(drainBuf.data(), n); // in place, returns the number of analysis-rate samples
        int i = 0;
        while (i < m) {
            const int take = std::min(m - i, kFftSize - accumFill);
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
    if (anyFrame) publishSnapshot();
}

int FeedbackDetector::decimate(float* x, int n) noexcept {
    if (decimation == 1) return n;
    const int taps = static_cast<int>(decimTaps.size());
    int out = 0;
    for (int i = 0; i < n; ++i) {
        // Double-length history: the newest `taps` samples are always contiguous at decimHist[decimPos .. +taps).
        decimPos = decimPos == 0 ? taps - 1 : decimPos - 1;
        decimHist[static_cast<size_t>(decimPos)] = decimHist[static_cast<size_t>(decimPos + taps)] = x[i];
        if (++decimPhase >= decimation) {
            decimPhase = 0;
            const float* h = decimHist.data() + decimPos;
            float acc = 0.0f;
            for (int k = 0; k < taps; ++k) acc += decimTaps[static_cast<size_t>(k)] * h[k];
            x[out++] = acc; // out <= i, so writing in place is safe
        }
    }
    return out;
}

void FeedbackDetector::processFrame() {
    for (int i = 0; i < kFftSize; ++i) fftBuf[static_cast<size_t>(i)] = accum[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
    std::fill(fftBuf.begin() + kFftSize, fftBuf.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform(fftBuf.data(), true);

    for (int k = 0; k < numBins; ++k) {
        const float a = fftBuf[static_cast<size_t>(k)] * ampScale;
        const float p = std::isfinite(a) ? a * a : 0.0f;
        float& s = smoothPow[static_cast<size_t>(k)];
        if (!havePrevFrame) s = p;
        else {
            const float c = p > s ? attackCoef : releaseCoef;
            s = c * s + (1.0f - c) * p;
        }
        if (s < 1.0e-20f) s = 0.0f; // no denormals
        specDb[static_cast<size_t>(k)] = powToDb(s);
    }
    havePrevFrame = true;
    nowSec += hopSec;
    findPeaks();
    updateTracks();
}

void FeedbackDetector::findPeaks() {
    peaks.clear();
    const float* d = specDb.data();
    for (int k = loBin; k <= hiBin; ++k) {
        const float v = d[k];
        if (v <= kMinLevelDb || !(v > d[k - 1] && v >= d[k + 1])) continue;
        int m = 0;
        for (int j = kMedianInner; j <= kMedianOuter; ++j) {
            if (k - j >= 1) medianScratch[static_cast<size_t>(m++)] = d[k - j];
            if (k + j < numBins) medianScratch[static_cast<size_t>(m++)] = d[k + j];
        }
        auto mid = medianScratch.begin() + m / 2;
        std::nth_element(medianScratch.begin(), mid, medianScratch.begin() + m);
        const float prom = v - *mid;
        if (prom < kKeepPromDb) continue;

        // -6 dB width with linear interpolation of the crossings.
        const float thr = v - 6.0f;
        int l = k;
        while (l > k - 8 && l > 1 && d[l - 1] > thr) --l;
        int r = k;
        while (r < k + 8 && d[r + 1] > thr) ++r;
        if (d[l - 1] > thr || d[r + 1] > thr) continue; // very broad (or runs into the spectrum edge)
        const float lx = static_cast<float>(l) - (d[l] - thr) / std::max(1.0e-6f, d[l] - d[l - 1]);
        const float rx = static_cast<float>(r) + (d[r] - thr) / std::max(1.0e-6f, d[r] - d[r + 1]);
        const float width = rx - lx;
        if (width > kMaxWidthBins) continue;

        const float off = detail::parabolicOffset(d[k - 1], v, d[k + 1]);
        peaks.push_back({ static_cast<float>(k) + off, v, prom, width, false });
    }
    if (static_cast<int>(peaks.size()) > kMaxPeaks) {
        std::nth_element(peaks.begin(), peaks.begin() + kMaxPeaks, peaks.end(),
                         [](const Peak& a, const Peak& b) { return a.promDb > b.promDb; });
        peaks.resize(kMaxPeaks);
    }
}

void FeedbackDetector::updateTracks() {
    // Match existing tracks to the nearest unused peak within tolerance.
    for (auto& t : tracks) {
        Peak* best = nullptr;
        float bestDist = kMatchTolBins + 1.0e-4f;
        for (auto& p : peaks) {
            if (p.used) continue;
            const float dist = std::fabs(p.bin - t.bin);
            if (dist < bestDist) { bestDist = dist; best = &p; }
        }
        if (best == nullptr) continue;
        best->used = true;
        const double dt = std::max(1.0e-6, nowSec - t.lastSeenSec);
        const float inst = static_cast<float>((best->db - t.levelDb) / dt);
        t.growthDbPerSec = growthCoef * t.growthDbPerSec + (1.0f - growthCoef) * inst;
        t.levelDb = best->db;
        if (best->db < t.maxLevelDb - kDipDb) {
            // Deep level dip: the sound went away (e.g. alternating notes sharing a partial) and came back. A howl
            // does not do that, so its persistence restarts.
            t.seenSec = 0.0f;
            t.maxLevelDb = best->db;
            t.growthDbPerSec = 0.0f;
        }
        t.maxLevelDb = std::max(t.maxLevelDb, best->db);
        t.bin += 0.25f * (best->bin - t.bin);
        t.promDb += 0.3f * (best->promDb - t.promDb);
        t.widthBins += 0.3f * (best->widthBins - t.widthBins);
        t.lastSeenSec = nowSec;
        t.seenSec += hopSec;
        // Drift tolerance: 2 % of the frequency, at least 0.35 and at most 1 bin (a stationary howl's estimate jitters
        // far less; a semitone step is ~6 %).
        const float driftTol = clampf(0.02f * t.anchorBin, kMinDriftBins, kMaxDriftBins);
        if (std::fabs(best->bin - t.anchorBin) > driftTol || std::fabs(t.bin - t.anchorBin) > driftTol) {
            // Moving partial (glide, vibrato): not a stationary howl. Restart its persistence.
            t.anchorBin = t.bin = best->bin;
            t.seenSec = 0.0f;
            t.maxLevelDb = best->db;
            t.growthDbPerSec = 0.0f;
        }
    }
    // Drop tracks that vanished (transients, ended notes).
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                [this](const Track& t) { return nowSec - t.lastSeenSec > maxMissSec + 1.0e-6; }),
                 tracks.end());
    // Start new tracks.
    for (const auto& p : peaks) {
        if (p.used || p.promDb < kStartPromDb) continue;
        if (static_cast<int>(tracks.size()) >= kMaxTracks) break;
        tracks.push_back({ p.bin, p.bin, p.db, p.promDb, p.widthBins, 0.0f, 0.0f, p.db, nowSec, 0.0f });
    }
    for (auto& t : tracks) t.severity = computeSeverity(t);
}

float FeedbackDetector::computeSeverity(const Track& t) const noexcept {
    const float pers = t.seenSec;
    const float promScore = clampf((t.promDb - kStartPromDb) / 30.0f, 0.0f, 1.0f);
    const float persScore = clampf((pers - kMinPersistenceSec) / 4.5f, 0.0f, 1.0f);
    const float growthScore = clampf(t.growthDbPerSec / 10.0f, 0.0f, 1.0f);
    const float levelScore = clampf((t.levelDb + 60.0f) / 54.0f, 0.0f, 1.0f);
    return clampf(0.40f * promScore + 0.30f * persScore + 0.15f * growthScore + 0.15f * levelScore, 0.0f, 1.0f);
}

void FeedbackDetector::publishSnapshot() {
    std::array<FeedbackCandidate, kMaxCandidates> out {};
    std::array<float, kMaxCandidates> sev {};
    int count = 0;
    const float binHz = getBinHz();
    for (const auto& t : tracks) {
        const float pers = t.seenSec;
        if (pers + 1.0e-4f < kMinPersistenceSec) continue;
        FeedbackCandidate c;
        c.frequencyHz = t.bin * binHz;
        c.prominenceDb = t.promDb;
        c.persistenceSec = pers;
        c.severity = t.severity;
        c.suggestedQ = clampf(30.0f - (t.widthBins - 2.0f) * (20.0f / (kMaxWidthBins - 2.0f)), 10.0f, 30.0f);
        c.suggestedDepthDb = -(3.0f + 15.0f * t.severity);
        // Insert sorted by severity (descending); keep the top kMaxCandidates.
        int pos = count;
        while (pos > 0 && sev[static_cast<size_t>(pos - 1)] < c.severity) --pos;
        if (pos >= kMaxCandidates) continue;
        const int last = std::min(count, kMaxCandidates - 1);
        for (int i = last; i > pos; --i) {
            out[static_cast<size_t>(i)] = out[static_cast<size_t>(i - 1)];
            sev[static_cast<size_t>(i)] = sev[static_cast<size_t>(i - 1)];
        }
        out[static_cast<size_t>(pos)] = c;
        sev[static_cast<size_t>(pos)] = c.severity;
        count = std::min(count + 1, kMaxCandidates);
    }
    std::scoped_lock s(snapMutex);
    snapCandidates = out;
    snapCount = count;
    std::copy(specDb.begin(), specDb.end(), snapDb.begin());
}

int FeedbackDetector::getCandidates(FeedbackCandidate* out, int maxCount) const {
    if (out == nullptr || maxCount <= 0) return 0;
    std::scoped_lock s(snapMutex);
    const int n = std::min(maxCount, snapCount);
    for (int i = 0; i < n; ++i) out[i] = snapCandidates[static_cast<size_t>(i)];
    return n;
}

void FeedbackDetector::getSpectrumDb(float* out, int numOut) const {
    if (out == nullptr || numOut <= 0) return;
    std::scoped_lock s(snapMutex);
    detail::mapToLogBins(snapDb.empty() ? nullptr : snapDb.data(), static_cast<int>(snapDb.size()),
                         analysisRate / kFftSize, out, numOut, 20.0f, 20000.0f, kFloorDb);
}

} // namespace crowdmike::dsp
