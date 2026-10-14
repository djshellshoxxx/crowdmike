#include "Limiter.h"
#include "DspUtil.h"
#include "../Core/ParamTables.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace crowdmike::dsp {
using namespace crowdmike::params;
namespace {
int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }
}

void Limiter::prepare(double sampleRate, int /*maxBlock*/, int channels, float lookaheadMs) {
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels = std::max(0, channels);
    const float la = util::isFiniteBits(lookaheadMs) ? clampf(lookaheadMs, 0.0f, 50.0f) : 0.0f;
    latency = static_cast<int>(std::lround(la * 0.001 * sr));
    window = latency + 1;
    dqSize = nextPow2(window + 1);
    delaySize = nextPow2(latency + 1);
    const auto nc = static_cast<size_t>(numChannels);
    delay.assign(nc * static_cast<size_t>(delaySize), 0.0f);
    drvRing.assign(static_cast<size_t>(delaySize), 1.0f);
    box.assign(nc * static_cast<size_t>(window), 1.0f);
    dqVal.assign(nc * static_cast<size_t>(dqSize), 1.0f);
    dqIdx.assign(nc * static_cast<size_t>(dqSize), 0);
    dqHead.assign(nc, 0);
    dqTail.assign(nc, 0);   // used as element count
    boxSum.assign(nc, 0.0);
    env.assign(nc, 1.0f);
    sDriveDb.prepare(sr, 20.0f);
    sOn.prepare(sr, 10.0f);
    reset();
}

void Limiter::resetGainPaths() noexcept {
    std::fill(box.begin(), box.end(), 1.0f);
    std::fill(dqHead.begin(), dqHead.end(), 0);
    std::fill(dqTail.begin(), dqTail.end(), 0);
    std::fill(boxSum.begin(), boxSum.end(), static_cast<double>(window));
    std::fill(env.begin(), env.end(), 1.0f);
    boxPos = 0;
}

void Limiter::reset() noexcept {
    std::fill(delay.begin(), delay.end(), 0.0f);
    std::fill(drvRing.begin(), drvRing.end(), 1.0f);
    resetGainPaths();
    writePos = 0;
    t = 0;
    first = true;
    settledOff = true;
    relMs = -1.0f;
    sOn.snap(0.0f);
    sDriveDb.snap(0.0f);
    grMeter.store(0.0f, std::memory_order_relaxed);
}

float Limiter::gainStep(size_t cs, float req, std::int64_t idx, double invW) noexcept {
    // exact sliding minimum over the last `window` samples (monotonic deque)
    const int qMask = dqSize - 1;
    float* qv = dqVal.data() + cs * static_cast<size_t>(dqSize);
    std::int64_t* qi = dqIdx.data() + cs * static_cast<size_t>(dqSize);
    int& head = dqHead[cs];
    int& count = dqTail[cs];
    while (count > 0 && qv[(head + count - 1) & qMask] >= req) --count;
    qv[(head + count) & qMask] = req;
    qi[(head + count) & qMask] = idx;
    ++count;
    while (qi[head] <= idx - window) { head = (head + 1) & qMask; --count; }
    const float held = qv[head];
    // instant attack, one-pole release (never above `held`)
    float& e = env[cs];
    e = held < e ? held : held + relCoef * (e - held);
    // boxcar average of W samples
    float* bx = box.data() + cs * static_cast<size_t>(window);
    boxSum[cs] += static_cast<double>(e) - static_cast<double>(bx[boxPos]);
    bx[boxPos] = e;
    return std::min(1.0f, static_cast<float>(boxSum[cs] * invW));
}

