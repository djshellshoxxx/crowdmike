#include "ReverseChunk.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr double kBeatDiv[5] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
constexpr float kLoopFloor = 1.0e-4f;   // -80 dB: repeat loop ends below this

inline float voiceWindow(int k, int len, int xf) noexcept {
    if (k < xf) return std::sin(0.5f * kPi * static_cast<float>(k + 1) / static_cast<float>(xf + 1));
    if (k >= len - xf) return std::sin(0.5f * kPi * static_cast<float>(len - k) / static_cast<float>(xf + 1));
    return 1.0f;
}
}

void ReverseChunk::prepare(double sampleRate, int maxBlockSize) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxLen = static_cast<int>(std::ceil(kMaxChunkSec * sr));
    maxROff = static_cast<int>(std::ceil(0.5 * sr));
    cap = 2 * maxLen + maxROff + std::max(1, maxBlockSize) + 64;
    circL.assign(static_cast<size_t>(cap), 0.0f);
    circR.assign(static_cast<size_t>(cap), 0.0f);
    for (auto& slot : hold)
        for (auto& ch : slot) ch.assign(static_cast<size_t>(maxLen + 1), 0.0f);
    mixAmt.prepare(sr, 20.0f); srcBlend.prepare(sr, 20.0f); bypassGain.prepare(sr, 10.0f);
    atkFast = msToCoef(0.5f, sr); relFast = msToCoef(30.0f, sr);
    atkSlow = msToCoef(50.0f, sr); relSlow = msToCoef(300.0f, sr);
    reset();
}

void ReverseChunk::resetState() noexcept {
    wIdx = 0; wTotal = 0; filled = 0;
    for (auto& v : voices) v = Voice {};
    envFast = envSlow = 0.0f;
    refractory = 0; contCounter = 1; repCounter = 1;
    lastBeatPos = 0.0; beatValid = false;
    pending = false;
    generation = 0;
}

void ReverseChunk::reset() noexcept {
    resetState();
    prevTrig = false;
    prevMode = prevTrigMode = -1;
    fresh = true; cleared = true;
    trigCount.store(0, std::memory_order_relaxed);
    lastTrigSample.store(0, std::memory_order_relaxed);
    vWrite.store(0.0f); vPlay.store(0.0f); vStart.store(0.0f); vLen.store(0.0f); vXf.store(0.0f); vWet.store(0.0f);
    vState.store(0); vPending.store(false);
}

float ReverseChunk::readCirc(const std::vector<float>& buf, int64_t absIdx) const noexcept {
    const int64_t age = wTotal - absIdx;   // 1 == most recently written sample
    if (age < 1 || age > filled) return 0.0f;
    int ri = wIdx - static_cast<int>(age);
    if (ri < 0) ri += cap;
    return buf[static_cast<size_t>(ri)];
}

bool ReverseChunk::busy() const noexcept {
    for (const auto& v : voices)
        if (v.active && !v.releasing && v.k < v.len - v.xf) return true;
    return false;
}

void ReverseChunk::releaseAll() noexcept {
    for (auto& v : voices) {
        if (!v.active || v.releasing) continue;
        v.releasing = true;
        v.relPos = 0;
        v.relLen = std::max(1, std::min(v.xf, v.len - v.k));
    }
}

int ReverseChunk::freeHoldSlot() noexcept {
    bool used[2] = { false, false }, live[2] = { false, false };
    for (const auto& v : voices)
        if (v.active && v.kind == 1 && v.holdSlot >= 0) { used[v.holdSlot] = true; live[v.holdSlot] |= !v.releasing; }
    for (int s = 0; s < 2; ++s) if (!used[s]) return s;
    // Both in use: take one that is only fading out (cut it; happens only for two retriggers inside one crossfade).
    int s = !live[0] ? 0 : (!live[1] ? 1 : 0);
    for (auto& v : voices)
        if (v.active && v.kind == 1 && v.holdSlot == s) v.active = false;
    return s;
}

