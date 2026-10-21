#include "BeatTracker.h"
#include "DspCommon.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::dsp {
namespace {
constexpr double kScanLo = 58.0, kScanHi = 205.0, kScanStep = 0.25;
constexpr float kHarmonicWeight[4] = { 1.0f, 0.7f, 0.5f, 0.4f };
constexpr double kEstimateEverySec = 0.5;
constexpr double kMinHistorySec = 3.5;
constexpr int kWindowFrames = 2000;      // 8 s at 250 fps
constexpr float kSilentRms = 5.0e-4f;    // about -66 dBFS
constexpr float kMinConfidence = 0.25f;
constexpr float kOctaveUpRatio = 0.97f;   // take the faster octave when its comb score is at least this fraction of the slower one

inline double octaves(double a, double b) noexcept { return std::log2(a / b); }
}

void BeatTracker::prepare(double sampleRate) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    hop = std::max(16, static_cast<int>(std::lround(sr / 250.0)));
    frameRate = sr / hop;
    a1 = 1.0f - std::exp(-kTwoPi * 150.0f / static_cast<float>(sr));
    a2 = 1.0f - std::exp(-kTwoPi * 1500.0f / static_cast<float>(sr));
    fifo = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(kFifoFrames) * 2);
    for (int i = 0; i < kFifoFrames * 2; ++i) fifo[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
    writeFrames.store(0, std::memory_order_relaxed);
    readFrames = 0;
    pushed.store(0, std::memory_order_relaxed);
    localPushed = 0;
    feat = Features {};
    envRing.assign(kHistory, 0.0f);
    levelRing.assign(kHistory, 0.0f);
    win.assign(kHistory, 0.0f);
    work.assign(kHistory, 0.0f);
    smoothed.assign(kHistory, 0.0f);
    prefix.assign(kHistory + 1, 0.0);
    const int maxLag = kWindowFrames / 2 + 2;
    ac.assign(static_cast<size_t>(maxLag), 0.0f);
    scoreGrid.assign(static_cast<size_t>((kScanHi - kScanLo) / kScanStep) + 2, 0.0f);
    seenGenAudio = seenGenWorker = generation.load();
    analysisReset();
}

void BeatTracker::reset() noexcept { generation.fetch_add(1, std::memory_order_acq_rel); }

void BeatTracker::analysisReset() noexcept {
    absFrame = 0;
    sinceEstimate = 0;
    have = false;
    lockedBpm = candBpm = 0.0;
    candCount = 0;
    confSmooth = 0.0f;
    lastBeat = 0;
    std::fill(envRing.begin(), envRing.end(), 0.0f);
    std::fill(levelRing.begin(), levelRing.end(), 0.0f);
    publish(false, 0.0, 0.0f, 0, 0);
}

// ------------------------------------------------------------------------------------------ audio thread
void BeatTracker::pushSamples(const float* x, int n) noexcept {
    if (fifo == nullptr || x == nullptr || n <= 0) return;
    const std::uint32_t g = generation.load(std::memory_order_acquire);
    if (g != seenGenAudio) { seenGenAudio = g; feat.havePrev = false; }   // frame boundaries stay aligned to `pushed`
    std::uint64_t w = writeFrames.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        const float s = sanitize(x[i]);
        feat.lp1 += a1 * (s - feat.lp1);
        feat.lp2 += a2 * (s - feat.lp2);
        const float low = feat.lp1, mid = feat.lp2 - feat.lp1, high = s - feat.lp2;
        feat.e0 += static_cast<double>(low) * low;
        feat.e1 += static_cast<double>(mid) * mid;
        feat.e2 += static_cast<double>(high) * high;
        if (++feat.count < hop) continue;
        const double inv = 1.0 / hop;
        const float r0 = static_cast<float>(std::sqrt(feat.e0 * inv));
        const float r1 = static_cast<float>(std::sqrt(feat.e1 * inv));
        const float r2 = static_cast<float>(std::sqrt(feat.e2 * inv));
        const float v[3] = { std::log1p(200.0f * r0), std::log1p(200.0f * r1), std::log1p(200.0f * r2) };
        float flux = 0.0f;
        if (feat.havePrev)
            for (int b = 0; b < 3; ++b) flux += std::max(0.0f, v[b] - feat.prev[b]);
        for (int b = 0; b < 3; ++b) feat.prev[b] = v[b];
        feat.havePrev = true;
        const float level = static_cast<float>(std::sqrt((feat.e0 + feat.e1 + feat.e2) * inv));
        feat.e0 = feat.e1 = feat.e2 = 0.0;
        feat.count = 0;
        const size_t slot = static_cast<size_t>(w % kFifoFrames) * 2;
        fifo[slot].store(flux, std::memory_order_relaxed);
        fifo[slot + 1].store(level, std::memory_order_relaxed);
        ++w;
        writeFrames.store(w, std::memory_order_release);
    }
    localPushed += n;
    pushed.store(localPushed, std::memory_order_release);
}

