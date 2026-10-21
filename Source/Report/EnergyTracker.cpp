#include "EnergyTracker.h"
#include <algorithm>
#include <cmath>

namespace crowdmike::report {
namespace {
float clampDb(float x) noexcept {
    if (!std::isfinite(x)) return kFloorDb;
    return std::min(std::max(x, kFloorDb), kCeilDb);
}
double dbToPower(float db) noexcept { return std::pow(10.0, static_cast<double>(db) / 10.0); }
float powerToDb(double p) noexcept {
    if (!(p > 1.0e-12)) return kFloorDb;
    return std::min(std::max(static_cast<float>(10.0 * std::log10(p)), kFloorDb), kCeilDb);
}
std::uint32_t delta(std::uint32_t cur, std::uint32_t before) noexcept { return cur >= before ? cur - before : 0u; }
int toInt(std::uint32_t v) noexcept { return v > 1000000u ? 1000000 : static_cast<int>(v); }

template <typename T>
void capEvents(std::vector<T>& v, std::size_t cap) {
    if (v.size() > cap) v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() - cap));
}
}

void EnergyTracker::Acc::add(const Sample& s) noexcept {
    if (n == 0) t = s.t;
    pCrowd += dbToPower(s.crowdDb);
    pMaster += dbToPower(s.masterDb);
    pLufs += dbToPower(s.lufs);
    dens += s.density;
    peak = std::max(peak, s.masterPeakDb);
    clip += s.clip; limiter += s.limiter; feedback += s.feedback; panic += s.panic; fx += s.fx; scene += s.scene; xrun += s.xrun;
    rec = rec || s.recording;
    ++n;
}

Sample EnergyTracker::Acc::finish() const noexcept {
    Sample s;
    if (n <= 0) return s;
    const double inv = 1.0 / n;
    s.t = t;
    s.crowdDb = powerToDb(pCrowd * inv);
    s.masterDb = powerToDb(pMaster * inv);
    s.lufs = powerToDb(pLufs * inv);
    s.density = static_cast<float>(dens * inv);
    s.masterPeakDb = peak;
    s.clip = clip; s.limiter = limiter; s.feedback = feedback; s.panic = panic; s.fx = fx; s.scene = scene; s.xrun = xrun;
    s.recording = rec;
    return s;
}

EnergyTracker::EnergyTracker(const Config& c) : cfg(c), detector(c.peak) {
    cfg.maxSamples = std::max<std::size_t>(cfg.maxSamples, 4);
}

void EnergyTracker::reset() {
    detector.reset();
    series_.clear();
    step = 1;
    acc = Acc {};
    haveFirst = havePrev = false;
    firstT = lastT = 0.0;
    lastCrowd = kFloorDb;
    prev = Counters {};
    pendingScene = 0;
    peaks_.clear(); suppressed_.clear(); feedback_.clear(); scenes_.clear(); markers_.clear(); fx_.clear();
    activeFbCount = 0;
    recent.fill(kFloorDb);
    recentPos = recentCount = 0;
}

void EnergyTracker::halve() {
    std::size_t out = 0;
    for (std::size_t i = 0; i < series_.size(); i += 2) {
        Acc a;
        a.add(series_[i]);
        if (i + 1 < series_.size()) a.add(series_[i + 1]);
        series_[out++] = a.finish();
    }
    series_.resize(out);
    step *= 2;
}

void EnergyTracker::flushBucket() {
    if (acc.n <= 0) return;
    if (series_.size() >= cfg.maxSamples) halve();
    series_.push_back(acc.finish());
    acc = Acc {};
}

