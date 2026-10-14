#include "EchoThrow.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {

void EchoThrow::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    capCapacity = static_cast<int>(std::ceil(kMaxCaptureMs * 0.001 * sr)) + 16;
    for (int s = 0; s < kSlots; ++s) {
        capL[s].assign(static_cast<size_t>(capCapacity), 0.0f);
        capR[s].assign(static_cast<size_t>(capCapacity), 0.0f);
    }
    sendAmt.prepare(sr, 15.0f); decayAmt.prepare(sr, 30.0f); bypassGain.prepare(sr, 10.0f);
    hpHz.prepare(sr, 50.0f); lpHz.prepare(sr, 50.0f);
    reset();
}

void EchoThrow::reset() noexcept {
    for (auto& s : slots) s = Slot {};
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    appliedHp = appliedLp = -1.0f;
    ctrlCounter = 0; idleCount = 0; orderCounter = 0;
    prevTrig = prevKill = prevFreeze = false;
    fresh = true; cleared = true;
    activeThrows.store(0, std::memory_order_relaxed);
    capturing.store(false, std::memory_order_relaxed);
}

void EchoThrow::killAll(float seconds) noexcept {
    const float step = 1.0f / std::max(1.0f, seconds * static_cast<float>(sr));
    for (auto& s : slots)
        if (s.active && (!s.killing || s.killStep < step)) { s.killing = true; s.killStep = step; }
}

void EchoThrow::startThrow(int timeSamples, int capSamples, int repeats) noexcept {
    int freeIdx = -1, live = 0, oldest = -1;
    for (int i = 0; i < kSlots; ++i) {
        if (!slots[i].active) { if (freeIdx < 0) freeIdx = i; continue; }
        if (!slots[i].killing) {
            ++live;
            if (oldest < 0 || slots[i].order < slots[oldest].order) oldest = i;
        }
    }
    if (freeIdx < 0) {
        // All slots busy: replace the quietest (prefer one that is already fading).
        freeIdx = 0;
        for (int i = 1; i < kSlots; ++i)
            if (slots[i].kill < slots[freeIdx].kill) freeIdx = i;
        if (freeIdx == oldest) oldest = -1;
    }
    if (live >= kSlots - 1 && oldest >= 0 && oldest != freeIdx) {
        slots[oldest].killing = true;
        slots[oldest].killStep = 1.0f / std::max(1.0f, 0.010f * static_cast<float>(sr));
    }
    Slot& s = slots[freeIdx];
    s = Slot {};
    s.active = true;
    s.T = std::max(1, timeSamples);
    s.capLen = std::clamp(capSamples, 1, capCapacity);
    s.R = s.Rend = s.capK = std::max(1, repeats);
    s.order = ++orderCounter;
}

