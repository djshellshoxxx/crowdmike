#include "InputChannel.h"

namespace crowdmike {
using namespace params;

void InputChannel::prepare(double sampleRate, int maxBlock) {
    sr = sampleRate;
    strip.prepare(sampleRate, maxBlock, 2);
    eq.prepare(sampleRate, maxBlock, 2, 5);
    phase.prepare(sampleRate, maxBlock, 2);
    align.prepare(sampleRate, maxBlock, 2, 500.0f);
    gate.prepare(sampleRate, maxBlock, 2);
    deess.prepare(sampleRate, maxBlock, 2);
    comp.prepare(sampleRate, maxBlock, 2);
    trans.prepare(sampleRate, maxBlock, 2);
    rider.prepare(sampleRate, maxBlock, 2);
}

void InputChannel::reset() {
    strip.reset();
    eq.reset(); phase.reset(); align.reset(); gate.reset(); deess.reset(); comp.reset(); trans.reset(); rider.reset();
}

void InputChannel::process(float* const* ch, int nch, int n, const float* p, bool mutedBySolo, float* const* preDynamics) noexcept {
    using P = InputP;
    strip.setTrimDb(p[P::Trim]);
    strip.setMuted(p[P::Mute] >= 0.5f || mutedBySolo);
    strip.setPolarityInverted(p[P::Pol] >= 0.5f);
    strip.setHighPassEnabled(p[P::HpOn] >= 0.5f);
    strip.setHighPassHz(p[P::HpHz]);
    strip.setHighPassSlope(static_cast<int>(p[P::HpSlope]));
    strip.setLowPassEnabled(p[P::LpOn] >= 0.5f);
    strip.setLowPassHz(p[P::LpHz]);
    strip.setLowPassSlope(static_cast<int>(p[P::LpSlope]));
    juce::AudioBuffer<float> view(ch, nch, n);
    strip.process(view);

    // Dedicated notch (band 0) + four parametric bands.
    float* b = eqBands;
    b[kEqBandStride * 0 + EqOn] = p[P::NotchOn];
    b[kEqBandStride * 0 + EqType] = static_cast<float>(EqNotch);
    b[kEqBandStride * 0 + EqHz] = p[P::NotchHz];
    b[kEqBandStride * 0 + EqGain] = p[P::NotchDepth];
    b[kEqBandStride * 0 + EqQ] = p[P::NotchQ];
    for (int k = 0; k < 4 * kEqBandStride; ++k) b[kEqBandStride + k] = p[P::Eq1On + k];
    eq.process(ch, nch, n, b, 5);

    phase.process(ch, nch, n, static_cast<int>(p[P::PhaseStages]), p[P::PhaseHz]);
    align.process(ch, nch, n, p[P::AlignMs]);

    if (preDynamics != nullptr)
        for (int c = 0; c < nch; ++c) std::copy(ch[c], ch[c] + n, preDynamics[c]);

    gate.process(ch, nch, n, p + P::GateOn);
    deess.process(ch, nch, n, p + P::DeEssOn);
    comp.process(ch, nch, n, p + P::InCompOn);
    trans.process(ch, nch, n, p + P::TransOn);
    rider.process(ch, nch, n, p[P::Riding] >= 0.5f);
}
}