// ------------------------------------------------------------------------------------------ worker thread
void BeatTracker::analyse() {
    if (fifo == nullptr) return;
    const std::uint32_t g = generation.load(std::memory_order_acquire);
    if (g != seenGenWorker) {
        seenGenWorker = g;
        analysisReset();
        readFrames = writeFrames.load(std::memory_order_acquire);
    }
    const int stepFrames = std::max(1, static_cast<int>(std::lround(kEstimateEverySec * frameRate)));
    for (;;) {
        const std::uint64_t w = writeFrames.load(std::memory_order_acquire);
        if (readFrames >= w) break;
        if (w - readFrames > static_cast<std::uint64_t>(kFifoFrames - 8)) {   // overrun: keep the lock, drop the history
            readFrames = w - static_cast<std::uint64_t>(kFifoFrames / 2);
            absFrame = 0;
            sinceEstimate = 0;
            std::fill(envRing.begin(), envRing.end(), 0.0f);
            std::fill(levelRing.begin(), levelRing.end(), 0.0f);
        }
        const size_t slot = static_cast<size_t>(readFrames % kFifoFrames) * 2;
        const float flux = fifo[slot].load(std::memory_order_relaxed);
        const float level = fifo[slot + 1].load(std::memory_order_relaxed);
        // frame index in the pushed-sample timeline: readFrames (frames are aligned to multiples of `hop`)
        ++readFrames;
        onFrame(flux, level);
        if (++sinceEstimate >= stepFrames && static_cast<double>(std::min<std::uint64_t>(absFrame, kHistory)) >= kMinHistorySec * frameRate) {
            sinceEstimate = 0;
            estimate();
        }
    }
}

void BeatTracker::onFrame(float flux, float level) noexcept {
    const size_t idx = static_cast<size_t>(absFrame % kHistory);
    envRing[idx] = std::isfinite(flux) ? std::min(flux, 12.0f) : 0.0f;
    levelRing[idx] = std::isfinite(level) ? level : 0.0f;
    ++absFrame;
}

void BeatTracker::publish(bool valid, double bpm, float conf, std::int64_t lastBeatSample, std::int64_t nowSample) noexcept {
    seq.fetch_add(1, std::memory_order_acq_rel);   // odd: writing
    pubValid.store(valid, std::memory_order_relaxed);
    pubBpm.store(bpm, std::memory_order_relaxed);
    pubConf.store(conf, std::memory_order_relaxed);
    pubLastBeat.store(lastBeatSample, std::memory_order_relaxed);
    pubNow.store(nowSample, std::memory_order_relaxed);
    seq.fetch_add(1, std::memory_order_acq_rel);   // even: stable
}

BeatEstimate BeatTracker::getEstimate() const noexcept {
    BeatEstimate e;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::uint32_t s0 = seq.load(std::memory_order_acquire);
        if (s0 & 1u) continue;
        e.valid = pubValid.load(std::memory_order_relaxed);
        e.bpm = pubBpm.load(std::memory_order_relaxed);
        e.confidence = pubConf.load(std::memory_order_relaxed);
        e.lastBeatSample = pubLastBeat.load(std::memory_order_relaxed);
        e.nowSample = pubNow.load(std::memory_order_relaxed);
        if (seq.load(std::memory_order_acquire) == s0) break;
    }
    e.periodSamples = e.valid && e.bpm > 0.0 ? 60.0 * sr / e.bpm : 0.0;
    if (!std::isfinite(e.bpm) || !std::isfinite(e.confidence)) e = BeatEstimate {};
    return e;
}