void EchoThrow::process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                        const FxContext& ctx) noexcept {
    namespace P = params::EchoP;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, params::EchoP::defs[i].def, lo, hi); };

    const bool bypass = prm(P::Bypass, 0.0f, 1.0f) > 0.5f;
    const float timeMs = prm(P::TimeMs, 1.0f, 4000.0f);
    const float capMs = prm(P::CaptureMs, 1.0f, kMaxCaptureMs);
    const int repeats = static_cast<int>(std::lround(prm(P::Repeats, 1.0f, 16.0f)));
    const float decay = prm(P::Decay, 0.0f, 0.99f);
    const float send = prm(P::Send, 0.0f, 2.0f);
    const float hpf = prm(P::Hpf, 10.0f, 20000.0f);
    const float lpf = prm(P::Lpf, 200.0f, 24000.0f);
    const bool trig = prm(P::Trigger, 0.0f, 1.0f) > 0.5f;
    const bool kill = prm(P::TailKill, 0.0f, 1.0f) > 0.5f;
    const bool frozen = ctx.freeze;

    if (fresh) {
        sendAmt.snap(send); decayAmt.snap(decay); bypassGain.snap(bypass ? 0.0f : 1.0f);
        hpHz.snap(hpf); lpHz.snap(lpf);
        prevTrig = trig; prevKill = kill; prevFreeze = frozen;
        fresh = false;
    } else {
        sendAmt.setTarget(send); decayAmt.setTarget(decay); bypassGain.setTarget(bypass ? 0.0f : 1.0f);
        hpHz.setTarget(hpf); lpHz.setTarget(lpf);
    }

    const bool trigEdge = trig && !prevTrig;
    const bool killEdge = kill && !prevKill;
    prevTrig = trig; prevKill = kill;

    if (bypass && !bypassGain.isSmoothing() && bypassGain.value() == 0.0f) {
        if (!cleared) {
            for (auto& s : slots) s = Slot {};
            hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
            idleCount = 0;
            cleared = true;
        }
        prevFreeze = frozen;
        activeThrows.store(0, std::memory_order_relaxed);
        capturing.store(false, std::memory_order_relaxed);
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    cleared = false;

    if (killEdge) killAll(0.040f);
    if (trigEdge && !bypass)
        startThrow(static_cast<int>(std::lround(timeMs * 0.001 * sr)), static_cast<int>(std::lround(capMs * 0.001 * sr)),
                   repeats);
    if (prevFreeze && !frozen) {
        for (auto& s : slots)
            if (s.active) s.Rend = std::max(s.Rend, static_cast<int>(s.t / s.T));
    }
    prevFreeze = frozen;

    const int fadeBase = std::max(1, static_cast<int>(std::lround(0.005 * sr)));
    const int idleReset = static_cast<int>(0.1 * sr);

    for (int i = 0; i < n; ++i) {
        const float h = hpHz.next(), l = lpHz.next();
        if (ctrlCounter == 0) {
            if (h != appliedHp) { const auto k = fx::fxDesignHighpass(sr, h); hpL.setCoeffs(k); hpR.setCoeffs(k); appliedHp = h; }
            if (l != appliedLp) { const auto k = fx::fxDesignLowpass(sr, l); lpL.setCoeffs(k); lpR.setCoeffs(k); appliedLp = l; }
        }
        ctrlCounter = (ctrlCounter + 1) & 15;

        const float snd = sendAmt.next();
        const float dec = decayAmt.next();
        const float bg = bypassGain.next();
        const float xL = sanitize(inL != nullptr ? inL[i] : 0.0f) * snd;
        const float xR = sanitize(inR != nullptr ? inR[i] : 0.0f) * snd;

        float wetL = 0.0f, wetR = 0.0f;
        bool any = false;
        for (int si = 0; si < kSlots; ++si) {
            Slot& s = slots[si];
            if (!s.active) continue;
            any = true;
            const int64_t t = s.t;
            if (t < s.capLen) {
                const int fl = std::min(fadeBase, std::max(1, s.capLen / 4));
                float env = 1.0f;
                if (t < fl) env = 0.5f - 0.5f * std::cos(kPi * (static_cast<float>(t) + 0.5f) / static_cast<float>(fl));
                else if (t >= s.capLen - fl)
                    env = 0.5f - 0.5f * std::cos(kPi * (static_cast<float>(s.capLen - t) - 0.5f) / static_cast<float>(fl));
                capL[si][static_cast<size_t>(t)] = xL * env;
                capR[si][static_cast<size_t>(t)] = xR * env;
            }
            int64_t kmax = t / s.T;
            int64_t kmin = t < s.capLen ? 1 : (t - s.capLen) / s.T + 1;
            if (!frozen) kmax = std::min<int64_t>(kmax, s.Rend);
            kmin = std::max<int64_t>(kmin, std::max<int64_t>(1, kmax - 15));
            if (kmin <= kmax) {
                float g = kmin <= s.capK ? std::pow(dec, static_cast<float>(kmin - 1))
                                         : std::pow(dec, static_cast<float>(s.capK - 1));
                float sL = 0.0f, sR = 0.0f;
                for (int64_t k = kmin; k <= kmax; ++k) {
                    const size_t idx = static_cast<size_t>(t - k * s.T);
                    sL += g * capL[si][idx];
                    sR += g * capR[si][idx];
                    if (k < s.capK) g *= dec;
                }
                wetL += sL * s.kill;
                wetR += sR * s.kill;
            }
            ++s.t;
            if (s.killing) {
                s.kill -= s.killStep;
                if (s.kill <= 0.0f) { s.active = false; continue; }
            }
            if (!frozen && s.t >= static_cast<int64_t>(s.Rend) * s.T + s.capLen) s.active = false;
        }

        if (any) idleCount = 0;
        else if (idleCount < idleReset && ++idleCount == idleReset) { hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset(); }

        outL[i] = lpL.process(hpL.process(wetL)) * bg;
        outR[i] = lpR.process(hpR.process(wetR)) * bg;
    }

    int act = 0;
    bool cap = false;
    for (const auto& s : slots)
        if (s.active) { ++act; cap = cap || (s.t < s.capLen && !s.killing); }
    activeThrows.store(act, std::memory_order_relaxed);
    capturing.store(cap, std::memory_order_relaxed);
}
}
