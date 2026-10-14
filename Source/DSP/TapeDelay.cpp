#include "TapeDelay.h"
#include "Core/ParamTables.h"

namespace crowdmike::dsp {
namespace {
constexpr double kWowHz = 0.6, kFl1Hz = 7.3, kFl2Hz = 11.9;
constexpr float kWowMs = 2.5f, kFlutMs = 0.12f, kDriftMs = 1.5f, kHeadsMs = 20.0f;
constexpr uint32_t kSeed = 0x7A9E5EEDu, kDriftSeed = 0x0DD5EEDu;

inline float sinCycle(double ph) noexcept { return static_cast<float>(std::sin(6.283185307179586 * ph)); }
inline double wrap1(double ph) noexcept { return ph >= 1.0 ? ph - 1.0 : ph; }
}

void TapeDelay::prepare(double sampleRate, int maxBlockSize) {
    (void) maxBlockSize;
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    const float maxMs = kMaxTimeMs + kHeadsMs + kWowMs + kFlutMs + kDriftMs + 10.0f;
    maxDelaySamples = static_cast<int>(std::ceil(maxMs * 0.001 * sr));
    bufL.allocate(maxDelaySamples + 4);
    bufR.allocate(maxDelaySamples + 4);
    slewCoef = 1.0f - msToCoef(150.0f, sr);
    hissFL.setCutoff(sr, 7000.0f); hissFR.setCutoff(sr, 7000.0f);
    driftF1.setCutoff(sr, 0.8f); driftF2.setCutoff(sr, 0.8f);
    for (Smoother* s : { &fbAmt, &drive, &wowAmt, &flutAmt, &ageAmt, &noiseAmt, &mixAmt, &headsAmt })
        s->prepare(sr, 30.0f);
    bypassGain.prepare(sr, 10.0f);
    lowCut.prepare(sr, 50.0f); highCut.prepare(sr, 50.0f);
    reset();
}

void TapeDelay::clearState() noexcept {
    bufL.clear(); bufR.clear();
    hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
    hissFL.reset(); hissFR.reset(); driftF1.reset(); driftF2.reset();
    rng.seed(kSeed); rngDrift.seed(kDriftSeed);
    wowPh = flPh1 = flPh2 = 0.0;
    driftTarget = 0.0f; driftCount = 0;
}

void TapeDelay::reset() noexcept {
    clearState();
    appliedLc = appliedHc = -1.0f;
    ctrlCounter = 0;
    fresh = true; cleared = true;
}

void TapeDelay::process(const float* inL, const float* inR, float* outL, float* outR, int n, const float* p,
                        const FxContext& ctx) noexcept {
    namespace P = params::TapeP;
    (void) ctx;
    if (n <= 0 || outL == nullptr || outR == nullptr) return;
    auto prm = [p](int i, float lo, float hi) { return fx::fxParam(p, i, params::TapeP::defs[i].def, lo, hi); };

    const bool bypass = prm(P::Bypass, 0.0f, 1.0f) > 0.5f;
    const float timeMs = prm(P::TimeMs, 1.0f, kMaxTimeMs);
    const float targetD = std::max(1.0f, static_cast<float>(timeMs * 0.001 * sr));
    const float vFb = prm(P::Feedback, 0.0f, 0.919f);
    const float vDrive = prm(P::Drive, 0.0f, 1.0f), vAge = prm(P::Age, 0.0f, 1.0f);
    const float vWow = prm(P::Wow, 0.0f, 1.0f), vFl = prm(P::Flutter, 0.0f, 1.0f);
    const float vLc = prm(P::LowCut, 10.0f, 5000.0f), vHc = prm(P::HighCut, 500.0f, 24000.0f);
    const float vNoise = prm(P::Noise, 0.0f, 1.0f), vMix = prm(P::Mix, 0.0f, 1.0f), vHeads = prm(P::Heads, 0.0f, 1.0f);

    if (fresh) {
        fbAmt.snap(vFb); drive.snap(vDrive); ageAmt.snap(vAge); wowAmt.snap(vWow); flutAmt.snap(vFl);
        lowCut.snap(vLc); highCut.snap(vHc); noiseAmt.snap(vNoise); mixAmt.snap(vMix); headsAmt.snap(vHeads);
        bypassGain.snap(bypass ? 0.0f : 1.0f);
        delayS = targetD;
        fresh = false;
    } else {
        fbAmt.setTarget(vFb); drive.setTarget(vDrive); ageAmt.setTarget(vAge); wowAmt.setTarget(vWow);
        flutAmt.setTarget(vFl); lowCut.setTarget(vLc); highCut.setTarget(vHc); noiseAmt.setTarget(vNoise);
        mixAmt.setTarget(vMix); headsAmt.setTarget(vHeads); bypassGain.setTarget(bypass ? 0.0f : 1.0f);
    }

    if (bypass && !bypassGain.isSmoothing() && bypassGain.value() == 0.0f) {
        if (!cleared) { clearState(); delayS = targetD; cleared = true; }
        std::fill(outL, outL + n, 0.0f);
        std::fill(outR, outR + n, 0.0f);
        return;
    }
    cleared = false;

    const float msToS = static_cast<float>(0.001 * sr);
    const float maxD = static_cast<float>(maxDelaySamples - 2);
    const int driftPeriod = std::max(1, static_cast<int>(0.25 * sr));

    for (int i = 0; i < n; ++i) {
        const float lc = lowCut.next(), hc = highCut.next(), age = ageAmt.next();
        if (ctrlCounter == 0) {
            if (lc != appliedLc) { const auto k = fx::fxDesignHighpass(sr, lc); hpL.setCoeffs(k); hpR.setCoeffs(k); appliedLc = lc; }
            const float hcEff = hc * (1.0f - 0.55f * age);
            if (hcEff != appliedHc) { const auto k = fx::fxDesignLowpass(sr, hcEff); lpL.setCoeffs(k); lpR.setCoeffs(k); appliedHc = hcEff; }
        }
        ctrlCounter = (ctrlCounter + 1) & 15;

        const float fb = fbAmt.next(), dr = drive.next(), wow = wowAmt.next(), fl = flutAmt.next();
        const float noise = noiseAmt.next(), mix = mixAmt.next(), heads = headsAmt.next(), bg = bypassGain.next();

        // Transport glide.
        delayS += slewCoef * (targetD - delayS);

        // Age drift: new random target every 250 ms, smoothed twice (~0.8 Hz).
        if (driftCount == 0) driftTarget = rngDrift.bipolar();
        if (++driftCount >= driftPeriod) driftCount = 0;
        const float drift = driftF2.lowpass(driftF1.lowpass(driftTarget));

        const float wowL = sinCycle(wowPh), wowR = sinCycle(wrap1(wowPh + 0.25 * heads));
        const float flut = 0.7f * sinCycle(flPh1) + 0.3f * sinCycle(flPh2);
        const float flutR = 0.7f * sinCycle(wrap1(flPh1 + 0.5 * heads)) + 0.3f * sinCycle(flPh2);
        wowPh = wrap1(wowPh + kWowHz / sr);
        flPh1 = wrap1(flPh1 + kFl1Hz / sr);
        flPh2 = wrap1(flPh2 + kFl2Hz / sr);

        const float common = delayS + age * kDriftMs * msToS * drift;
        const float dL = clampf(common + wow * kWowMs * msToS * wowL + fl * kFlutMs * msToS * flut, 1.0f, maxD);
        const float dR = clampf(common + heads * kHeadsMs * msToS + wow * kWowMs * msToS * wowR
                                    + fl * kFlutMs * msToS * flutR, 1.0f, maxD);

        const float yL = bufL.readLinear(dL), yR = bufR.readLinear(dR);
        const float tL = lpL.process(hpL.process(yL));
        const float tR = lpR.process(hpR.process(yR));

        const float g = 1.0f + 3.0f * dr;
        const float xL = sanitize(inL != nullptr ? inL[i] : 0.0f) * bg;
        const float xR = sanitize(inR != nullptr ? inR[i] : 0.0f) * bg;
        // Clamp the tanh argument so huge (finite) inputs cannot produce inf * 0 style issues.
        bufL.push(fx::fxFlush(std::tanh(clampf(g * xL + fb * tL, -50.0f, 50.0f))));
        bufR.push(fx::fxFlush(std::tanh(clampf(g * xR + fb * tR, -50.0f, 50.0f))));

        const float hissLvl = 0.003f * noise + 0.0008f * age;
        const float hL = hissFL.lowpass(rng.bipolar()) * hissLvl;
        const float hR = hissFR.lowpass(rng.bipolar()) * hissLvl;
        const float og = (1.0f + dr) / g * mix * bg;
        outL[i] = tL * og + hL * mix * bg;
        outR[i] = tR * og + hR * mix * bg;
    }
}
}