bool ReverseChunk::startVoice(int kind, int holdSlot, int pass, float gain, int64_t start, int len, int xf,
                              int rOff) noexcept {
    int idx = -1;
    for (int i = 0; i < kVoices; ++i) if (!voices[i].active) { idx = i; break; }
    if (idx < 0) {
        // Steal the releasing voice closest to its end, else the oldest.
        int best = -1, bestLeft = 0x7fffffff;
        for (int i = 0; i < kVoices; ++i) {
            const auto& v = voices[i];
            const int left = v.releasing ? v.relLen - v.relPos : 0x7ffffffe - static_cast<int>(generation - v.gen);
            if (left < bestLeft) { bestLeft = left; best = i; }
        }
        idx = best < 0 ? 0 : best;
    }
    Voice& v = voices[idx];
    v = Voice {};
    v.active = true; v.kind = kind; v.holdSlot = holdSlot; v.pass = pass; v.gain = gain;
    v.start = start; v.len = len; v.xf = xf; v.rOff = rOff; v.gen = ++generation;
    v.birth = wTotal;
    return true;
}

bool ReverseChunk::startFresh() noexcept {
    const int64_t avail = filled;
    const int len = static_cast<int>(std::min<int64_t>(lenParam, avail));
    if (len < 16) return false;
    const int xf = std::clamp(xfParam, 1, len / 2);
    const int rOff = link ? 0 : std::min(len / 2, maxROff);
    const int kind = mode == 2 ? 1 : 0;
    const int slot = kind == 1 ? freeHoldSlot() : -1;
    startVoice(kind, slot, 0, 1.0f, wTotal - len, len, xf, rOff);
    trigCount.fetch_add(1, std::memory_order_relaxed);
    lastTrigSample.store(wTotal, std::memory_order_relaxed);
    return true;
}

void ReverseChunk::handleTrigger() noexcept {
    if (mode == 1) return;   // Continuous runs on its own clock
    if (!busy()) { pending = false; startFresh(); return; }
    switch (retrig) {
        case 0: releaseAll(); pending = false; startFresh(); break;    // Restart
        case 1: if (mode == 2) { releaseAll(); pending = false; } break; // Ignore (Repeat: stop the loop)
        default: pending = true; break;                                  // Queue
    }
}

