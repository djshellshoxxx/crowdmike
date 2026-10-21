#include "CrowdEngine.h"
#include "Engine/Panning.h"
#include "Core/ParamTables.h"
#include <chrono>

namespace crowdmike {
using namespace params;

namespace {
constexpr int kTempoSourceAuto = 4;   // TempoSource::Auto (Control/TempoHub.h); the engine does not depend on Control
// dst += src * smoothed gain (per-sample ramp only while the gain moves).
inline void addRamped(float* dst, const float* src, dsp::Smoother& g, int n) noexcept {
    if (g.isSmoothing()) { for (int i = 0; i < n; ++i) dst[i] += src[i] * g.next(); }
    else if (g.value() != 0.0f) juce::FloatVectorOperations::addWithMultiply(dst, src, g.value(), n);
}
inline void updatePeak(AtomicPeak& p, const float* x, int n) noexcept {
    float m = 0.0f;
    for (int i = 0; i < n; ++i) m = std::max(m, std::abs(x[i]));
    p.update(m);
}
}

void CrowdEngine::prepare(double sr, int blockSize, int ins, int outs) {
    prepared = false;
    sampleRate = sr;
    maxBlock = std::max(1, blockSize);
    numIn = juce::jlimit(1, kMaxInputs, ins);
    numOut = juce::jlimit(1, kMaxOutputs, outs);

    raw.allocate(kMaxInputs, maxBlock);
    proc.allocate(kMaxInputs, maxBlock);
    preDyn.allocate(kMaxInputs, maxBlock);
    dryBuf.allocate(kMaxInputs, maxBlock);
    groupL.allocate(kMaxGroups, maxBlock);
    groupR.allocate(kMaxGroups, maxBlock);
    sendL.allocate(kNumFx, maxBlock);
    sendR.allocate(kNumFx, maxBlock);
    retL.allocate(kNumFx, maxBlock);
    retR.allocate(kNumFx, maxBlock);
    pbL.allocate(10, maxBlock);
    pbR.allocate(10, maxBlock);
    mastBuf.allocate(2, maxBlock);
    preMix.allocate(2, maxBlock);
    outBuf.allocate(kMaxOutputs, maxBlock);
    duckBuf.allocate(2 * kMaxGroups + 2 * kNumFx, maxBlock);
    duckKey.allocate(1, maxBlock);
    duckGain.allocate(1, maxBlock);

    for (auto& c : inputs) c.prepare(sr, maxBlock);
    for (auto& g : groups) g.prepare(sr, maxBlock);
    for (auto& o : outputs) o.prepare(sr, maxBlock);
    fx.prepare(sr, maxBlock);
    master.prepare(sr, maxBlock);
    latencySamples = outputs[0].getLatencySamples();
    bypassDelay.prepare(sr, maxBlock, kMaxInputs, 100.0f);
    density.prepare(sr, maxBlock);
    ducker.prepare(sr, maxBlock);
    duckDelay.prepare(sr, maxBlock, 2 * kMaxGroups + 2 * kNumFx, 10.0f);
    probeBank.prepare(sr, numIn);
    feedbackDetector.prepare(sr);
    beat.prepare(sr);
    beatMono.assign(static_cast<size_t>(maxBlock), 0.0f);
    beatFeeding = false;
    echoQuant.reset(); reverseQuant.reset(); clipQuant.reset();
    prevEchoTrig = prevReverseTrig = echoSuppress = reverseSuppress = false;
    clipPendingCount = 0;
    correlation.prepare(sr);
    for (auto& h : inHistory) h.prepare(sr, 4.0f, 160);
    for (auto& h : groupHistoryBuf) h.prepare(sr, 4.0f, 160);
    masterHistoryBuf.prepare(sr, 4.0f, 160);
    loudness.prepare(sr);
    edges = {};

    for (auto& s : mixL) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    for (auto& s : mixR) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    for (auto& s : inSend) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    for (auto& s : grpSend) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    outGain.prepare(sr, 5.0f);
    outGain.snap(1.0f);
    bypassMix.prepare(sr, 10.0f);
    bypassMix.snap(0.0f);
    playbackLevel.prepare(sr, 20.0f);
    playbackLevelInit = false;
    busMatrix.prepare(sr, 10.0f);
    direct.resetToIdentity(numIn, numOut);
    direct.prepare(sr);
    lastRoutingRevision = 0xFFFFFFFFu;
    recallSamplesLeft = 0;
    meters.latencySamples.store(latencySamples);
    meters.sampleRate.store(static_cast<int>(sr));
    meters.blockSize.store(maxBlock);
    for (auto& t : trackSource) t.store(-1);
    for (auto& t : trackChannels) t.store(1);
    monitorBus.prepare(sr, maxBlock);
    prepared = true;
}

void CrowdEngine::requestClipTrigger(int slot) noexcept {
    if (slot < 0 || slot >= kMaxClips) return;
    int s1, n1, s2, n2;
    clipFifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 > 0) { clipRing[static_cast<size_t>(s1)] = slot; clipFifo.finishedWrite(1); }
}

