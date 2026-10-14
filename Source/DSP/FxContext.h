#pragma once
// FxContext: per-block host/engine context for the time-based send/return effects (docs/MODULES.md).
//
// * bpm / hasTempo / ppqPosition / playing come from the host playhead (hasTempo == false => modules fall back to
//   their millisecond / manual settings).
// * freeze mirrors the global "Freeze / Hold" parameter (GlobalP::Freeze).
// * densityMod is the engine's crowd-density send modulation; the effect modules ignore it (the engine applies it to
//   the send level before calling process()).
// Plain aggregate, trivially copyable, safe to build on the audio thread every block.

namespace crowdmike::dsp {
struct FxContext {
    double bpm = 120.0;
    bool hasTempo = false;
    double ppqPosition = 0.0;
    bool playing = false;
    bool freeze = false;
    float densityMod = 0.0f;
};
}
