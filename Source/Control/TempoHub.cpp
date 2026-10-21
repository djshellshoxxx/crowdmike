#include "TempoHub.h"
#include <algorithm>
#include <cmath>

namespace crowdmike {
namespace {
inline double finiteOr(double v, double fallback) noexcept { return std::isfinite(v) ? v : fallback; }
inline double clampBpm(double v) noexcept { return std::clamp(finiteOr(v, 120.0), TempoHub::kMinBpm, TempoHub::kMaxBpm); }
}

void TempoHub::prepare(double sampleRate, int) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    clock.prepare(sr);
    eventCount = 0;
    absSamples = 0;
    seenTapSeq = tapSeq.load(std::memory_order_acquire);
    ppqFree = 0.0;
    bpmOut = 120.0;
    autoHad = false;
    lastClockPpq = 0.0;
    lastTrackerNow = -1;
}

bool TempoHub::tap(double nowSec) {
    const bool accepted = taps.tap(nowSec);
    if (taps.hasBpm()) {
        tapBpm.store(clampBpm(taps.bpm()), std::memory_order_relaxed);
        tapHas.store(true, std::memory_order_relaxed);
    }
    if (accepted) tapSeq.fetch_add(1, std::memory_order_acq_rel);
    return tapHas.load(std::memory_order_relaxed);
}

void TempoHub::resetTaps() {
    taps.reset();
    tapHas.store(false, std::memory_order_relaxed);
}