void Limiter::process(float* const* ch, int nch, int n, const float* p, bool linkChannels) noexcept {
    juce::ScopedNoDenormals noDenormals;
    nch = std::min(nch, numChannels);
    if (ch == nullptr || nch <= 0 || n <= 0) return;

    const bool on = util::readBool(p, LimP::On, false);
    const float ceilDb = util::readParam(p, LimP::Ceiling, -1.0f, -60.0f, 0.0f);
    const float rel = util::readParam(p, LimP::Release, 100.0f, 1.0f, 5000.0f);
    const float driveDb = util::readParam(p, LimP::Drive, 0.0f, -24.0f, 36.0f);
    const float ceil = dbToGain(ceilDb);
    if (!util::same(rel, relMs)) { relMs = rel; relCoef = msToCoef(rel, sr); }

    if (first) {
        first = false;
        sOn.snap(on ? 1.0f : 0.0f);
        sDriveDb.snap(driveDb);
    }
    sOn.setTarget(on ? 1.0f : 0.0f);
    sDriveDb.setTarget(driveDb);
    const int dMask = delaySize - 1;

    if (!on && util::same(sOn.value(), 0.0f) && !sOn.isSmoothing()) {
        if (!settledOff) { settledOff = true; resetGainPaths(); }
        grMeter.store(0.0f, std::memory_order_relaxed);
        if (latency == 0) {
            for (int c = 0; c < nch; ++c) util::cleanBuffer(ch[c], n);
            return;
        }
        for (int i = 0; i < n; ++i) {
            const int rp = (writePos - latency) & dMask;
            drvRing[static_cast<size_t>(writePos)] = dbToGain(sDriveDb.next());   // kept for priming on re-enable
            for (int c = 0; c < nch; ++c) {
                float* dl = delay.data() + static_cast<size_t>(c) * static_cast<size_t>(delaySize);
                dl[writePos] = util::cleanSample(ch[c][i]);
                ch[c][i] = dl[rp];
            }
            writePos = (writePos + 1) & dMask;
            ++t;
        }
        return;
    }
    const double invW = 1.0 / window;
    auto requiredGain = [ceil](float a) noexcept { return a > ceil ? ceil / a : 1.0f; };
    if (settledOff) {
        // Re-enable: run the gain path over the L samples already in the delay line, so the lookahead is complete
        // from the first output sample (no un-analysed samples in flight).
        settledOff = false;
        for (int k = latency; k >= 1; --k) {
            const int pos = (writePos - k) & dMask;
            const float drv = drvRing[static_cast<size_t>(pos)];
            float linkedPeak = 0.0f;
            for (int c = 0; c < nch; ++c)
                linkedPeak = std::max(linkedPeak, std::fabs(delay[static_cast<size_t>(c) * static_cast<size_t>(delaySize) + static_cast<size_t>(pos)] * drv));
            for (int c = 0; c < nch; ++c) {
                const float a = linkChannels ? linkedPeak
                                             : std::fabs(delay[static_cast<size_t>(c) * static_cast<size_t>(delaySize) + static_cast<size_t>(pos)] * drv);
                gainStep(static_cast<size_t>(c), requiredGain(a), t - k, invW);
            }
            boxPos = boxPos + 1 == window ? 0 : boxPos + 1;
        }
    }

    float minG = 1.0f;
    float driveGain = dbToGain(sDriveDb.value());
    for (int i = 0; i < n; ++i) {
        if (sDriveDb.isSmoothing()) driveGain = dbToGain(sDriveDb.next());
        const float w = sOn.next();
        drvRing[static_cast<size_t>(writePos)] = driveGain;
        float linkedPeak = 0.0f;
        for (int c = 0; c < nch; ++c) {
            const float x = util::cleanSample(ch[c][i]);
            delay[static_cast<size_t>(c) * static_cast<size_t>(delaySize) + static_cast<size_t>(writePos)] = x;
            linkedPeak = std::max(linkedPeak, std::fabs(x * driveGain));
        }
        const int rp = (writePos - latency) & dMask;
        const float drvD = drvRing[static_cast<size_t>(rp)];
        for (int c = 0; c < nch; ++c) {
            const auto cs = static_cast<size_t>(c);
            const float* dl = delay.data() + cs * static_cast<size_t>(delaySize);
            const float a = linkChannels ? linkedPeak : std::fabs(dl[writePos] * driveGain);
            const float g = gainStep(cs, requiredGain(a), t, invW);
            minG = std::min(minG, g);
            const float dry = dl[rp];
            const float y = clampf(dry * drvD * g, -ceil, ceil);
            ch[c][i] = dry + w * (y - dry);
        }
        boxPos = boxPos + 1 == window ? 0 : boxPos + 1;
        writePos = (writePos + 1) & dMask;
        ++t;
    }
    grMeter.store(gainToDb(minG), std::memory_order_relaxed);
}
}
