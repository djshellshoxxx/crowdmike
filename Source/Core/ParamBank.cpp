#include "ParamBank.h"
#include <cmath>

namespace crowdmike {
using namespace params;

namespace {
const ModInfo kInfos[] = {
    { Mod::Input,   InputP::defs,   InputP::Count,   kMaxInputs,  "input", "Input ",   true,  "." },
    { Mod::Group,   GroupP::defs,   GroupP::Count,   kMaxGroups,  "group", "Group ",   true,  "." },
    { Mod::Output,  OutputP::defs,  OutputP::Count,  kMaxOutputs, "out",   "Output ",  true,  "." },
    { Mod::Master,  MasterP::defs,  MasterP::Count,  1,           "master.", "Master ", false, "" },
    { Mod::Global,  GlobalP::defs,  GlobalP::Count,  1,           "",       "",         false, "" },
    { Mod::FxDelay, DelayP::defs,   DelayP::Count,   1,           "fx.delay.",   "",    false, "" },
    { Mod::FxEcho,  EchoP::defs,    EchoP::Count,    1,           "fx.echo.",    "",    false, "" },
    { Mod::FxTape,  TapeP::defs,    TapeP::Count,    1,           "fx.tape.",    "",    false, "" },
    { Mod::FxReverse, ReverseP::defs, ReverseP::Count, 1,         "fx.reverse.", "",    false, "" },
    { Mod::FxNoise, NoiseP::defs,   NoiseP::Count,   1,           "fx.noise.",   "",    false, "" },
    { Mod::Macro,   nullptr,        1,               kNumMacros,  "macro",  "Macro ",   true,  "" },
};
constexpr int kModCount = static_cast<int>(Mod::Count);

struct Offsets {
    int base[kModCount] {};
    int total = 0;
    Offsets() {
        for (int m = 0; m < kModCount; ++m) { base[m] = total; total += kInfos[m].defCount * kInfos[m].instances; }
    }
};
const Offsets& offsets() { static const Offsets o; return o; }

juce::StringArray splitChoices(const char* text) {
    return juce::StringArray::fromTokens(juce::String::fromUTF8(text), "|", "");
}

juce::NormalisableRange<float> makeRange(const Def& d) {
    juce::NormalisableRange<float> range(d.lo, d.hi, d.step > 0.0f ? d.step : 0.0f);
    if (d.mid > d.lo && d.mid < d.hi) range.setSkewForCentre(d.mid);
    return range;
}
}

const ModInfo& ParamBank::info(Mod m) noexcept { return kInfos[static_cast<int>(m)]; }
int ParamBank::totalCount() noexcept { return offsets().total; }
int ParamBank::baseIndex(Mod m, int instance) noexcept {
    return offsets().base[static_cast<int>(m)] + instance * kInfos[static_cast<int>(m)].defCount;
}

juce::String ParamBank::paramId(Mod m, int instance, int paramEnum) {
    const auto& mi = info(m);
    juce::String prefix = juce::String(mi.idPrefix);
    if (mi.numbered) prefix += juce::String(instance + 1);
    prefix += mi.dotSuffix;
    return m == Mod::Macro ? prefix : prefix + mi.defs[paramEnum].suffix;
}

int ParamBank::indexForId(const juce::String& id) {
    static const std::map<juce::String, int> lookup = [] {
        std::map<juce::String, int> m;
        for (int mod = 0; mod < kModCount; ++mod)
            for (int inst = 0; inst < kInfos[mod].instances; ++inst)
                for (int p = 0; p < kInfos[mod].defCount; ++p)
                    m[paramId(static_cast<Mod>(mod), inst, p)] = baseIndex(static_cast<Mod>(mod), inst) + p;
        return m;
    }();
    const auto it = lookup.find(id);
    return it == lookup.end() ? -1 : it->second;
}

juce::String ParamBank::idForIndex(int globalIndex) {
    for (int mod = 0; mod < kModCount; ++mod) {
        const int start = offsets().base[mod];
        const int span = kInfos[mod].defCount * kInfos[mod].instances;
        if (globalIndex >= start && globalIndex < start + span) {
            const int rel = globalIndex - start;
            return paramId(static_cast<Mod>(mod), rel / kInfos[mod].defCount, rel % kInfos[mod].defCount);
        }
    }
    return {};
}

juce::AudioProcessorValueTreeState::ParameterLayout ParamBank::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int mod = 0; mod < kModCount; ++mod) {
        const auto& mi = kInfos[mod];
        for (int inst = 0; inst < mi.instances; ++inst) {
            if (mi.id == Mod::Macro) {
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { paramId(Mod::Macro, inst, 0), 1 }, "Macro " + juce::String(inst + 1),
                    juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
                continue;
            }
            for (int p = 0; p < mi.defCount; ++p) {
                const Def& d = mi.defs[p];
                const juce::ParameterID pid { paramId(mi.id, inst, p), 1 };
                juce::String name = juce::String(mi.namePrefix);
                if (mi.numbered) name += juce::String(inst + 1) + " ";
                name += d.name;
                switch (d.kind) {
                    case Kind::Bool:
                        layout.add(std::make_unique<juce::AudioParameterBool>(pid, name, d.def >= 0.5f));
                        break;
                    case Kind::Choice:
                        layout.add(std::make_unique<juce::AudioParameterChoice>(pid, name, splitChoices(d.choices),
                                                                                 static_cast<int>(d.def)));
                        break;
                    case Kind::Float:
                        layout.add(std::make_unique<juce::AudioParameterFloat>(
                            pid, name, makeRange(d), d.def,
                            juce::AudioParameterFloatAttributes().withLabel(d.unit)));
                        break;
                }
            }
        }
    }
    return layout;
}