void ReverseChunk::process(const float* sendL, const float* sendR, const float* preL, const float* preR, float* outL,
                           float* outR, int n, const float* p, const FxContext& ctx) noexcept {
    namespace P = params::ReverseP;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, params::ReverseP::defs[i].def, lo, hi); };

    const bool bypass = prm(P::Bypass, 0.0f, 1.0f) > 0.5f;
    mode = static_cast<int>(std::lround(prm(P::Mode, 0.0f, 2.0f)));
    trigMode = static_cast<int>(std::lround(prm(P::TrigMode, 0.0f, 3.0f)));
    retrig = static_cast<int>(std::lround(prm(P::Retrigger, 0.0f, 2.0f)));
    lenParam = std::clamp(static_cast<int>(std::lround(prm(P::ChunkSec, 0.0f, static_cast<float>(kMaxChunkSec)) * sr)),
                          16, maxLen);
    xfParam = std::max(1, static_cast<int>(std::lround(prm(P::Crossfade, 0.0f, 1000.0f) * 0.001 * sr)));
    link = prm(P::StereoLink, 0.0f, 1.0f) > 0.5f;
    decay = prm(P::Decay, 0.0f, 1.0f);
    frozen = ctx.freeze;
    const float mix = prm(P::Mix, 0.0f, 1.0f);
    const float src = prm(P::Source, 0.0f, 1.0f) > 0.5f ? 1.0f : 0.0f;
    const int beatDiv = static_cast<int>(std::lround(prm(P::BeatDiv, 0.0f, 4.0f)));
    const float sens = prm(P::Sens, 0.0f, 1.0f);
    const bool trig = prm(P::Trigger, 0.0f, 1.0f) > 0.5f;

    if (fresh) {
        mixAmt.snap(mix); srcBlend.snap(src); bypassGain.snap(bypass ? 0.0f : 1.0f);
        prevTrig = trig;
        fresh = false;
    } else {
        mixAmt.setTarget(mix); srcBlend.setTarget(src); bypassGain.setTarget(bypass ? 0.0f : 1.0f);
    }
    const bool trigEdge = trig && !prevTrig;
    prevTrig = trig;

    if (bypass && !bypassGain.isSmoothing() && bypassGain.value() == 0.0f) {
        if (!cleared) { resetState(); cleared = true; }
        prevMode = mode; prevTrigMode = trigMode;
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        vState.store(0, std::memory_order_relaxed); vPending.store(false, std::memory_order_relaxed);
        vWet.store(0.0f, std::memory_order_relaxed);
        return;
    }
    cleared = false;

    if (mode != prevMode) {
        if (prevMode >= 0) releaseAll();
        pending = false;
        contCounter = 1;
        prevMode = mode;
    }
    if (trigMode != prevTrigMode) { repCounter = 1; beatValid = false; prevTrigMode = trigMode; }

    // Beat-sync setup (per block).
    const bool beatOn = trigMode == 3 && ctx.hasTempo && ctx.playing && std::isfinite(ctx.bpm) && ctx.bpm > 1.0
                        && std::isfinite(ctx.ppqPosition);
    const double bps = beatOn ? ctx.bpm / 60.0 / sr : 0.0;
    const double div = kBeatDiv[std::clamp(beatDiv, 0, 4)];
    if (beatOn) {
        const double pos0 = ctx.ppqPosition;
        const bool jitter = beatValid && pos0 < lastBeatPos && lastBeatPos - pos0 < 0.01;
        if (!beatValid || (!jitter && (pos0 < lastBeatPos || pos0 > lastBeatPos + 1.0))) lastBeatPos = pos0 - bps;
        beatValid = true;
    } else {
        beatValid = false;
    }
    const float onsetRatio = 4.0f - 2.8f * sens;
    const float onsetFloor = dbToGain(-24.0f - 36.0f * sens);
    const int refractorySamples = static_cast<int>(0.15 * sr);
    const bool noPre = preL == nullptr || preR == nullptr;

    for (int i = 0; i < n; ++i) {
        const float sb = srcBlend.next();
        const float sL = sanitize(sendL != nullptr ? sendL[i] : 0.0f), sR = sanitize(sendR != nullptr ? sendR[i] : 0.0f);
        float cL = sL, cR = sR;
        if (!noPre && sb > 0.0f) {
            cL = sL + sb * (sanitize(preL[i]) - sL);
            cR = sR + sb * (sanitize(preR[i]) - sR);
        }

        // ---- trigger sources (act before this sample is captured)
        bool fire = trigEdge && i == 0;
        if (mode != 1) {
            if (trigMode == 1) {
                if (--repCounter <= 0) {
                    if (filled >= lenParam) { fire = true; repCounter = std::max(1, lenParam - std::clamp(xfParam, 1, lenParam / 2)); }
                    else repCounter = static_cast<int>(lenParam - filled);
                }
            } else if (trigMode == 2) {
                const float x = std::fabs(0.5f * (cL + cR));
                envFast = x + (x > envFast ? atkFast : relFast) * (envFast - x);
                envSlow = x + (x > envSlow ? atkSlow : relSlow) * (envSlow - x);
                if (refractory > 0) --refractory;
                else if (envFast > onsetFloor && envFast > envSlow * onsetRatio) { fire = true; refractory = refractorySamples; }
            } else if (beatOn) {
                const double pos = ctx.ppqPosition + static_cast<double>(i) * bps;
                if (pos > lastBeatPos && std::floor(pos / div) > std::floor(lastBeatPos / div)) fire = true;
                if (pos > lastBeatPos) lastBeatPos = pos;
            }
        } else {
            if (--contCounter <= 0) {
                if (filled >= lenParam && startFresh()) {
                    const int len = lenParam;
                    contCounter = std::max(1, len - std::clamp(xfParam, 1, len / 2));
                } else {
                    contCounter = static_cast<int>(std::max<int64_t>(1, lenParam - filled));
                }
            }
        }
        if (fire) handleTrigger();

        // ---- capture
        circL[static_cast<size_t>(wIdx)] = cL;
        circR[static_cast<size_t>(wIdx)] = cR;
        if (++wIdx >= cap) wIdx = 0;
        ++wTotal;
        if (filled < cap) ++filled;

        // ---- voices
        float accL = 0.0f, accR = 0.0f;
        for (int vi = 0; vi < kVoices; ++vi) {
            Voice& v = voices[vi];
            if (!v.active || v.birth == wTotal) continue;   // spawned during this sample: starts next sample
            float yL, yR;
            if (v.kind == 1 && v.pass > 0) {
                yL = hold[v.holdSlot][0][static_cast<size_t>(v.k)];
                yR = hold[v.holdSlot][1][static_cast<size_t>(v.k)];
            } else {
                // NB: wTotal already includes this sample; chunk data are all older, ages >= 1.
                const int64_t idx = v.start + v.len - 1 - v.k;
                yL = readCirc(circL, idx);
                yR = readCirc(circR, idx - v.rOff);
                if (v.kind == 1) {
                    hold[v.holdSlot][0][static_cast<size_t>(v.k)] = yL;
                    hold[v.holdSlot][1][static_cast<size_t>(v.k)] = yR;
                }
            }
            float g = v.gain * voiceWindow(v.k, v.len, v.xf);
            if (v.releasing) g *= std::cos(0.5f * kPi * static_cast<float>(v.relPos) / static_cast<float>(v.relLen));
            accL += yL * g;
            accR += yR * g;

            ++v.k;
            if (v.releasing) {
                if (++v.relPos >= v.relLen) { v.active = false; continue; }
            } else if (v.k == v.len - v.xf) {
                // Reached the final fade: chain the next pass / queued trigger so it overlaps by xf.
                if (pending && mode != 1) {
                    pending = false;
                    startFresh();
                } else if (v.kind == 1 && mode == 2) {
                    const float ng = frozen ? v.gain : v.gain * decay;
                    if (ng >= kLoopFloor)
                        startVoice(1, v.holdSlot, v.pass + 1, ng, v.start, v.len, v.xf, v.rOff);
                }
            }
            if (v.k >= v.len) v.active = false;
        }

        const float g = mixAmt.next() * bypassGain.next();
        outL[i] = accL * g;
        outR[i] = accR * g;
    }

    // ---- GUI snapshot
    const float invCap = 1.0f / static_cast<float>(cap);
    int main = -1, activeCount = 0;
    for (int i = 0; i < kVoices; ++i) {
        if (!voices[i].active) continue;
        ++activeCount;
        if (main < 0 || (voices[main].releasing && !voices[i].releasing)
            || (voices[main].releasing == voices[i].releasing && voices[i].gen > voices[main].gen))
            main = i;
    }
    vWrite.store(static_cast<float>(wIdx) * invCap, std::memory_order_relaxed);
    vPending.store(pending, std::memory_order_relaxed);
    if (main >= 0) {
        const Voice& v = voices[main];
        // Absolute index -> the circular-buffer slot it occupies (the write index tracks wTotal modulo cap).
        auto norm = [this, invCap](int64_t abs) {
            const int64_t slot = (static_cast<int64_t>(wIdx) - (wTotal - abs)) % cap;
            return static_cast<float>(slot < 0 ? slot + cap : slot) * invCap;
        };
        vPlay.store(norm(v.start + v.len - 1 - std::min(v.k, v.len - 1)), std::memory_order_relaxed);
        vStart.store(norm(v.start), std::memory_order_relaxed);
        vLen.store(static_cast<float>(v.len) * invCap, std::memory_order_relaxed);
        vXf.store(static_cast<float>(v.xf) * invCap, std::memory_order_relaxed);
        const bool fading = activeCount > 1 || v.releasing || v.k < v.xf || v.k >= v.len - v.xf;
        vState.store(fading ? 3 : 2, std::memory_order_relaxed);
        vWet.store(mixAmt.value() * bypassGain.value() * v.gain, std::memory_order_relaxed);
    } else {
        const bool armed = pending || (mode == 1 && filled < lenParam);
        vState.store(armed ? 1 : 0, std::memory_order_relaxed);
        vXf.store(static_cast<float>(std::min(xfParam, lenParam / 2)) * invCap, std::memory_order_relaxed);
        vWet.store(0.0f, std::memory_order_relaxed);
    }
}

ReverseChunk::Viz ReverseChunk::getViz() const noexcept {
    Viz z {};
    z.writeHead01 = vWrite.load(std::memory_order_relaxed);
    z.playHead01 = vPlay.load(std::memory_order_relaxed);
    z.storedStart01 = vStart.load(std::memory_order_relaxed);
    z.storedLen01 = vLen.load(std::memory_order_relaxed);
    z.state = vState.load(std::memory_order_relaxed);
    z.pending = vPending.load(std::memory_order_relaxed);
    z.crossfade01 = vXf.load(std::memory_order_relaxed);
    z.wet01 = vWet.load(std::memory_order_relaxed);
    return z;
}
}
