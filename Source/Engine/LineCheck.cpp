#include "LineCheck.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::linecheck {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
inline float finite(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
inline float toDb(double power) noexcept { return static_cast<float>(10.0 * std::log10(std::max(power, 1.0e-20))); }
inline float ampToDb(double a) noexcept { return static_cast<float>(20.0 * std::log10(std::max(a, 1.0e-12))); }
}

const char* verdictName(Verdict v) noexcept {
    switch (v) {
        case Verdict::Dead: return "Dead";
        case Verdict::Clipping: return "Clipping";
        case Verdict::Hot: return "Hot";
        case Verdict::Low: return "Low";
        case Verdict::Ok: return "OK";
    }
    return "?";
}

// ------------------------------------------------------------------ probe
void ProbeBank::prepare(double sampleRate, int numInputs) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    inputs = std::clamp(numInputs, 0, kMaxInputs);
    frameLen = std::max(1, static_cast<int>(std::lround(sr * kFrameSeconds)));
    for (int b = 0; b < kHumBins; ++b) {
        const double k = std::round(static_cast<double>(kHumFreqs[b]) * frameLen / sr);   // exact integer bin for N/sr == 0.1
        coef[b] = 2.0 * std::cos(kTwoPi * k / frameLen);
    }
    ring.assign(static_cast<size_t>(kMaxInputs) * kRingFrames, Frame {});
    for (auto& h : head) h.store(0);
    for (auto& t : tail) t.store(0);
    armed.store(false);
    droppedFrames.store(0);
    generationNow.fetch_add(1);
    myGeneration = generationNow.load();
    resetAccumulators();
}

void ProbeBank::start() noexcept {
    generationNow.fetch_add(1, std::memory_order_acq_rel);
    droppedFrames.store(0, std::memory_order_relaxed);
    armed.store(true, std::memory_order_release);
}

void ProbeBank::resetAccumulators() noexcept {
    for (auto& a : acc) a = Acc {};
    frameCount = 0;
}

bool ProbeBank::push(int input, const Frame& f) noexcept {
    const auto h = head[static_cast<size_t>(input)].load(std::memory_order_relaxed);
    const auto t = tail[static_cast<size_t>(input)].load(std::memory_order_acquire);
    if (h - t >= static_cast<std::uint32_t>(kRingFrames)) { droppedFrames.fetch_add(1, std::memory_order_relaxed); return false; }
    ring[static_cast<size_t>(input) * kRingFrames + (h % kRingFrames)] = f;
    head[static_cast<size_t>(input)].store(h + 1, std::memory_order_release);
    return true;
}

bool ProbeBank::pop(int input, Frame& out) noexcept {
    if (input < 0 || input >= kMaxInputs || ring.empty()) return false;
    const auto t = tail[static_cast<size_t>(input)].load(std::memory_order_relaxed);
    const auto h = head[static_cast<size_t>(input)].load(std::memory_order_acquire);
    if (t == h) return false;
    out = ring[static_cast<size_t>(input) * kRingFrames + (t % kRingFrames)];
    tail[static_cast<size_t>(input)].store(t + 1, std::memory_order_release);
    return true;
}

void ProbeBank::finishFrame(int numIn) noexcept {
    for (int i = 0; i < numIn; ++i) {
        Frame f;
        const auto& a = acc[static_cast<size_t>(i)];
        f.n = frameLen;
        f.peak = a.peak;
        f.sum = a.sum;
        f.sumSq = a.sumSq;
        f.cross = a.cross;
        f.clip = a.clip;
        f.generation = myGeneration;
        const double inv = 4.0 / (static_cast<double>(frameLen) * static_cast<double>(frameLen));
        for (int b = 0; b < kHumBins; ++b) {
            const double s1 = a.s1[b], s2 = a.s2[b];
            const double power = s1 * s1 + s2 * s2 - coef[b] * s1 * s2;
            f.hum[b] = static_cast<float>(std::max(0.0, power) * inv);
        }
        push(i, f);
    }
    resetAccumulators();
}