void ParamBank::attach(juce::AudioProcessorValueTreeState& apvts) {
    const int n = totalCount();
    raw.assign(static_cast<size_t>(n), nullptr);
    ranged.assign(static_cast<size_t>(n), nullptr);
    eff.assign(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        const auto id = idForIndex(i);
        raw[static_cast<size_t>(i)] = apvts.getRawParameterValue(id);
        ranged[static_cast<size_t>(i)] = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        jassert(raw[static_cast<size_t>(i)] != nullptr && ranged[static_cast<size_t>(i)] != nullptr);
        eff[static_cast<size_t>(i)] = raw[static_cast<size_t>(i)] != nullptr ? raw[static_cast<size_t>(i)]->load() : 0.0f;
    }
    const int macroBase = baseIndex(Mod::Macro, 0);
    (void) macroBase;
}

void ParamBank::refresh() noexcept {
    const size_t n = raw.size();
    for (size_t i = 0; i < n; ++i) eff[i] = raw[i]->load(std::memory_order_relaxed);

    const int macroBase = baseIndex(Mod::Macro, 0);
    for (int m = 0; m < kNumMacros; ++m) macroValues[static_cast<size_t>(m)] = eff[static_cast<size_t>(macroBase + m)];

    const MacroSet& set = slots[static_cast<size_t>(activeSlot.load(std::memory_order_acquire))];
    for (int m = 0; m < kNumMacros; ++m) {
        const MacroMap& map = set.macros[static_cast<size_t>(m)];
        if (map.count == 0) continue;
        const float value = juce::jlimit(0.0f, 1.0f, macroValues[static_cast<size_t>(m)]);
        for (int d = 0; d < map.count; ++d) {
            const MacroDest& dest = map.dests[static_cast<size_t>(d)];
            if (dest.paramIndex < 0 || dest.paramIndex >= static_cast<int>(n)) continue;
            auto* param = ranged[static_cast<size_t>(dest.paramIndex)];
            float normalised;
            if (dest.bipolar) {
                const float base01 = param->convertTo0to1(eff[static_cast<size_t>(dest.paramIndex)]);
                const float swing = (dest.max01 - dest.min01) * 0.5f;
                const float t = (dest.invert ? 1.0f - value : value) * 2.0f - 1.0f;
                normalised = base01 + t * swing;
            } else {
                float t = dest.invert ? 1.0f - value : value;
                if (dest.curve != 0.0f) t = std::pow(t, std::exp2(2.0f * dest.curve));
                normalised = dest.min01 + (dest.max01 - dest.min01) * t;
            }
            normalised = juce::jlimit(0.0f, 1.0f, normalised);
            float actual = param->convertFrom0to1(normalised);
            if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr) actual = normalised >= 0.5f ? 1.0f : 0.0f;
            eff[static_cast<size_t>(dest.paramIndex)] = actual;
        }
    }
}

void ParamBank::publishMacros(const MacroSet& set) {
    slots[static_cast<size_t>(writeSlot)] = set;
    const int previous = activeSlot.exchange(writeSlot, std::memory_order_acq_rel);
    // Rotate through the other slots so the audio thread's slot is never rewritten within the next two publishes.
    writeSlot = (previous + 1) % static_cast<int>(slots.size());
    if (writeSlot == activeSlot.load()) writeSlot = (writeSlot + 1) % static_cast<int>(slots.size());
}
}