void CrowdEngine::setTrackSource(int track, int tapId, int channels) noexcept {
    if (track < 0 || track >= kMaxRecordTracks) return;
    trackChannels[static_cast<size_t>(track)].store(channels);
    trackSource[static_cast<size_t>(track)].store(tapId);
}

void CrowdEngine::syncRouting(float rampMs) noexcept {
    if (services.routing == nullptr) return;
    const auto rev = services.routing->revision();
    if (rev == lastRoutingRevision) return;
    lastRoutingRevision = rev;
    for (int o = 0; o < numOut; ++o)
        for (int i = 0; i < numIn; ++i) direct.setTargetRouteGain(i, o, services.routing->getDirect(i, o));
    busMatrix.syncTargets(*services.routing, numOut, rampMs);
}

void CrowdEngine::process(juce::AudioBuffer<float>& buffer, const BlockInfo& info) noexcept {
    const auto start = std::chrono::steady_clock::now();
    const int total = buffer.getNumSamples();
    if (!prepared || services.bank == nullptr) { buffer.clear(); return; }
    transportTrigger.setHostPlaying(info.playing);
    if (info.hasTempo) lastBpm.store(info.bpm, std::memory_order_relaxed);
    for (int offset = 0; offset < total; offset += maxBlock)
        processChunk(buffer, offset, std::min(maxBlock, total - offset), info);

    const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double budget = static_cast<double>(total) / sampleRate;
    cpuSmoothed = cpuSmoothed * 0.95 + (used / budget) * 0.05;
    meters.cpuLoad.store(static_cast<float>(cpuSmoothed), std::memory_order_relaxed);
    if (used > budget) meters.overruns.fetch_add(1, std::memory_order_relaxed);
    meters.blocks.fetch_add(1, std::memory_order_relaxed);
}

void CrowdEngine::tapPointers(int id, const float* out[2], int& channels) const noexcept {
    out[0] = out[1] = nullptr;
    channels = TapMap::channelsFor(id);
    if (id < TapMap::Proc) out[0] = raw.channel(id - TapMap::Raw);
    else if (id < TapMap::Group) out[0] = proc.channel(id - TapMap::Proc);
    else if (id < TapMap::Fx) { out[0] = groupL.channel(id - TapMap::Group); out[1] = groupR.channel(id - TapMap::Group); }
    else if (id < TapMap::Master) { out[0] = retL.channel(id - TapMap::Fx); out[1] = retR.channel(id - TapMap::Fx); }
    else if (id == TapMap::Master) { out[0] = mastBuf.channel(0); out[1] = mastBuf.channel(1); }
    else if (id == TapMap::Playback) { out[0] = pbL.channel(9); out[1] = pbR.channel(9); }
    else if (id >= TapMap::FxExtra) { const int f = TapMap::fxSlotOf(id); out[0] = retL.channel(f); out[1] = retR.channel(f); }
    else if (id < TapMap::Count) {
        const int pair = id - TapMap::OutPair;
        out[0] = pair * 2 < numOut ? outBuf.channel(pair * 2) : nullptr;
        out[1] = pair * 2 + 1 < numOut ? outBuf.channel(pair * 2 + 1) : nullptr;
    }
}

void CrowdEngine::pushTaps(int n) noexcept {
    auto* rec = services.recorder;
    if (rec == nullptr) return;
    for (int t = 0; t < kMaxRecordTracks; ++t) {
        if (!rec->isTrackActive(t)) continue;
        const int id = trackSource[static_cast<size_t>(t)].load(std::memory_order_relaxed);
        if (!TapMap::isValid(id)) continue;
        const float* ptr[2];
        int ch = 1;
        tapPointers(id, ptr, ch);
        rec->pushTap(t, ptr, std::min(ch, trackChannels[static_cast<size_t>(t)].load(std::memory_order_relaxed)), n);
    }
    rec->advance(n);
}