EnergyTracker::PushResult EnergyTracker::push(const TrackerInput& in) {
    PushResult res;
    if (!std::isfinite(in.t) || (haveFirst && in.t <= lastT)) return res;
    if (!haveFirst) { haveFirst = true; firstT = in.t; }
    lastT = in.t;

    Sample s;
    s.t = in.t;
    s.crowdDb = clampDb(in.crowdDb);
    s.density = std::isfinite(in.density) ? std::min(std::max(in.density, 0.0f), 1.0f) : 0.0f;
    s.masterDb = clampDb(in.masterDb);
    s.masterPeakDb = clampDb(in.masterPeakDb);
    s.lufs = clampDb(in.lufs);
    s.recording = in.recording;
    lastCrowd = s.crowdDb;
    recent[static_cast<size_t>(recentPos)] = s.crowdDb;
    recentPos = (recentPos + 1) % static_cast<int>(recent.size());
    recentCount = std::min(recentCount + 1, static_cast<int>(recent.size()));

    // ---- counters -> per-sample deltas (the first sample only establishes the baseline)
    if (havePrev) {
        const auto& c = in.counters;
        s.clip = toInt(delta(c.clip, prev.clip));
        s.limiter = toInt(delta(c.limiter, prev.limiter));
        s.panic = toInt(delta(c.panic, prev.panic));
        s.xrun = toInt(delta(c.xrun, prev.xrun));
        const int echo = toInt(delta(c.echo, prev.echo)), rev = toInt(delta(c.reverse, prev.reverse)), frz = toInt(delta(c.freeze, prev.freeze));
        s.fx = echo + rev + frz;
        if (echo > 0) fx_.push_back({ in.t, FxEvent::EchoThrow, echo });
        if (rev > 0) fx_.push_back({ in.t, FxEvent::ReverseTrigger, rev });
        if (frz > 0) fx_.push_back({ in.t, FxEvent::Freeze, frz });
        capEvents(fx_, cfg.maxEvents * 4);
    }
    prev = in.counters;
    havePrev = true;
    s.scene = pendingScene;
    pendingScene = 0;

    // ---- feedback events: a candidate that was not present (within 1/6 octave) in the previous sample
    std::array<float, 8> now {};
    int nowCount = 0;
    const int nObs = std::min(in.feedbackCount, static_cast<int>(in.feedback.size()));
    for (int i = 0; i < nObs; ++i) {
        const auto& o = in.feedback[static_cast<size_t>(i)];
        if (!(o.severity >= 0.5f) || !(o.frequencyHz > 0.0f) || !std::isfinite(o.frequencyHz) || nowCount >= static_cast<int>(now.size())) continue;
        now[static_cast<size_t>(nowCount++)] = o.frequencyHz;
        bool known = false;
        for (int k = 0; k < activeFbCount && !known; ++k)
            known = std::fabs(std::log2(static_cast<double>(o.frequencyHz) / std::max(1.0f, activeFb[static_cast<size_t>(k)]))) <= 1.0 / 6.0;
        if (!known) {
            feedback_.push_back({ in.t, o.frequencyHz, std::min(o.severity, 1.0f) });
            ++s.feedback;
            ++res.newFeedback;
        }
    }
    capEvents(feedback_, cfg.maxEvents);
    activeFb = now;
    activeFbCount = nowCount;

    // ---- peak detection on the raw 1 Hz crowd level
    const auto pr = detector.push(in.t, s.crowdDb);
    if (pr.accepted) {
        peaks_.push_back(*pr.accepted);
        res.newPeak = true;
        res.peak = *pr.accepted;
        if (peaks_.size() > cfg.maxPeaks) {   // drop the least prominent (never the one just added)
            auto worst = std::min_element(peaks_.begin(), peaks_.end() - 1,
                                          [](const PeakEvent& a, const PeakEvent& b) { return a.prominenceDb < b.prominenceDb; });
            peaks_.erase(worst);
        }
    }
    if (pr.suppressed) { suppressed_.push_back(pr.suppressedAt); capEvents(suppressed_, cfg.maxEvents); }

    // ---- bounded series
    acc.add(s);
    if (acc.n >= step) flushBucket();
    return res;
}

void EnergyTracker::noteSceneRecall(double t, const std::string& name) {
    scenes_.push_back({ t, name });
    capEvents(scenes_, cfg.maxEvents);
    ++pendingScene;
}

void EnergyTracker::noteMarker(double t, const std::string& label) {
    markers_.push_back({ t, label });
    capEvents(markers_, cfg.maxEvents);
}

int EnergyTracker::peakCountSince(double t0) const noexcept {
    int n = 0;
    for (const auto& p : peaks_) if (p.t >= t0) ++n;
    return n;
}

std::size_t EnergyTracker::memoryBytes() const noexcept {
    return series_.capacity() * sizeof(Sample) + peaks_.capacity() * sizeof(PeakEvent) + suppressed_.capacity() * sizeof(double)
         + feedback_.capacity() * sizeof(FeedbackEvent) + scenes_.capacity() * sizeof(SceneEvent) + markers_.capacity() * sizeof(MarkerEvent)
         + fx_.capacity() * sizeof(FxEvent);
}

void EnergyTracker::recentCrowdDb(float* out, int count) const noexcept {
    const int cap = static_cast<int>(recent.size());
    for (int i = 0; i < count; ++i) {
        const int back = count - 1 - i;   // 0 = newest
        out[i] = back < recentCount ? recent[static_cast<size_t>(((recentPos - 1 - back) % cap + cap) % cap)] : kFloorDb;
    }
}

