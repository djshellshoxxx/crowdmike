#pragma once
// Recorder tap identifiers (RECORDING_SPEC section 1): raw input, processed input, group, FX return, master, playback, output pair.
// Tap ids are persisted (recorder track sources), so ids 0..54 are frozen from the 5-FX releases; the FX returns of slots
// 6 and 7 are APPENDED after the output pairs (FxExtra).
#include <juce_core/juce_core.h>
#include "Core/Limits.h"

namespace crowdmike::TapMap {
enum : int {
    Raw = 0,                              // 16 mono
    Proc = Raw + kMaxInputs,              // 16 mono
    Group = Proc + kMaxInputs,            // 8 stereo
    Fx = Group + kMaxGroups,              // 5 stereo (legacy FX slots 1..5)
    Master = Fx + kNumLegacyFx,           // stereo
    Playback = Master + 1,                // stereo
    OutPair = Playback + 1,               // 8 stereo (channels 2k, 2k+1)
    FxExtra = OutPair + kMaxOutputs / 2,  // (kNumFx - kNumLegacyFx) stereo: FX slots 6, 7 (appended, ids 55, 56)
    Count = FxExtra + (kNumFx - kNumLegacyFx)
};
inline bool isValid(int id) noexcept { return id >= 0 && id < Count; }
inline int channelsFor(int id) noexcept { return id < Group ? 1 : 2; }
inline int forRawInput(int i) noexcept { return Raw + i; }
inline int forProcessedInput(int i) noexcept { return Proc + i; }
inline int forGroup(int g) noexcept { return Group + g; }
inline int forFx(int f) noexcept { return f < kNumLegacyFx ? Fx + f : FxExtra + (f - kNumLegacyFx); }
// FX slot (0-based) of an FX-return tap id, -1 for any other id.
inline int fxSlotOf(int id) noexcept {
    if (id >= Fx && id < Master) return id - Fx;
    if (id >= FxExtra && id < Count) return kNumLegacyFx + (id - FxExtra);
    return -1;
}
inline int forOutputPair(int pair) noexcept { return OutPair + pair; }
inline juce::String defaultName(int id) {
    static const char* fx[] = { "Delay", "Echo", "Tape", "Reverse", "Noise", "Reverb", "Multiplier" };
    if (id >= FxExtra && id < Count) return juce::String(fx[fxSlotOf(id)]) + " Return";
    if (id < Proc) return "Raw In " + juce::String(id - Raw + 1);
    if (id < Group) return "Proc In " + juce::String(id - Proc + 1);
    if (id < Fx) return "Group " + juce::String(id - Group + 1);
    if (id < Master) return juce::String(fx[id - Fx]) + " Return";
    if (id == Master) return "Master";
    if (id == Playback) return "Playback";
    if (id < Count) return "Out " + juce::String((id - OutPair) * 2 + 1) + "-" + juce::String((id - OutPair) * 2 + 2);
    return {};
}
}