void CrowdEngine::applyDucker(const float* G, int ins, int n, bool bypass) noexcept {
    namespace D = DuckP;
    const float* dp = G + duckBase();
    const bool userOn = dp[D::On] >= 0.5f;
    const int keyIdx = static_cast<int>(dp[D::Key] + 0.5f) - 1;
    const bool keyValid = keyIdx >= 0 && keyIdx < ins;
    const bool enabled = userOn && !bypass && keyValid;

    // Key: raw host input scaled by the key input's trim only (post-trim, pre-processing).
    float* key = duckKey.channel(0);
    if (keyValid) {
        const float k = dsp::dbToGain(services.bank->effective(Mod::Input, keyIdx)[InputP::Trim]);
        const float* src = raw.channel(keyIdx);
        for (int s = 0; s < n; ++s) key[s] = src[s] * k;
    } else {
        std::fill(key, key + n, 0.0f);
    }
    float* gain = duckGain.channel(0);
    const bool reducing = ducker.process(key, n, dp, enabled, gain);
    meters.duckGrDb.store(ducker.getGainReductionDb(), std::memory_order_relaxed);
    meters.duckKeyPeak.update(ducker.getKeyPeak());
    meters.duckOpen.store(ducker.isOpen(), std::memory_order_relaxed);

    const float lookMs = enabled ? juce::jlimit(0.0f, 10.0f, dp[D::Lookahead]) : 0.0f;
    const bool copyPath = userOn || lookMs > 0.0f || duckDelay.isGliding() || duckDelay.getDelaySamples() > 0;
    if (!copyPath) {
        for (int g = 0; g < kMaxGroups; ++g) { duckGL[static_cast<size_t>(g)] = groupL.channel(g); duckGR[static_cast<size_t>(g)] = groupR.channel(g); }
        for (int f = 0; f < kNumFx; ++f) { duckFL[static_cast<size_t>(f)] = retL.channel(f); duckFR[static_cast<size_t>(f)] = retR.channel(f); }
        return;
    }

    // Copies (never in place: recorder taps and meters keep the pre-duck buses), delayed together by the lookahead.
    for (int g = 0; g < kMaxGroups; ++g) {
        std::copy(groupL.channel(g), groupL.channel(g) + n, duckBuf.channel(2 * g));
        std::copy(groupR.channel(g), groupR.channel(g) + n, duckBuf.channel(2 * g + 1));
    }
    for (int f = 0; f < kNumFx; ++f) {
        std::copy(retL.channel(f), retL.channel(f) + n, duckBuf.channel(2 * kMaxGroups + 2 * f));
        std::copy(retR.channel(f), retR.channel(f) + n, duckBuf.channel(2 * kMaxGroups + 2 * f + 1));
    }
    duckDelay.process(duckBuf.data(), 2 * kMaxGroups + 2 * kNumFx, n, lookMs);

    if (reducing) {
        const int target = static_cast<int>(dp[D::Target] + 0.5f);
        const bool groupsOn = target == DuckTargetGroups || target == DuckTargetBoth;
        const bool fxOn = target == DuckTargetFx || target == DuckTargetBoth;
        const bool excludeKey = dp[D::ExcludeKey] >= 0.5f;
        if (groupsOn)
            for (int g = 0; g < kMaxGroups; ++g) {
                if (excludeKey && keyValid && services.routing->getAssign(keyIdx, g) != 0.0f) continue;
                juce::FloatVectorOperations::multiply(duckBuf.channel(2 * g), gain, n);
                juce::FloatVectorOperations::multiply(duckBuf.channel(2 * g + 1), gain, n);
            }
        if (fxOn)
            for (int f = 0; f < kNumFx; ++f) {
                juce::FloatVectorOperations::multiply(duckBuf.channel(2 * kMaxGroups + 2 * f), gain, n);
                juce::FloatVectorOperations::multiply(duckBuf.channel(2 * kMaxGroups + 2 * f + 1), gain, n);
            }
    }
    for (int g = 0; g < kMaxGroups; ++g) { duckGL[static_cast<size_t>(g)] = duckBuf.channel(2 * g); duckGR[static_cast<size_t>(g)] = duckBuf.channel(2 * g + 1); }
    for (int f = 0; f < kNumFx; ++f) { duckFL[static_cast<size_t>(f)] = duckBuf.channel(2 * kMaxGroups + 2 * f); duckFR[static_cast<size_t>(f)] = duckBuf.channel(2 * kMaxGroups + 2 * f + 1); }
}

// Cue / monitor output: every monitored track's record tap (whether or not it is armed) summed into one stereo pair.
void CrowdEngine::mixMonitor(int n) noexcept {
    if (!monitorBus.isActive()) return;
    const int pair = monitorBus.getPair();
    monitorBus.beginBlock(n);
    for (int t = 0; t < kMaxRecordTracks; ++t) {
        if (!monitorBus.isTrackMonitored(t)) continue;
        const int id = trackSource[static_cast<size_t>(t)].load(std::memory_order_relaxed);
        if (!TapMap::isValid(id) || id == TapMap::forOutputPair(pair)) continue;   // never feed a bus into itself
        const float* ptr[2];
        int ch = 1;
        tapPointers(id, ptr, ch);
        const int use = std::min(ch, trackChannels[static_cast<size_t>(t)].load(std::memory_order_relaxed));
        monitorBus.addSource(ptr[0], use >= 2 ? ptr[1] : nullptr, n);
    }
}