ReportData EnergyTracker::buildReport(const ReportMeta& meta, double t0, double t1, bool includePending) const {
    ReportData d;
    d.meta = meta;
    d.stepSeconds = static_cast<double>(step);
    d.meta.peakProminenceDb = static_cast<double>(cfg.peak.prominenceDb);
    d.meta.peakMinSpacingSeconds = static_cast<double>(cfg.peak.minSpacingSec);
    auto& sm = d.summary;
    if (!haveFirst) return d;

    // ---- series slice (+ the unflushed bucket as a partial sample)
    struct Row { Sample s; double weight; };
    std::vector<Row> rows;
    for (const auto& s : series_) if (s.t >= t0 - 1e-9 && s.t <= t1 + 1e-9) rows.push_back({ s, static_cast<double>(step) });
    if (acc.n > 0) {
        const Sample p = acc.finish();
        if (p.t >= t0 - 1e-9 && p.t <= t1 + 1e-9) rows.push_back({ p, static_cast<double>(acc.n) });
    }
    double windowEnd = t0;
    for (const auto& r : rows) windowEnd = std::max(windowEnd, r.s.t + r.weight);
    windowEnd = std::min(windowEnd, std::max(t1, t0));
    sm.durationSeconds = std::max(0.0, windowEnd - t0);

    double sumCrowd = 0.0, sumLufs = 0.0;
    int nCrowd = 0, nLufs = 0;
    for (const auto& r : rows) {
        Sample s = r.s;
        sm.clipEvents += s.clip; sm.limiterEvents += s.limiter; sm.feedbackEvents += s.feedback;
        sm.panicEvents += s.panic; sm.xruns += s.xrun; sm.sceneRecalls += s.scene;
        if (s.recording) sm.recordedSeconds += r.weight;
        if (s.crowdDb >= kActiveFloorDb) { sumCrowd += dbToPower(s.crowdDb); ++nCrowd; }
        if (s.lufs >= kActiveFloorDb) { sumLufs += dbToPower(s.lufs); ++nLufs; }
        if (s.crowdDb > sm.peakCrowdDb) { sm.peakCrowdDb = s.crowdDb; sm.peakCrowdAtSeconds = s.t - t0; }
        sm.maxLufs = std::max(sm.maxLufs, s.lufs);
        sm.maxMasterPeakDb = std::max(sm.maxMasterPeakDb, s.masterPeakDb);
        s.t -= t0;
        d.series.push_back(s);
    }
    sm.averageCrowdDb = nCrowd > 0 ? powerToDb(sumCrowd / nCrowd) : kFloorDb;
    sm.meanLufs = nLufs > 0 ? powerToDb(sumLufs / nLufs) : kFloorDb;

    // ---- events
    for (auto p : peaks_) {
        if (p.t < t0 - 1e-9 || p.t > t1 + 1e-9) continue;
        p.t -= t0; p.confirmedAt -= t0;
        d.peaks.push_back(p);
    }
    if (includePending)
        if (auto p = detector.pendingPeak(); p && p->t >= t0 - 1e-9 && p->t <= t1 + 1e-9) {
            p->t -= t0; p->confirmedAt -= t0;
            d.peaks.push_back(*p);
        }
    for (const double t : suppressed_) if (t >= t0 - 1e-9 && t <= t1 + 1e-9) ++sm.peaksSuppressed;
    sm.peakCount = static_cast<int>(d.peaks.size());
    for (const auto& p : d.peaks) {
        if (p.levelDb > sm.peakCrowdDb) { sm.peakCrowdDb = p.levelDb; sm.peakCrowdAtSeconds = p.t; }
    }

    std::vector<PeakEvent> ranked = d.peaks;
    std::stable_sort(ranked.begin(), ranked.end(), [](const PeakEvent& a, const PeakEvent& b) {
        if (a.prominenceDb != b.prominenceDb) return a.prominenceDb > b.prominenceDb;
        return a.t < b.t;
    });
    for (std::size_t i = 0; i < ranked.size() && i < 5; ++i) d.topMoments.push_back({ static_cast<int>(i) + 1, ranked[i] });

    for (const auto& e : scenes_) {
        if (e.t < t0 - 1e-9 || e.t > t1 + 1e-9) continue;
        d.sceneEvents.push_back({ e.t - t0, e.name });
        auto it = std::find_if(d.scenes.begin(), d.scenes.end(), [&](const SceneUse& u) { return u.name == e.name; });
        if (it == d.scenes.end()) d.scenes.push_back({ e.name, 1, e.t - t0 });
        else ++it->recalls;
    }
    for (auto f : feedback_) if (f.t >= t0 - 1e-9 && f.t <= t1 + 1e-9) { f.t -= t0; d.feedbackEvents.push_back(f); }
    for (auto m : markers_) if (m.t >= t0 - 1e-9 && m.t <= t1 + 1e-9) { m.t -= t0; d.markers.push_back(m); }
    for (const auto& e : fx_) {
        if (e.t < t0 - 1e-9 || e.t > t1 + 1e-9) continue;
        if (e.kind == FxEvent::EchoThrow) sm.fx.echoThrows += e.count;
        else if (e.kind == FxEvent::ReverseTrigger) sm.fx.reverseTriggers += e.count;
        else sm.fx.freezes += e.count;
    }
    return d;
}
}
