#pragma once
// Recorder tap identifiers (RECORDING_SPEC section 1): raw input, processed input, group, FX return, master, playback, output pair.
#include <juce_core/juce_core.h>
#include "Core/Limits.h"

namespace crowdmike::TapMap {
enum : int {
    Raw = 0,                              // 16 mono
    Proc = Raw + kMaxInputs,              // 16 mono
    Group = Proc + kMaxInputs,            // 8 stereo
    Fx = Group + kMaxGroups,              // 5 stereo
    Master = Fx + kNumFx,                 // stereo
    Playback = Master + 1,                // stereo
    OutPair = Playback + 1,               // 8 stereo (channels 2k, 2k+1)
    Count = OutPair + kMaxOutputs / 2
};
inline bool isValid(int id) noexcept { return id >= 0 && id < Count; }
inline int channelsFor(int id) noexcept { return id < Group ? 1 : 2; }
inline int forRawInput(int i) noexcept { return Raw + i; }
inline int forProcessedInput(int i) noexcept { return Proc + i; }
inline int forGroup(int g) noexcept { return Group + g; }
inline int forFx(int f) noexcept { return Fx + f; }
inline int forOutputPair(int pair) noexcept { return OutPair + pair; }
inline juce::String defaultName(int id) {
    static const char* fx[] = { "Delay", "Echo", "Tape", "Reverse", "Noise" };
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
