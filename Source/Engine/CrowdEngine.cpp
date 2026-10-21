#include "CrowdEngine.h"
#include "Engine/Panning.h"
#include <chrono>

namespace crowdmike {
using namespace params;

namespace {
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

    for (auto& c : inputs) c.prepare(sr, maxBlock);
    for (auto& g : groups) g.prepare(sr, maxBlock);
    for (auto& o : outputs) o.prepare(sr, maxBlock);
    fx.prepare(sr, maxBlock);
    master.prepare(sr, maxBlock);
    latencySamples = outputs[0].getLatencySamples();
    bypassDelay.prepare(sr, maxBlock, kMaxInputs, 100.0f);
    density.prepare(sr, maxBlock);
    feedbackDetector.prepare(sr);
    correlation.prepare(sr);
    for (auto& h : inHistory) h.prepare(sr, 4.0f, 160);
    for (auto& h : groupHistoryBuf) h.prepare(sr, 4.0f, 160);
    masterHistoryBuf.prepare(sr, 4.0f, 160);

    for (auto& s : mixL) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    for (auto& s : mixR) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    for (auto& s : inSend) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    for (auto& s : grpSend) { s.prepare(sr, 20.0f); s.snap(0.0f); }
    outGain.prepare(sr, 5.0f);
    outGain.snap(1.0f);
    bypassMix.prepare(sr, 10.0f);
    bypassMix.snap(0.0f);
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
    prepared = true;
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

void CrowdEngine::processChunk(juce::AudioBuffer<float>& host, int offset, int n, const BlockInfo& info) noexcept {
    auto& bank = *services.bank;
    bank.refresh();
    const float* G = bank.effective(Mod::Global);
    const bool panic = G[GlobalP::Panic] >= 0.5f;
    const bool bypass = G[GlobalP::Bypass] >= 0.5f;
    const bool rehearsal = G[GlobalP::Rehearsal] >= 0.5f;
    const bool unlockLimiters = G[GlobalP::UnlockLimiters] >= 0.5f;

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

    // ---- 3. playback clips (-> master / groups / direct bus)
    pbL.clear(n);
    pbR.clear(n);
    if (services.playback != nullptr) {
        float* dl[10];
        float* dr[10];
        for (int k = 0; k < 10; ++k) { dl[k] = pbL.channel(k); dr[k] = pbR.channel(k); }
        services.playback->process(dl, dr, n, panic);
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
    dsp::FxContext ctx;
    ctx.bpm = info.bpm; ctx.hasTempo = info.hasTempo; ctx.ppqPosition = info.ppqPosition; ctx.playing = info.playing;
    ctx.freeze = G[GlobalP::Freeze] >= 0.5f;
    ctx.densityMod = densityNow;
    const float* sendLp[kNumFx]; const float* sendRp[kNumFx]; float* retLp[kNumFx]; float* retRp[kNumFx];
    for (int f = 0; f < kNumFx; ++f) { sendLp[f] = sendL.channel(f); sendRp[f] = sendR.channel(f); retLp[f] = retL.channel(f); retRp[f] = retR.channel(f); }
    fx.process(sendLp, sendRp, preMix.channel(0), preMix.channel(1), retLp, retRp, n, bank, ctx, rampMs);
    for (int f = 0; f < kNumFx; ++f) updatePeak(meters.fxPeak[static_cast<size_t>(f)], retL.channel(f), n);

    // ---- 7. master bus
    float* ml = mastBuf.channel(0);
    float* mr = mastBuf.channel(1);
    for (int s = 0; s < n; ++s) { ml[s] = pbL.channel(0)[s]; mr[s] = pbR.channel(0)[s]; }
    for (int g = 0; g < kMaxGroups; ++g) {
        juce::FloatVectorOperations::add(ml, groupL.channel(g), n);
        juce::FloatVectorOperations::add(mr, groupR.channel(g), n);
    }
    density.process(mastBuf.data(), 2, n);
    for (int f = 0; f < kNumFx; ++f) {
        juce::FloatVectorOperations::add(ml, retL.channel(f), n);
        juce::FloatVectorOperations::add(mr, retR.channel(f), n);
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

    // ---- 8. output matrix: direct inputs + buses
    {
        juce::AudioBuffer<float> src(proc.data(), ins, n);
        juce::AudioBuffer<float> dst(outBuf.data(), numOut, n);
        direct.process(src, dst);
    }
    for (int g = 0; g < kMaxGroups; ++g) {
        busPointers[static_cast<size_t>(RoutingState::busRowForGroup(g, 0))] = groupL.channel(g);
        busPointers[static_cast<size_t>(RoutingState::busRowForGroup(g, 1))] = groupR.channel(g);
    }
    for (int f = 0; f < kNumFx; ++f) {
        busPointers[static_cast<size_t>(RoutingState::busRowForFx(f, 0))] = retL.channel(f);
        busPointers[static_cast<size_t>(RoutingState::busRowForFx(f, 1))] = retR.channel(f);
    }
    busPointers[static_cast<size_t>(RoutingState::busRowForPlayback(0))] = pbL.channel(9);
    busPointers[static_cast<size_t>(RoutingState::busRowForPlayback(1))] = pbR.channel(9);
    busPointers[static_cast<size_t>(RoutingState::busRowForMaster(0))] = ml;
    busPointers[static_cast<size_t>(RoutingState::busRowForMaster(1))] = mr;
    busMatrix.process(busPointers.data(), outBuf.data(), numOut, n);

    // ---- 9. output stage (limiter forced on unless the operator unlocked output limiters)
    const float ceilingDb = G[GlobalP::OutCeiling];
    const float releaseMs = G[GlobalP::OutLimRelease];
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
    }

    // ---- 10. record taps (before panic / rehearsal so recording continues), then analysis feed
    pushTaps(n);
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
    const int writable = host.getNumChannels();
    for (int c = 0; c < writable; ++c) {
        float* dst = host.getWritePointer(c, offset);
        if (c < numOut) std::copy(outBuf.channel(c), outBuf.channel(c) + n, dst);
        else std::fill(dst, dst + n, 0.0f);
    }
}
}
