#pragma once
#include <cmath>
#include "DSP/DspCommon.h"

namespace crowdmike {
// Constant-power pan: centre = -3 dB per side, hard sides = unity on one side.
inline void constantPowerPan(float pan, float& left, float& right) noexcept {
    const float a = (dsp::clampf(pan, -1.0f, 1.0f) + 1.0f) * dsp::kPi * 0.25f;
    left = std::cos(a);
    right = std::sin(a);
}
// Balance: attenuates the opposite side only (used for stereo sources / groups).
inline void balanceGains(float pan, float& left, float& right) noexcept {
    const float p = dsp::clampf(pan, -1.0f, 1.0f);
    left = p > 0.0f ? 1.0f - p : 1.0f;
    right = p < 0.0f ? 1.0f + p : 1.0f;
}
}
