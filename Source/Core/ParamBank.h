#pragma once
// Builds the AudioProcessorValueTreeState layout from ParamTables.h and gives the audio thread a flat, block-coherent
// "effective" value array (raw host values with macro modulation applied). DSP modules read `effective(module, instance)`.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Core/Limits.h"
#include "Core/ParamTables.h"

namespace crowdmike {
// Modules added after the first release come AFTER Macro on purpose: global parameter indices and the persisted preset `module`
// value of every earlier module stay unchanged.
enum class Mod : int { Input, Group, Output, Master, Global, FxDelay, FxEcho, FxTape, FxReverse, FxNoise, Macro, Tempo, FxReverb, FxMulti, Count };

struct ModInfo {
    Mod id;
    const params::Def* defs;
    int defCount;
    int instances;
    const char* idPrefix;     // "input" -> "input1." ; empty prefix for single-instance modules
    const char* namePrefix;
    bool numbered;            // append instance number to the prefix
    const char* dotSuffix;    // appended after the number/prefix ("." or "")
};

// One macro destination. Values are normalised parameter positions (0..1).
struct MacroDest {
    int paramIndex = -1;      // global ParamBank index
    float min01 = 0.0f, max01 = 1.0f;
    float curve = 0.0f;       // -1..1, exponent 2^(2*curve) applied to the macro position
    bool invert = false;
    bool bipolar = false;     // true: modulates around the parameter's own value (macro 0.5 = no change)
};
struct MacroMap {
    static constexpr int kMaxDests = 16;
    std::array<MacroDest, kMaxDests> dests {};
    int count = 0;
};
struct MacroSet { std::array<MacroMap, kNumMacros> macros {}; };

class ParamBank {
public:
    static const ModInfo& info(Mod m) noexcept;
    static int totalCount() noexcept;
    static int baseIndex(Mod m, int instance = 0) noexcept;
    static int index(Mod m, int instance, int paramEnum) noexcept { return baseIndex(m, instance) + paramEnum; }
    static juce::String paramId(Mod m, int instance, int paramEnum);
    static const params::Def& def(Mod m, int paramEnum) noexcept { return info(m).defs[paramEnum]; }
    static int indexForId(const juce::String& id);   // -1 when unknown (message-thread only)
    static juce::String idForIndex(int globalIndex);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void attach(juce::AudioProcessorValueTreeState& apvts);

    // Audio thread: copies host values and applies macro modulation. Allocation-free.
    void refresh() noexcept;
    const float* effective(Mod m, int instance = 0) const noexcept { return &eff[static_cast<size_t>(baseIndex(m, instance))]; }
    float effectiveAt(int globalIndex) const noexcept { return eff[static_cast<size_t>(globalIndex)]; }
    float rawAt(int globalIndex) const noexcept { return raw[static_cast<size_t>(globalIndex)]->load(std::memory_order_relaxed); }
    juce::RangedAudioParameter* parameterAt(int globalIndex) const noexcept { return ranged[static_cast<size_t>(globalIndex)]; }

    // Message thread: publish a new macro configuration (lock-free hand-over to the audio thread).
    void publishMacros(const MacroSet& set);
    const MacroSet& currentMacros() const noexcept { return slots[static_cast<size_t>(activeSlot.load(std::memory_order_acquire))]; }
    float macroValue(int macro) const noexcept { return macroValues[static_cast<size_t>(macro)]; }

private:
    std::vector<std::atomic<float>*> raw;
    std::vector<juce::RangedAudioParameter*> ranged;
    std::vector<float> eff;
    std::array<MacroSet, 4> slots {};
    std::atomic<int> activeSlot { 0 };
    int writeSlot = 1;
    std::array<float, kNumMacros> macroValues {};
};
}