void ProbeBank::process(const float* const* in, int numIn, int n) noexcept {
    if (!armed.load(std::memory_order_relaxed)) return;
    const auto gen = generationNow.load(std::memory_order_acquire);
    if (gen != myGeneration) { myGeneration = gen; resetAccumulators(); }
    numIn = std::min({ numIn, inputs, kMaxInputs });
    int offset = 0;
    while (offset < n) {
        const int seg = std::min(n - offset, frameLen - frameCount);
        for (int i = 0; i < numIn; ++i) {
            auto& a = acc[static_cast<size_t>(i)];
            const float* x = in[i] + offset;
            const float* nx = (i + 1 < numIn) ? in[i + 1] + offset : nullptr;
            double s1[kHumBins], s2[kHumBins];
            for (int b = 0; b < kHumBins; ++b) { s1[b] = a.s1[b]; s2[b] = a.s2[b]; }
            float peak = a.peak;
            double sum = a.sum, sumSq = a.sumSq, cross = a.cross;
            int clip = a.clip;
            for (int s = 0; s < seg; ++s) {
                const float v = finite(x[s]);
                const float av = std::abs(v);
                if (av > peak) peak = av;
                if (av >= kClipLevel) ++clip;
                sum += v;
                sumSq += static_cast<double>(v) * v;
                if (nx != nullptr) cross += static_cast<double>(v) * finite(nx[s]);
                for (int b = 0; b < kHumBins; ++b) {
                    const double s0 = v + coef[b] * s1[b] - s2[b];
                    s2[b] = s1[b];
                    s1[b] = s0;
                }
            }
            a.peak = peak; a.sum = sum; a.sumSq = sumSq; a.cross = cross; a.clip = clip;
            for (int b = 0; b < kHumBins; ++b) { a.s1[b] = s1[b]; a.s2[b] = s2[b]; }
        }
        frameCount += seg;
        offset += seg;
        if (frameCount >= frameLen) finishFrame(numIn);
    }
}

// ------------------------------------------------------------------ collector
void Collector::begin(ProbeBank& bank, int numInputs) {
    perInput.assign(static_cast<size_t>(std::clamp(numInputs, 0, kMaxInputs)), {});
    for (auto& v : perInput) v.reserve(static_cast<size_t>(kFramesPerListen + 8));
    bank.start();
    generation = bank.generation();
    minFrames = 0;
}

void Collector::pull(ProbeBank& bank) {
    Frame f;
    for (size_t i = 0; i < perInput.size(); ++i)
        while (bank.pop(static_cast<int>(i), f))
            if (f.generation == generation && static_cast<int>(perInput[i].size()) < kFramesPerListen) perInput[i].push_back(f);
    minFrames = perInput.empty() ? 0 : static_cast<int>(perInput.front().size());
    for (const auto& v : perInput) minFrames = std::min(minFrames, static_cast<int>(v.size()));
}