TempoState TempoHub::process(int numSamples, const TempoParams& p, const HostTempoInfo& host) noexcept {
    const int n = std::max(0, numSamples);
    const double blockSec = static_cast<double>(n) / sr;
    TempoState s;
    s.source = std::clamp(p.source, 0, 4);
    s.barBeats = std::clamp(p.barBeats, 1, 16);
    const double nudge = std::clamp(finiteOr(p.nudgeBpm, 0.0), -50.0, 50.0);

    // MIDI clock position at the start of the block must not see this block's own ticks.
    const double clockPpq0 = clock.ppqAt(absSamples);
    for (int i = 0; i < eventCount; ++i) {
        const Event& e = events[static_cast<size_t>(i)];
        const std::int64_t pos = absSamples + std::min(e.offset, std::max(0, n - 1));
        switch (e.type) {
            case Ev::Tick: clock.onTick(pos); break;
            case Ev::Start: clock.onStart(); break;
            case Ev::Continue: clock.onContinue(); break;
            case Ev::Stop: clock.onStop(); break;
        }
    }
    eventCount = 0;

    // Operator requests (message thread): taps align the beat grid, resync restarts the bar.
    const std::uint32_t ts = tapSeq.load(std::memory_order_acquire);
    if (ts != seenTapSeq) { seenTapSeq = ts; ppqFree = std::round(ppqFree); }
    const bool resync = resyncRequest.exchange(false, std::memory_order_acq_rel);
    if (resync) ppqFree = 0.0;

    switch (static_cast<TempoSource>(s.source)) {
        case TempoSource::Host: {
            const bool ok = host.hasBpm && std::isfinite(host.bpm) && host.bpm > 0.0;
            s.hasTempo = ok;
            s.bpm = ok ? clampBpm(host.bpm) : 120.0;
            s.ppq = host.hasPpq && std::isfinite(host.ppq) ? host.ppq : 0.0;
            s.playing = host.playing;
            s.confidence = ok ? 1.0f : 0.0f;
            s.lowConfidence = false;
            if (host.hasPpq && std::isfinite(host.ppq)) ppqFree = host.ppq;
            break;
        }
        case TempoSource::Tap:
        case TempoSource::Manual: {
            const bool tapSrc = static_cast<TempoSource>(s.source) == TempoSource::Tap;
            s.hasTempo = tapSrc ? tapHas.load(std::memory_order_relaxed) : true;
            const double base = tapSrc ? tapBpm.load(std::memory_order_relaxed) : p.manualBpm;
            s.bpm = s.hasTempo ? clampBpm(base + nudge) : 120.0;
            s.ppq = ppqFree;
            s.playing = s.hasTempo;
            s.confidence = s.hasTempo ? 1.0f : 0.0f;
            if (s.hasTempo) ppqFree += static_cast<double>(n) * s.bpm / 60.0 / sr;
            break;
        }
        case TempoSource::MidiClock: {
            const bool stale = clock.hasTempo() && (absSamples - clock.lastTickPos()) > static_cast<std::int64_t>(0.5 * sr);
            s.hasTempo = clock.hasTempo();
            s.bpm = s.hasTempo ? clampBpm(clock.bpm()) : 120.0;
            double q = finiteOr(clockPpq0, 0.0);
            if (clock.isRunning() && q < lastClockPpq && lastClockPpq - q < 2.0 / dsp::MidiClockTempo::kPpqn) q = lastClockPpq;
            lastClockPpq = q;
            s.ppq = q;
            s.playing = s.hasTempo && clock.isRunning() && !stale;
            s.lowConfidence = !s.hasTempo || stale;
            s.confidence = s.lowConfidence ? 0.0f : 1.0f;
            ppqFree = q;
            break;
        }
        case TempoSource::Auto: {
            dsp::BeatEstimate est;
            if (beat != nullptr) est = beat->getEstimate();
            if (est.valid && std::isfinite(est.bpm)) {
                const double target = clampBpm(est.bpm + nudge);
                if (!autoHad) { bpmOut = target; autoHad = true; }
                else bpmOut += (target - bpmOut) * (1.0 - std::exp(-blockSec / 0.3));
            }
            s.hasTempo = autoHad;
            s.bpm = autoHad ? clampBpm(bpmOut) : 120.0;
            s.confidence = std::isfinite(est.confidence) ? std::clamp(est.confidence, 0.0f, 1.0f) : 0.0f;
            s.lowConfidence = !est.valid || est.confidence < kLowConfidence;
            // Phase lock the free-running beat clock to the detected beats (gently, so it never lurches).
            if (beat != nullptr && est.valid && est.confidence >= kLowConfidence && est.periodSamples > 0.0) {
                const std::int64_t nowT = beat->getPushedSamples();
                if (nowT > lastTrackerNow) {   // only while the tracker is actually being fed
                    double phase = std::fmod(static_cast<double>(nowT - est.lastBeatSample) / est.periodSamples, 1.0);
                    if (phase < 0.0) phase += 1.0;
                    double err = phase - (ppqFree - std::floor(ppqFree));
                    err -= std::round(err);
                    ppqFree += err * (1.0 - std::exp(-blockSec / 0.8));
                }
                lastTrackerNow = nowT;
            }
            s.ppq = ppqFree;
            s.playing = s.hasTempo;
            if (s.hasTempo) ppqFree += static_cast<double>(n) * s.bpm / 60.0 / sr;
            break;
        }
    }
    if (!std::isfinite(s.ppq)) s.ppq = 0.0;
    if (!std::isfinite(ppqFree)) ppqFree = 0.0;
    if (!std::isfinite(s.bpm)) s.bpm = 120.0;
    absSamples += n;
    publish(s);
    return s;
}

void TempoHub::publish(const TempoState& s) noexcept {
    sBpm.store(s.bpm, std::memory_order_relaxed);
    sPpq.store(s.ppq, std::memory_order_relaxed);
    sHas.store(s.hasTempo, std::memory_order_relaxed);
    sPlaying.store(s.playing, std::memory_order_relaxed);
    sLow.store(s.lowConfidence, std::memory_order_relaxed);
    sConf.store(s.confidence, std::memory_order_relaxed);
    sSource.store(s.source, std::memory_order_relaxed);
    sBarBeats.store(s.barBeats, std::memory_order_relaxed);
}

TempoState TempoHub::snapshot() const noexcept {
    TempoState s;
    s.bpm = sBpm.load(std::memory_order_relaxed);
    s.ppq = sPpq.load(std::memory_order_relaxed);
    s.hasTempo = sHas.load(std::memory_order_relaxed);
    s.playing = sPlaying.load(std::memory_order_relaxed);
    s.lowConfidence = sLow.load(std::memory_order_relaxed);
    s.confidence = sConf.load(std::memory_order_relaxed);
    s.source = sSource.load(std::memory_order_relaxed);
    s.barBeats = sBarBeats.load(std::memory_order_relaxed);
    return s;
}
}