void CrowdEngine::processChunk(juce::AudioBuffer<float>& host, int offset, int n, const BlockInfo& info) noexcept {
    auto& bank = *services.bank;
    bank.refresh();
    const float* G = bank.effective(Mod::Global);
    const bool panic = G[GlobalP::Panic] >= 0.5f;
    const bool bypass = G[GlobalP::Bypass] >= 0.5f;
    const bool rehearsal = G[GlobalP::Rehearsal] >= 0.5f;
    const bool unlockLimiters = G[GlobalP::UnlockLimiters] >= 0.5f;

    // Show Report event counters: rising edges only (the first chunk after prepare() latches without counting).
    {
        const bool echoNow = bank.effective(Mod::FxEcho)[EchoP::Trigger] >= 0.5f;
        const bool freezeNow = G[GlobalP::Freeze] >= 0.5f;
        if (edges.init) {
            if (panic && !edges.panic) meters.events.panicEvents.fetch_add(1, std::memory_order_relaxed);
            if (echoNow && !edges.echo) meters.events.echoThrows.fetch_add(1, std::memory_order_relaxed);
            if (freezeNow && !edges.freeze) meters.events.freezeEvents.fetch_add(1, std::memory_order_relaxed);
        }
        edges.init = true;
        edges.panic = panic;
        edges.echo = echoNow;
        edges.freeze = freezeNow;
    }

    // Scene-recall ramp: stretch every smoother for the duration of the recall.
    if (recallRequested.exchange(false)) {
        activeRecallMs = recallRampMs.load();
        recallSamplesLeft = static_cast<int>(activeRecallMs * 0.001 * sampleRate);
    }
    const float rampMs = recallSamplesLeft > 0 ? activeRecallMs : -1.0f;
    if (recallSamplesLeft > 0) recallSamplesLeft -= n;

    const int hostChannels = host.getNumChannels();
    const int ins = std::min(numIn, hostChannels);

    // ---- 1. host inputs -> raw
    for (int i = 0; i < kMaxInputs; ++i) {
        float* dst = raw.channel(i);
        if (i < ins) std::copy(host.getReadPointer(i, offset), host.getReadPointer(i, offset) + n, dst);
        else std::fill(dst, dst + n, 0.0f);
    }
    // Dry copy for the bypass path (delayed by the plug-in latency so bypass stays time-aligned).
    for (int i = 0; i < ins; ++i) std::copy(raw.channel(i), raw.channel(i) + n, dryBuf.channel(i));
    const float bypassTarget = bypass ? 1.0f : 0.0f;
    bypassMix.setTarget(bypassTarget);
    const bool needDry = bypass || bypassMix.isSmoothing() || bypassMix.value() > 0.0f;
    if (needDry) {
        const float ms = (static_cast<float>(latencySamples) + 0.25f) * 1000.0f / static_cast<float>(sampleRate);
        bypassDelay.process(dryBuf.data(), ins, n, ms);
    }

    if (G[GlobalP::Soundcheck] >= 0.5f && services.soundcheck != nullptr) {
        services.soundcheck->process(raw.data(), ins, n);
        meters.soundcheckActive.store(services.soundcheck->isReplacingInputs(), std::memory_order_relaxed);
    } else {
        meters.soundcheckActive.store(false, std::memory_order_relaxed);
    }

    probeBank.process(raw.data(), ins, n);   // Line Check: raw host inputs, pre-trim (one relaxed load when disarmed)

    syncRouting(rampMs);

    // ---- 2. input chains
    bool anySolo = false;
    for (int i = 0; i < ins; ++i) anySolo = anySolo || bank.effective(Mod::Input, i)[InputP::Solo] >= 0.5f;
    linkLead.fill(false);
    linkFollower.fill(false);
    for (int i = 0; i + 1 < ins; ++i) {
        if (linkFollower[static_cast<size_t>(i)]) continue;
        if (bank.effective(Mod::Input, i)[InputP::Link] >= 0.5f) { linkLead[static_cast<size_t>(i)] = true; linkFollower[static_cast<size_t>(i + 1)] = true; }
    }
    for (int i = 0; i < kMaxInputs; ++i) std::copy(raw.channel(i), raw.channel(i) + n, proc.channel(i));
    for (int i = 0; i < ins; ++i) {
        if (linkFollower[static_cast<size_t>(i)]) continue;
        const float* p = bank.effective(Mod::Input, i);
        const bool lead = linkLead[static_cast<size_t>(i)];
        const bool soloed = p[InputP::Solo] >= 0.5f;
        float* chans[2] = { proc.channel(i), lead ? proc.channel(i + 1) : nullptr };
        float* pre[2] = { preDyn.channel(i), lead ? preDyn.channel(i + 1) : nullptr };
        bool wantPre = p[InputP::FxPre] >= 0.5f;
        if (wantPre) {
            bool any = false;
            for (int f = 0; f < kNumFx; ++f) any = any || p[inputSendParam(f)] > -79.9f;
            wantPre = any;
        }
        inputs[static_cast<size_t>(i)].process(chans, lead ? 2 : 1, n, p, anySolo && !soloed, wantPre ? pre : nullptr);
        if (!wantPre) for (int c = 0; c < (lead ? 2 : 1); ++c) std::copy(chans[c], chans[c] + n, pre[c]);   // post-fader send source
        meters.inGrDb[static_cast<size_t>(i)].store(inputs[static_cast<size_t>(i)].getCompressorGrDb(), std::memory_order_relaxed);
    }
    for (int i = 0; i < ins; ++i) {
        const float* x = proc.channel(i);
        float m = 0.0f;
        for (int s = 0; s < n; ++s) m = std::max(m, std::abs(x[s]));
        meters.inPeak[static_cast<size_t>(i)].update(m);
        if (m >= 1.0f) meters.inClip[static_cast<size_t>(i)].store(true, std::memory_order_relaxed);
        inHistory[static_cast<size_t>(i)].push(x, n);
    }

    // ---- 2b. Beat Sync: block timing for quantised triggers (Echo / Reverse / clips), BEAT_SYNC_SPEC section 7
    const float* TP = bank.effective(Mod::Tempo);
    dsp::QuantiseTiming qt;
    qt.mode = juce::jlimit(0, 3, juce::roundToInt(TP[TempoP::Quantise]));
    qt.barBeats = kBarBeatsTable[juce::jlimit(0, 5, juce::roundToInt(TP[TempoP::BarBeats]))];
    qt.bpm = info.bpm;
    qt.hasTempo = info.hasTempo;
    qt.playing = info.playing;
    qt.sampleRate = sampleRate;
    qt.ppq = info.ppqPosition + (info.hasTempo && std::isfinite(info.bpm) ? static_cast<double>(offset) * info.bpm / 60.0 / sampleRate : 0.0);
    {
        bool request = false;
        for (;;) {
            int s1, n1, s2, n2;
            clipFifo.prepareToRead(1, s1, n1, s2, n2);
            if (n1 <= 0) break;
            const int slot = clipRing[static_cast<size_t>(s1)];
            clipFifo.finishedRead(1);
            request = true;
            if (clipPendingCount < static_cast<int>(clipPending.size())) clipPending[static_cast<size_t>(clipPendingCount++)] = slot;
        }
        const auto r = clipQuant.process(request, qt, n);
        if (r.kind != dsp::QuantiseResult::Kind::None) {   // Passthrough (no quantising) or Fire
            if (services.playback != nullptr)
                for (int k = 0; k < clipPendingCount; ++k) services.playback->triggerFromAudioThread(clipPending[static_cast<size_t>(k)], r.kind == dsp::QuantiseResult::Kind::Fire ? r.offset : 0);
            clipPendingCount = 0;
        }
    }

    // ---- 3. playback clips (-> master / groups / direct bus)
    pbL.clear(n);
    pbR.clear(n);
    if (services.playback != nullptr) {
        float* dl[10];
        float* dr[10];
        for (int k = 0; k < 10; ++k) { dl[k] = pbL.channel(k); dr[k] = pbR.channel(k); }
        const double bps = info.bpm / (60.0 * sampleRate);   // chunk start inside the host block -> its own ppq
        services.playback->setTempo({ info.bpm, info.ppqPosition + static_cast<double>(offset) * bps, info.hasTempo, info.playing });
        services.playback->process(dl, dr, n, panic);
        // Playback level (GlobalP::PlaybackLevel): one smoothed master gain over every playback destination (master, groups, bus).
        const float pbTarget = dsp::dbToGainOrZero(G[GlobalP::PlaybackLevel]);
        if (!playbackLevelInit) { playbackLevel.snap(pbTarget); playbackLevelInit = true; }
        playbackLevel.setTarget(pbTarget, rampMs);
        if (playbackLevel.isSmoothing()) {
            for (int s = 0; s < n; ++s) {
                const float gv = playbackLevel.next();
                for (int k = 0; k < 10; ++k) { dl[k][s] *= gv; dr[k][s] *= gv; }
            }
        } else if (playbackLevel.value() != 1.0f) {
            for (int k = 0; k < 10; ++k) {
                juce::FloatVectorOperations::multiply(dl[k], playbackLevel.value(), n);
                juce::FloatVectorOperations::multiply(dr[k], playbackLevel.value(), n);
            }
        }
    }

    // ---- 4. input -> group mix (+ input FX sends, pre-group mix)
    sendL.clear(n); sendR.clear(n);
    preMix.clear(n);
    const float densityK = G[GlobalP::DensityToSend];
    const float densityNow = density.getDensity();
    const float sendMod = juce::jlimit(0.0f, 2.0f, 1.0f + densityK * (densityNow * 2.0f - 1.0f));

    for (int i = 0; i < ins; ++i) {
        const float* p = bank.effective(Mod::Input, i);
        const size_t si = static_cast<size_t>(i);
        float l, r;
        if (linkLead[si]) { balanceGains(p[InputP::Pan], l, r); r = 0.0f; }
        else if (linkFollower[si]) { balanceGains(bank.effective(Mod::Input, i - 1)[InputP::Pan], l, r); l = 0.0f; }
        else constantPowerPan(p[InputP::Pan], l, r);
        contribL[si] = l;
        contribR[si] = r;
    }
    for (int g = 0; g < kMaxGroups; ++g) {
        float* gl = groupL.channel(g);
        float* gr = groupR.channel(g);
        std::fill(gl, gl + n, 0.0f);
        std::fill(gr, gr + n, 0.0f);
        bool active = false;
        for (int i = 0; i < ins; ++i) {
            const size_t cell = static_cast<size_t>(g * kMaxInputs + i);
            const float a = services.routing->getAssign(i, g);
            mixL[cell].setTarget(a * contribL[static_cast<size_t>(i)], rampMs);
            mixR[cell].setTarget(a * contribR[static_cast<size_t>(i)], rampMs);
            if (a != 0.0f || mixL[cell].isSmoothing() || mixR[cell].isSmoothing() || mixL[cell].value() != 0.0f || mixR[cell].value() != 0.0f) {
                active = true;
                addRamped(gl, proc.channel(i), mixL[cell], n);
                addRamped(gr, proc.channel(i), mixR[cell], n);
            }
        }
        // playback routed to this group (route 1..8)
        const float* pl = pbL.channel(1 + g);
        const float* pr = pbR.channel(1 + g);
        for (int s = 0; s < n; ++s) { gl[s] += pl[s]; gr[s] += pr[s]; }
        groupActive[static_cast<size_t>(g)] = active || services.playback != nullptr;
    }
    for (int i = 0; i < ins; ++i) {
        const float* p = bank.effective(Mod::Input, i);
        const size_t si = static_cast<size_t>(i);
        const float* src = preDyn.channel(i);
        for (int f = 0; f < kNumFx; ++f) {
            auto& s = inSend[si * kNumFx + static_cast<size_t>(f)];
            s.setTarget(dsp::dbToGainOrZero(p[inputSendParam(f)]) * sendMod, rampMs);
            if (!s.isSmoothing() && s.value() == 0.0f) continue;
            // Each input contributes with its pan contribution; a send gain smoother is shared by L and R.
            if (s.isSmoothing()) {
                for (int k = 0; k < n; ++k) {
                    const float gv = s.next();
                    sendL.channel(f)[k] += src[k] * gv * contribL[si];
                    sendR.channel(f)[k] += src[k] * gv * contribR[si];
                }
            } else {
                juce::FloatVectorOperations::addWithMultiply(sendL.channel(f), src, s.value() * contribL[si], n);
                juce::FloatVectorOperations::addWithMultiply(sendR.channel(f), src, s.value() * contribR[si], n);
            }
        }
        // pre-group mix (reverse "pre" source)
        juce::FloatVectorOperations::addWithMultiply(preMix.channel(0), proc.channel(i), contribL[si], n);
        juce::FloatVectorOperations::addWithMultiply(preMix.channel(1), proc.channel(i), contribR[si], n);
    }

    // ---- 5. groups
    bool anyGroupSolo = false;
    for (int g = 0; g < kMaxGroups; ++g) anyGroupSolo = anyGroupSolo || bank.effective(Mod::Group, g)[GroupP::Solo] >= 0.5f;
    for (int g = 0; g < kMaxGroups; ++g) {
        const float* p = bank.effective(Mod::Group, g);
        const bool soloed = p[GroupP::Solo] >= 0.5f;
        groups[static_cast<size_t>(g)].process(groupL.channel(g), groupR.channel(g), n, p, anyGroupSolo && !soloed, rampMs);
        updatePeak(meters.groupPeakL[static_cast<size_t>(g)], groupL.channel(g), n);
        updatePeak(meters.groupPeakR[static_cast<size_t>(g)], groupR.channel(g), n);
        meters.groupGrDb[static_cast<size_t>(g)].store(groups[static_cast<size_t>(g)].getCompressorGrDb(), std::memory_order_relaxed);
        meters.groupLimGrDb[static_cast<size_t>(g)].store(groups[static_cast<size_t>(g)].getLimiterGrDb(), std::memory_order_relaxed);
        groupHistoryBuf[static_cast<size_t>(g)].push(groupL.channel(g), n);
        for (int f = 0; f < kNumFx; ++f) {
            auto& s = grpSend[static_cast<size_t>(g * kNumFx + f)];
            s.setTarget(dsp::dbToGainOrZero(p[groupSendParam(f)]) * sendMod, rampMs);
            float* sl = sendL.channel(f);
            float* sr = sendR.channel(f);
            if (s.isSmoothing()) {
                const float* gl = groupL.channel(g);
                const float* gr = groupR.channel(g);
                for (int k = 0; k < n; ++k) { const float gv = s.next(); sl[k] += gl[k] * gv; sr[k] += gr[k] * gv; }
            } else if (s.value() != 0.0f) {
                juce::FloatVectorOperations::addWithMultiply(sl, groupL.channel(g), s.value(), n);
                juce::FloatVectorOperations::addWithMultiply(sr, groupR.channel(g), s.value(), n);
            }
        }
    }

    // ---- 6. FX
    for (int f = 0; f < kNumFx; ++f) {
        updatePeak(meters.fxSendPeak[static_cast<size_t>(f)], sendL.channel(f), n);
        updatePeak(meters.fxSendPeak[static_cast<size_t>(f)], sendR.channel(f), n);
    }
    dsp::FxContext ctx;
    ctx.bpm = info.bpm; ctx.hasTempo = info.hasTempo; ctx.ppqPosition = qt.ppq; ctx.playing = info.playing;   // qt.ppq already includes this chunk's offset into an oversized host block
    ctx.freeze = G[GlobalP::Freeze] >= 0.5f;
    ctx.densityMod = densityNow;
    const float* sendLp[kNumFx]; const float* sendRp[kNumFx]; float* retLp[kNumFx]; float* retRp[kNumFx];
    for (int f = 0; f < kNumFx; ++f) { sendLp[f] = sendL.channel(f); sendRp[f] = sendR.channel(f); retLp[f] = retL.channel(f); retRp[f] = retR.channel(f); }
    FxTriggerPlan plan;
    plan.panicKillReverb = panic;
    {
        const float* pe = bank.effective(Mod::FxEcho);
        const float* pr = bank.effective(Mod::FxReverse);
        const bool eRaw = pe[EchoP::Trigger] >= 0.5f, rRaw = pr[ReverseP::Trigger] >= 0.5f;
        const bool eEdge = eRaw && !prevEchoTrig, rEdge = rRaw && !prevReverseTrig;
        prevEchoTrig = eRaw; prevReverseTrig = rRaw;
        const auto er = echoQuant.process(eEdge, qt, n), rr = reverseQuant.process(rEdge, qt, n);
        const auto resolve = [](const dsp::QuantiseResult& q, bool raw, bool& suppress, bool& over, int& at) {
            using K = dsp::QuantiseResult::Kind;
            if (q.kind != K::Passthrough) { over = true; at = q.kind == K::Fire ? q.offset : -1; suppress = raw; }
            else if (suppress) {   // quantising just ended while the button is still held: do not re-fire on the stale edge
                if (raw) { over = true; at = -1; } else suppress = false;
            }
        };
        resolve(er, eRaw, echoSuppress, plan.echoOverride, plan.echoFireAt);
        resolve(rr, rRaw, reverseSuppress, plan.reverseOverride, plan.reverseFireAt);
    }
    fx.process(sendLp, sendRp, preMix.channel(0), preMix.channel(1), retLp, retRp, n, bank, ctx, rampMs, &plan);
    for (int f = 0; f < kNumFx; ++f) updatePeak(meters.fxPeak[static_cast<size_t>(f)], retL.channel(f), n);

    // ---- 6b. performer ducker (docs/DUCKER_SPEC.md): ducked copies feed the master and the bus matrix; taps keep the originals
    applyDucker(G, ins, n, bypass);

    // ---- 7. master bus
    float* ml = mastBuf.channel(0);
    float* mr = mastBuf.channel(1);
    for (int s = 0; s < n; ++s) { ml[s] = pbL.channel(0)[s]; mr[s] = pbR.channel(0)[s]; }
    for (int g = 0; g < kMaxGroups; ++g) {
        juce::FloatVectorOperations::add(ml, duckGL[static_cast<size_t>(g)], n);
        juce::FloatVectorOperations::add(mr, duckGR[static_cast<size_t>(g)], n);
    }
    density.process(mastBuf.data(), 2, n);
    {   // Auto tempo: feed the onset analyser from the selected input or the master bus (before the FX returns)
        const bool autoOn = juce::roundToInt(TP[TempoP::Source]) == kTempoSourceAuto;
        if (autoOn && !beatFeeding) beat.reset();
        beatFeeding = autoOn;
        if (autoOn) {
            const int sel = juce::jlimit(0, kMaxInputs, juce::roundToInt(TP[TempoP::AutoSource]));
            float* mono = beatMono.data();
            if (sel >= 1 && sel <= ins) {
                std::copy(proc.channel(sel - 1), proc.channel(sel - 1) + n, mono);
            } else {
                for (int s = 0; s < n; ++s) mono[s] = 0.5f * (ml[s] + mr[s]);
            }
            beat.pushSamples(mono, n);
        }
    }
    for (int f = 0; f < kNumFx; ++f) {
        juce::FloatVectorOperations::add(ml, duckFL[static_cast<size_t>(f)], n);
        juce::FloatVectorOperations::add(mr, duckFR[static_cast<size_t>(f)], n);
    }
    master.process(ml, mr, n, bank.effective(Mod::Master), rampMs);
    updatePeak(meters.masterPeakL, ml, n);
    updatePeak(meters.masterPeakR, mr, n);
    meters.masterGrDb.store(master.getCompressorGrDb(), std::memory_order_relaxed);
    meters.masterLimGrDb.store(master.getLimiterGrDb(), std::memory_order_relaxed);
    meters.intensity.store(densityNow, std::memory_order_relaxed);
    correlation.process(ml, mr, n);
    meters.correlation.store(correlation.getCorrelation(), std::memory_order_relaxed);
    meters.width.store(correlation.getWidth(), std::memory_order_relaxed);
    masterHistoryBuf.push(ml, n);
    // Show Report feeds
    loudness.push(ml, mr, n);
    meters.crowdLevelDb.store(density.getLevelDb(), std::memory_order_relaxed);
    meters.masterRmsDb.store(loudness.getMomentaryRmsDb(), std::memory_order_relaxed);
    meters.shortTermLufs.store(loudness.getShortTermLufs(), std::memory_order_relaxed);
    updatePeak(meters.reportMasterPeak, ml, n);
    updatePeak(meters.reportMasterPeak, mr, n);
    meters.events.reverseTriggers.store(fx.getReverseTriggerCount(), std::memory_order_relaxed);

    // ---- 8. output matrix: direct inputs + buses
    {
        juce::AudioBuffer<float> src(proc.data(), ins, n);
        juce::AudioBuffer<float> dst(outBuf.data(), numOut, n);
        direct.process(src, dst);
    }
    for (int g = 0; g < kMaxGroups; ++g) {
        busPointers[static_cast<size_t>(RoutingState::busRowForGroup(g, 0))] = duckGL[static_cast<size_t>(g)];
        busPointers[static_cast<size_t>(RoutingState::busRowForGroup(g, 1))] = duckGR[static_cast<size_t>(g)];
    }
    for (int f = 0; f < kNumFx; ++f) {
        busPointers[static_cast<size_t>(RoutingState::busRowForFx(f, 0))] = duckFL[static_cast<size_t>(f)];
        busPointers[static_cast<size_t>(RoutingState::busRowForFx(f, 1))] = duckFR[static_cast<size_t>(f)];
    }
    busPointers[static_cast<size_t>(RoutingState::busRowForPlayback(0))] = pbL.channel(9);
    busPointers[static_cast<size_t>(RoutingState::busRowForPlayback(1))] = pbR.channel(9);
    busPointers[static_cast<size_t>(RoutingState::busRowForMaster(0))] = ml;
    busPointers[static_cast<size_t>(RoutingState::busRowForMaster(1))] = mr;
    busMatrix.process(busPointers.data(), outBuf.data(), numOut, n);

    // ---- 9. output stage (limiter forced on unless the operator unlocked output limiters)
    const float ceilingDb = G[GlobalP::OutCeiling];
    const float releaseMs = G[GlobalP::OutLimRelease];
    bool reportClip = false;
    float reportLimGr = std::abs(master.getLimiterGrDb());
    for (int c = 0; c < numOut; ++c) {
        const float* p = bank.effective(Mod::Output, c);
        const bool limOn = !unlockLimiters || p[OutputP::LimOn] >= 0.5f;
        outputs[static_cast<size_t>(c)].process(outBuf.channel(c), n, p, limOn, ceilingDb, releaseMs, rampMs);
        juce::FloatVectorOperations::clip(outBuf.channel(c), outBuf.channel(c), -1.0f, 1.0f, n);   // last-resort protection only
        const float* x = outBuf.channel(c);
        float m = 0.0f;
        for (int s = 0; s < n; ++s) m = std::max(m, std::abs(x[s]));
        meters.outPeak[static_cast<size_t>(c)].update(m);
        if (m >= 1.0f) meters.outClip[static_cast<size_t>(c)].store(true, std::memory_order_relaxed);
        meters.outLimGrDb[static_cast<size_t>(c)].store(outputs[static_cast<size_t>(c)].getLimiterGrDb(), std::memory_order_relaxed);
        reportClip = reportClip || m >= 1.0f;
        reportLimGr = std::max(reportLimGr, std::abs(outputs[static_cast<size_t>(c)].getLimiterGrDb()));
    }
    {   // Show Report episodes: clip = any output reached full scale; limiter = gain reduction above 1 dB (re-arms below 0.3 dB)
        if (reportClip && !edges.clip) meters.events.clipEvents.fetch_add(1, std::memory_order_relaxed);
        edges.clip = reportClip;
        const bool limNow = edges.limiter ? reportLimGr > 0.3f : reportLimGr > 1.0f;
        if (limNow && !edges.limiter) meters.events.limiterEvents.fetch_add(1, std::memory_order_relaxed);
        edges.limiter = limNow;
    }

    // ---- 10. record taps (before panic / rehearsal so recording continues), then analysis feed
    pushTaps(n);
    mixMonitor(n);
    {
        const int tap = feedbackTap.load(std::memory_order_relaxed);
        const float* mono = tap == 0 ? ml : (tap <= 16 ? proc.channel(tap - 1) : groupL.channel(juce::jlimit(0, kMaxGroups - 1, tap - 17)));
        feedbackDetector.pushSamples(mono, n);
    }

    // ---- 11. panic / rehearsal / bypass, then write to the host
    const bool silence = panic || rehearsal;
    outGain.setTarget(silence ? 0.0f : 1.0f, panic ? 5.0f : 20.0f);
    meters.panicActive.store(panic, std::memory_order_relaxed);
    meters.rehearsalActive.store(rehearsal, std::memory_order_relaxed);
    meters.bypassActive.store(bypass, std::memory_order_relaxed);
    for (int s = 0; s < n; ++s) {
        const float g = outGain.next();
        const float b = bypassMix.next();
        for (int c = 0; c < numOut; ++c) {
            const float wet = outBuf.channel(c)[s] * g;
            const float dry = (c < ins) ? dryBuf.channel(c)[s] : 0.0f;
            outBuf.channel(c)[s] = b <= 0.0f ? wet : wet * (1.0f - b) + dry * b;
        }
    }
    if (monitorBus.isActive()) {   // cue pair: added after panic / rehearsal / bypass, hard-limited to 0 dBFS
        const int pair = monitorBus.getPair();
        const int l = pair * 2, r = pair * 2 + 1;
        if (l < numOut) monitorBus.mixInto(outBuf.channel(l), r < numOut ? outBuf.channel(r) : nullptr, n);
    }
    const int writable = host.getNumChannels();
    for (int c = 0; c < writable; ++c) {
        float* dst = host.getWritePointer(c, offset);
        if (c < numOut) std::copy(outBuf.channel(c), outBuf.channel(c) + n, dst);
        else std::fill(dst, dst + n, 0.0f);
    }
}
}