// ------------------------------------------------------------------ analyser
std::vector<Result> Analyser::analyse(const std::vector<std::vector<Frame>>& frames, const std::vector<InputContext>& contexts,
                                      const Settings& s) {
    const int count = static_cast<int>(frames.size());
    std::vector<Result> out(static_cast<size_t>(count));
    std::vector<double> sumSqTotal(static_cast<size_t>(count), 0.0), crossTotal(static_cast<size_t>(count), 0.0);
    for (int i = 0; i < count; ++i) {
        auto& r = out[static_cast<size_t>(i)];
        r.input = i;
        const InputContext ctx = i < static_cast<int>(contexts.size()) ? contexts[static_cast<size_t>(i)] : InputContext {};
        const auto& fr = frames[static_cast<size_t>(i)];
        std::vector<const Frame*> use;
        for (const auto& f : fr) if (f.n > 0) use.push_back(&f);
        r.frames = static_cast<int>(use.size());
        if (use.empty()) { r.verdict = Verdict::Dead; continue; }
        r.valid = true;

        double sum = 0.0, nTotal = 0.0, crossSum = 0.0, ssSum = 0.0;
        float peak = 0.0f;
        std::vector<double> ac;
        ac.reserve(use.size());
        for (const auto* f : use) {
            peak = std::max(peak, f->peak);
            r.clips += f->clip;
            sum += f->sum;
            nTotal += f->n;
            crossSum += f->cross;
            ssSum += f->sumSq;
            const double mean = f->sum / f->n;
            ac.push_back(std::max(0.0, f->sumSq / f->n - mean * mean));
        }
        sumSqTotal[static_cast<size_t>(i)] = ssSum;
        crossTotal[static_cast<size_t>(i)] = crossSum;
        const double mean = sum / nTotal;
        r.meanDc = static_cast<float>(mean);
        r.dcDb = ampToDb(std::abs(mean));
        r.peakDb = ampToDb(peak);

        const int F = static_cast<int>(use.size());
        std::vector<int> order(static_cast<size_t>(F));
        for (int k = 0; k < F; ++k) order[static_cast<size_t>(k)] = k;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return ac[static_cast<size_t>(a)] < ac[static_cast<size_t>(b)]; });
        const int m = std::max(1, static_cast<int>(std::ceil(0.3 * F)));
        double loud = 0.0;
        for (int k = 0; k < m; ++k) loud += ac[static_cast<size_t>(order[static_cast<size_t>(F - 1 - k)])];
        r.programRmsDb = toDb(loud / m);
        r.noiseFloorDb = toDb(ac[static_cast<size_t>(order[static_cast<size_t>(std::floor(0.1 * (F - 1)))])]);

        // hum on the quietest 30 % of the frames
        double hum50 = 0.0, hum60 = 0.0, quietAc = 0.0;
        for (int k = 0; k < m; ++k) {
            const auto& f = *use[static_cast<size_t>(order[static_cast<size_t>(k)])];
            for (int b = 0; b < 4; ++b) { hum50 += 0.5 * f.hum[b]; hum60 += 0.5 * f.hum[b + 4]; }
            quietAc += ac[static_cast<size_t>(order[static_cast<size_t>(k)])];
        }
        hum50 /= m; hum60 /= m; quietAc /= m;
        const bool use50 = hum50 >= hum60;
        const double humMs = use50 ? hum50 : hum60;
        r.humRatio = static_cast<float>(humMs / (quietAc + 1.0e-20));
        r.humDb = toDb(humMs);
        r.humHz = use50 ? 50 : 60;

        // verdict
        const float acPeak = std::max(0.0f, peak - static_cast<float>(std::abs(mean)));
        const bool dead = r.programRmsDb < kDeadRmsDb && acPeak <= kDeadAcPeak;
        if (dead) r.verdict = Verdict::Dead;
        else if (r.clips >= 1) r.verdict = Verdict::Clipping;
        else if (peak >= kHotPeak || r.programRmsDb > kHotRmsDb) r.verdict = Verdict::Hot;
        else if (r.programRmsDb < kLowRmsDb) r.verdict = Verdict::Low;
        else r.verdict = Verdict::Ok;
        r.dc = std::abs(mean) >= kDcThreshold;
        r.hum = !dead && r.humRatio >= kHumRatio && r.humDb >= kHumMinDb;
        if (!r.hum) r.humHz = 0;

        // trim + high-pass proposal
        if (!dead) {
            const float ideal = s.targetRmsDb - r.programRmsDb;
            const float headroom = s.peakCeilingDb - r.peakDb;
            float proposed = std::min(ideal, headroom);
            r.limitedByHeadroom = headroom < ideal;
            if (r.clips > 0) proposed = std::min(proposed, ctx.trimDb);
            r.proposedTrimDb = std::clamp(proposed, -s.trimClampDb, s.trimClampDb);
            r.hasTrim = true;
            r.trimNoChange = std::abs(r.proposedTrimDb - ctx.trimDb) < kTrimDeadbandDb;
            r.suggestHpOn = true;
            r.suggestHpHz = r.hum ? kHpfHumHz : kHpfDefaultHz;
            r.hpNoChange = ctx.hpOn && ctx.hpHz >= r.suggestHpHz;
        }
    }

    // polarity vs neighbour
    for (int i = 0; i + 1 < count; ++i) {
        auto& a = out[static_cast<size_t>(i)];
        auto& b = out[static_cast<size_t>(i + 1)];
        if (!a.valid || !b.valid) continue;
        const double denom = std::sqrt(sumSqTotal[static_cast<size_t>(i)] * sumSqTotal[static_cast<size_t>(i + 1)]);
        if (denom <= 0.0) continue;
        const float corr = static_cast<float>(crossTotal[static_cast<size_t>(i)] / denom);
        const bool pa = i < static_cast<int>(contexts.size()) && contexts[static_cast<size_t>(i)].polarityInverted;
        const bool pb = i + 1 < static_cast<int>(contexts.size()) && contexts[static_cast<size_t>(i + 1)].polarityInverted;
        const float eff = (pa != pb) ? -corr : corr;
        a.correlationNext = eff;
        if (a.verdict == Verdict::Dead || b.verdict == Verdict::Dead) continue;
        if (eff <= kPolarityCorr && a.programRmsDb >= kPolarityMinRmsDb && b.programRmsDb >= kPolarityMinRmsDb
            && std::abs(a.programRmsDb - b.programRmsDb) <= kPolarityMaxLevelDiffDb) {
            b.polarityMismatch = true;
            b.polarityPartner = i;
        }
    }
    return out;
}

std::string describeFlags(const Result& r) {
    std::string t;
    const auto add = [&](const std::string& s) { if (!t.empty()) t += ", "; t += s; };
    if (r.dc) add("DC offset " + std::to_string(static_cast<int>(std::lround(r.dcDb))) + " dB");
    if (r.hum) add("Hum suspected " + std::to_string(r.humHz) + " Hz");
    if (r.polarityMismatch) add("Possible polarity mismatch vs input " + std::to_string(r.polarityPartner + 1));
    if (r.limitedByHeadroom && r.hasTrim) add("trim limited by headroom");
    return t;
}

int chooseTemplate(int inputs, int outputs) noexcept {
    if (inputs <= 1) return 1;
    if (inputs == 2) return 2;
    if (inputs <= 4) return 3;
    if (inputs >= 8 && outputs >= 8) return 8;
    if (outputs >= 4) return 4;
    return 3;
}
}