void BeatTracker::estimate() {
    const int total = static_cast<int>(std::min<std::uint64_t>(absFrame, kHistory));
    const int N = std::min(total, kWindowFrames);
    if (N < 64) return;
    const std::uint64_t firstAbs = absFrame - static_cast<std::uint64_t>(N);
    for (int k = 0; k < N; ++k) win[static_cast<size_t>(k)] = envRing[static_cast<size_t>((firstAbs + static_cast<std::uint64_t>(k)) % kHistory)];

    // ---- silence gate (mean level over the last second)
    {
        const int L = std::min(N, static_cast<int>(frameRate));
        double acc = 0.0;
        for (int k = 0; k < L; ++k) acc += levelRing[static_cast<size_t>((absFrame - 1 - static_cast<std::uint64_t>(k)) % kHistory)];
        if (acc / L < kSilentRms) {
            confSmooth *= 0.8f;
            publish(have, lockedBpm, confSmooth, lastBeat, static_cast<std::int64_t>(readFrames) * hop);
            return;
        }
    }

    // ---- onset envelope conditioning: local mean removal, rectify, 3-tap smoothing
    prefix[0] = 0.0;
    for (int k = 0; k < N; ++k) prefix[static_cast<size_t>(k) + 1] = prefix[static_cast<size_t>(k)] + win[static_cast<size_t>(k)];
    const int half = std::max(2, static_cast<int>(0.3 * frameRate));
    for (int k = 0; k < N; ++k) {
        const int a = std::max(0, k - half), b = std::min(N, k + half + 1);
        const double mean = (prefix[static_cast<size_t>(b)] - prefix[static_cast<size_t>(a)]) / (b - a);
        work[static_cast<size_t>(k)] = std::max(0.0f, win[static_cast<size_t>(k)] - static_cast<float>(mean));
    }
    for (int k = 0; k < N; ++k) {
        const float l = work[static_cast<size_t>(std::max(0, k - 1))], c = work[static_cast<size_t>(k)], r = work[static_cast<size_t>(std::min(N - 1, k + 1))];
        smoothed[static_cast<size_t>(k)] = 0.25f * l + 0.5f * c + 0.25f * r;
    }
    double e0 = 0.0;
    for (int k = 0; k < N; ++k) e0 += static_cast<double>(smoothed[static_cast<size_t>(k)]) * smoothed[static_cast<size_t>(k)];
    e0 /= N;
    if (!(e0 > 1.0e-9)) {
        confSmooth *= 0.8f;
        publish(have, lockedBpm, confSmooth, lastBeat, static_cast<std::int64_t>(readFrames) * hop);
        return;
    }

    // ---- autocorrelation (unbiased, normalised to lag 0)
    const int lagLo = std::max(2, static_cast<int>(std::floor(frameRate * 60.0 / (kScanHi + 5.0))) - 2);
    const int lagHi = std::min(N / 2, static_cast<int>(ac.size()) - 1);
    std::fill(ac.begin(), ac.end(), 0.0f);
    for (int l = lagLo; l <= lagHi; ++l) {
        double s = 0.0;
        const float* a = smoothed.data();
        const float* b = smoothed.data() + l;
        for (int k = 0; k < N - l; ++k) s += static_cast<double>(a[k]) * b[k];
        ac[static_cast<size_t>(l)] = static_cast<float>(s / (N - l) / e0);
    }
    // smooth the autocorrelation a little (peak shapes, +/-1 frame jitter)
    {
        float prev = ac[static_cast<size_t>(lagLo)];
        for (int l = lagLo + 1; l < lagHi; ++l) {
            const float cur = ac[static_cast<size_t>(l)];
            ac[static_cast<size_t>(l)] = 0.25f * prev + 0.5f * cur + 0.25f * ac[static_cast<size_t>(l) + 1];
            prev = cur;
        }
    }
    auto acAt = [&](double lag, bool& ok) noexcept -> float {
        if (lag < lagLo || lag >= lagHi) { ok = false; return 0.0f; }
        const int i = static_cast<int>(lag);
        const float f = static_cast<float>(lag - i);
        return ac[static_cast<size_t>(i)] * (1.0f - f) + ac[static_cast<size_t>(i) + 1] * f;
    };
    auto score = [&](double bpm) noexcept -> float {
        const double T = frameRate * 60.0 / bpm;
        float num = 0.0f, den = 0.0f;
        for (int h = 0; h < 4; ++h) {
            bool ok = true;
            const float v = acAt(T * (h + 1), ok);
            if (!ok) continue;
            num += kHarmonicWeight[h] * std::max(0.0f, v);
            den += kHarmonicWeight[h];
        }
        return den > 0.0f ? num / den : 0.0f;
    };

    // ---- coarse scan with a mild tempo prior
    int bestIdx = 0;
    double bestWeighted = -1.0, meanScore = 0.0;
    const int gridCount = static_cast<int>((kScanHi - kScanLo) / kScanStep) + 1;
    for (int i = 0; i < gridCount; ++i) {
        const double b = kScanLo + kScanStep * i;
        const float s = score(b);
        scoreGrid[static_cast<size_t>(i)] = s;
        meanScore += s;
        const double z = octaves(b, 120.0) / 2.5;   // very mild: only breaks ties between non-octave candidates
        const double weighted = s * std::exp(-0.5 * z * z);
        if (weighted > bestWeighted) { bestWeighted = weighted; bestIdx = i; }
    }
    meanScore /= gridCount;
    double bpm = kScanLo + kScanStep * bestIdx;

    // ---- octave resolution: prefer the faster level when its evidence is nearly as strong
    for (int pass = 0; pass < 2; ++pass) {
        bool moved = false;
        for (int m = 2; m <= 3; ++m) {
            const double nb = bpm * m;
            if (nb > kMaxBpm + 0.5) break;
            if (score(nb) >= kOctaveUpRatio * score(bpm)) { bpm = nb; moved = true; break; }
        }
        if (!moved) break;
    }

    // ---- fine search around the winner (centroid of the plateau)
    {
        const int steps = 300;
        double bestS = -1.0;
        int bi = 0;
        std::vector<float>& fine = work;   // reuse scratch (conditioning is finished)
        const double lo = bpm * 0.97;
        const double stepB = bpm * 0.06 / steps;
        for (int i = 0; i <= steps; ++i) {
            const float s = score(lo + stepB * i);
            fine[static_cast<size_t>(i)] = s;
            if (s > bestS) { bestS = s; bi = i; }
        }
        int l = bi, r = bi;
        while (l > 0 && fine[static_cast<size_t>(l) - 1] >= 0.93 * bestS) --l;
        while (r < steps && fine[static_cast<size_t>(r) + 1] >= 0.93 * bestS) ++r;
        double sw = 0.0, sb = 0.0;
        for (int i = l; i <= r; ++i) { sw += fine[static_cast<size_t>(i)]; sb += fine[static_cast<size_t>(i)] * (lo + stepB * i); }
        if (sw > 0.0) bpm = sb / sw;
    }
    bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
    if (!std::isfinite(bpm)) return;

    // ---- confidence: peak sharpness against the scan average, and periodicity at the chosen lag
    const float sBest = score(bpm);
    bool okLag = true;
    const float acBest = acAt(frameRate * 60.0 / bpm, okLag);
    const float conf1 = clampf((sBest / static_cast<float>(meanScore + 1.0e-6) - 1.0f) / 3.0f, 0.0f, 1.0f);
    const float conf2 = clampf(acBest * 1.5f, 0.0f, 1.0f);
    const float conf = std::min(conf1, conf2);
    numEstimates.fetch_add(1, std::memory_order_relaxed);

    // ---- hysteresis / lock logic
    auto closeTo = [](double a, double b, double rel) { return std::fabs(a - b) <= rel * b; };
    if (conf < kMinConfidence) {
        confSmooth = 0.7f * confSmooth + 0.3f * conf;
    } else {
        confSmooth = 0.5f * confSmooth + 0.5f * conf;
        if (!have) {
            if (candCount > 0 && closeTo(bpm, candBpm, 0.03)) { ++candCount; candBpm = 0.5 * (candBpm + bpm); }
            else { candBpm = bpm; candCount = 1; }
            if (candCount >= 2) { lockedBpm = candBpm; have = true; candCount = 0; }
        } else {
            const double rel = std::fabs(bpm - lockedBpm) / lockedBpm;
            if (rel < 0.025) {
                lockedBpm += 0.35 * (bpm - lockedBpm);
                candCount = 0;
            } else {
                if (candCount > 0 && closeTo(bpm, candBpm, 0.03)) ++candCount; else { candBpm = bpm; candCount = 1; }
                const double oct = std::fabs(octaves(bpm, lockedBpm));
                const bool octaveRelated = std::fabs(oct - 1.0) < 0.06 || std::fabs(oct - 0.585) < 0.06;
                const int need = octaveRelated ? 6 : 3;
                if (candCount >= need && conf >= 0.4f) { lockedBpm = candBpm; candCount = 0; }
            }
        }
    }
    lockedBpm = have ? std::clamp(lockedBpm, kMinBpm, kMaxBpm) : 0.0;

    // ---- beat phase: comb over the recent window at the locked period
    if (have) {
        const double P = frameRate * 60.0 / lockedBpm;
        const int Wp = std::min(N, static_cast<int>(6.0 * frameRate));
        const int M = std::max(2, static_cast<int>(Wp / P));
        double bestPhase = 0.0, bestVal = -1.0;
        for (double ph = 0.0; ph < P; ph += 0.5) {
            double acc = 0.0;
            for (int m = 0; m < M; ++m) {
                const double pos = (N - 1) - ph - m * P;
                if (pos < 1.0) break;
                const int i = static_cast<int>(pos);
                const float f = static_cast<float>(pos - i);
                acc += smoothed[static_cast<size_t>(i)] * (1.0f - f) + smoothed[static_cast<size_t>(i) + 1 < static_cast<size_t>(N) ? static_cast<size_t>(i) + 1 : static_cast<size_t>(i)] * f;
            }
            if (acc > bestVal) { bestVal = acc; bestPhase = ph; }
        }
        const double beatFrame = static_cast<double>(readFrames) - N + (N - 1) - bestPhase;   // monotone frame index
        lastBeat = static_cast<std::int64_t>((beatFrame + 0.5) * hop);
    }
    publish(have, lockedBpm, std::clamp(confSmooth, 0.0f, 1.0f), lastBeat, static_cast<std::int64_t>(readFrames) * hop);
}
}
