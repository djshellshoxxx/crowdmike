#include "PresetManager.h"
#include <cstring>

namespace crowdmike::PresetManager {
const char* const kPresetTag = "CrowdMikePreset";

const std::vector<FactoryPreset>& factoryPresets() {
    using S = PresetScope;
    static const std::vector<FactoryPreset> presets = {
        // ---- compressors (EFFECTS_SPEC section 6 crowd presets). Block prefix "comp" works on input/group/master compressors.
        { "Gentle Glue", S::Block, "", "comp", { { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 2 }, { "compAttack", 30 }, { "compRelease", 200 }, { "compKnee", 10 }, { "compMakeup", 0 }, { "compAutoMakeup", 1 }, { "compMix", 1 } } },
        { "Dense Crowd", S::Block, "", "comp", { { "compOn", 1 }, { "compThr", -24 }, { "compRatio", 4 }, { "compAttack", 15 }, { "compRelease", 150 }, { "compKnee", 8 }, { "compMakeup", 0 }, { "compAutoMakeup", 1 }, { "compMix", 1 } } },
        { "Applause Lift", S::Block, "", "comp", { { "compOn", 1 }, { "compThr", -22 }, { "compRatio", 3 }, { "compAttack", 5 }, { "compRelease", 90 }, { "compKnee", 6 }, { "compMakeup", 2 }, { "compAutoMakeup", 0 }, { "compMix", 0.7f } } },
        { "Broadcast Crowd", S::Block, "", "comp", { { "compOn", 1 }, { "compThr", -18 }, { "compRatio", 3 }, { "compAttack", 20 }, { "compRelease", 250 }, { "compKnee", 12 }, { "compMakeup", 0 }, { "compAutoMakeup", 1 }, { "compMix", 1 } } },
        { "Aggressive Hype", S::Block, "", "comp", { { "compOn", 1 }, { "compThr", -30 }, { "compRatio", 8 }, { "compAttack", 3 }, { "compRelease", 70 }, { "compKnee", 3 }, { "compMakeup", 3 }, { "compAutoMakeup", 0 }, { "compMix", 0.6f } } },
        // ---- input cleanup
        { "Crowd HPF 100 Hz", S::Block, "input", "highPass", { { "highPassEnabled", 1 }, { "highPassHz", 100 }, { "highPassSlope", 1 } } },
        { "Stage Rumble Cut 150 Hz", S::Block, "input", "highPass", { { "highPassEnabled", 1 }, { "highPassHz", 150 }, { "highPassSlope", 2 } } },
        { "Hiss Tamer 14 kHz", S::Block, "input", "lowPass", { { "lowPassEnabled", 1 }, { "lowPassHz", 14000 }, { "lowPassSlope", 1 } } },
        { "Quiet Stage Gate", S::Block, "input", "gate", { { "gateOn", 1 }, { "gateThr", -48 }, { "gateRange", -18 }, { "gateRatio", 3 }, { "gateAttack", 5 }, { "gateHold", 80 }, { "gateRelease", 300 }, { "gateHyst", 4 } } },
        { "Whistle Tamer", S::Block, "input", "deess", { { "deessOn", 1 }, { "deessFreq", 4500 }, { "deessThr", -26 }, { "deessRatio", 4 }, { "deessRange", 14 }, { "deessMode", 0 } } },
        { "Shriek Control", S::Block, "input", "deess", { { "deessOn", 1 }, { "deessFreq", 7500 }, { "deessThr", -24 }, { "deessRatio", 3 }, { "deessRange", 10 }, { "deessMode", 0 } } },
        { "Applause Transients", S::Block, "input", "trans", { { "transOn", 1 }, { "transAttack", 4 }, { "transSustain", -1 }, { "transSens", 0.6f }, { "transMix", 1 } } },
        { "Group HPF 90 Hz", S::Block, "group", "hp", { { "hpOn", 1 }, { "hpHz", 90 } } },
        { "Group Air Roll-off 16 kHz", S::Block, "group", "lp", { { "lpOn", 1 }, { "lpHz", 16000 } } },
        // ---- effects
        { "Slapback", S::Effect, "delay", "", { { "mode", 0 }, { "sync", 0 }, { "timeMs", 110 }, { "feedback", 0.15f }, { "mix", 0.8f }, { "lpf", 5000 } } },
        { "Ping-pong 1/8 Synced", S::Effect, "delay", "", { { "mode", 1 }, { "sync", 1 }, { "division", 5 }, { "feedback", 0.4f }, { "mix", 1 }, { "lpf", 6500 } } },
        { "Big Room Echo", S::Effect, "delay", "", { { "mode", 0 }, { "sync", 0 }, { "timeMs", 520 }, { "feedback", 0.5f }, { "diffusion", 0.4f }, { "lpf", 4500 } } },
        { "Chant Throw", S::Effect, "echo", "", { { "timeMs", 380 }, { "captureMs", 1500 }, { "repeats", 5 }, { "decay", 0.55f }, { "send", 1 } } },
        { "Quick Slap Throw", S::Effect, "echo", "", { { "timeMs", 140 }, { "captureMs", 400 }, { "repeats", 3 }, { "decay", 0.4f }, { "send", 1 } } },
        { "Vintage Tape", S::Effect, "tape", "", { { "timeMs", 350 }, { "feedback", 0.45f }, { "drive", 0.45f }, { "age", 0.5f }, { "wow", 0.3f }, { "flutter", 0.25f }, { "highCut", 4200 } } },
        { "Clean Tape Echo", S::Effect, "tape", "", { { "timeMs", 280 }, { "feedback", 0.3f }, { "drive", 0.15f }, { "age", 0.1f }, { "wow", 0.05f }, { "flutter", 0.05f }, { "highCut", 8000 } } },
        { "4 s Reverse Swell", S::Effect, "reverse", "", { { "mode", 0 }, { "chunkSec", 4 }, { "trigMode", 0 }, { "mix", 1 }, { "crossfadeMs", 120 } } },
        { "Rolling Reverse", S::Effect, "reverse", "", { { "mode", 1 }, { "chunkSec", 2 }, { "mix", 0.7f }, { "crossfadeMs", 250 } } },
        { "Subtle Density", S::Effect, "noise", "", { { "amount", 0.12f }, { "color", 1 }, { "follow", 0.8f }, { "lpf", 9000 }, { "width", 0.8f } } },
        { "Stadium Hiss", S::Effect, "noise", "", { { "amount", 0.25f }, { "color", 3 }, { "follow", 0.9f }, { "hpf", 800 }, { "lpf", 14000 }, { "width", 1 } } },
        // ---- master chains
        { "Broadcast Master", S::MasterChain, "master", "", { { "eqOn", 1 }, { "compOn", 1 }, { "compThr", -16 }, { "compRatio", 2 }, { "compAttack", 30 }, { "compRelease", 250 }, { "compAutoMakeup", 1 }, { "limOn", 1 }, { "limCeil", -1.5f } } },
        { "PA Safe Master", S::MasterChain, "master", "", { { "eqOn", 1 }, { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 3 }, { "compAttack", 15 }, { "compRelease", 180 }, { "limOn", 1 }, { "limCeil", -3 } } },
        // ---- appended with FX slots 6 and 7 (additive: the entries above keep their positions)
        { "Arena Hall", S::Effect, "reverb", "", { { "on", 1 }, { "size", 0.75f }, { "decay", 3.2f }, { "preDelayMs", 35 }, { "hfDampHz", 6500 }, { "lowCutHz", 140 }, { "diffusion", 0.8f }, { "modDepth", 0.35f }, { "modRate", 0.5f }, { "width", 1 }, { "wetDb", -4 } } },
        { "Stadium Bowl", S::Effect, "reverb", "", { { "on", 1 }, { "size", 1 }, { "decay", 6.5f }, { "preDelayMs", 60 }, { "hfDampHz", 5000 }, { "lowCutHz", 200 }, { "diffusion", 0.85f }, { "modDepth", 0.4f }, { "modRate", 0.4f }, { "width", 1 }, { "wetDb", -6 } } },
        { "Small Club", S::Effect, "reverb", "", { { "on", 1 }, { "size", 0.25f }, { "decay", 0.9f }, { "preDelayMs", 8 }, { "hfDampHz", 9000 }, { "lowCutHz", 100 }, { "diffusion", 0.7f }, { "modDepth", 0.2f }, { "width", 0.8f }, { "wetDb", -5 } } },
        { "Endless Wash", S::Effect, "reverb", "", { { "on", 1 }, { "size", 1 }, { "decay", 18 }, { "preDelayMs", 40 }, { "hfDampHz", 4000 }, { "lowCutHz", 250 }, { "diffusion", 0.9f }, { "modDepth", 0.5f }, { "modRate", 0.3f }, { "wetDb", -10 } } },
        { "Double The Crowd", S::Effect, "multi", "", { { "on", 1 }, { "voices", 6 }, { "depthMs", 3 }, { "rateHz", 0.7f }, { "detuneCents", 5 }, { "scatter", 0.6f }, { "tone", 0.4f }, { "levelDb", 0 } } },
        { "Packed Terrace", S::Effect, "multi", "", { { "on", 1 }, { "voices", 12 }, { "depthMs", 5 }, { "rateHz", 0.9f }, { "detuneCents", 8 }, { "scatter", 0.8f }, { "tone", 0.6f }, { "levelDb", 0 } } },
        { "Full Stadium", S::Effect, "multi", "", { { "on", 1 }, { "voices", 16 }, { "depthMs", 8 }, { "rateHz", 1.2f }, { "detuneCents", 12 }, { "scatter", 1 }, { "tone", 0.7f }, { "levelDb", -1 } } },
    };
    return presets;
}

std::vector<const FactoryPreset*> factoryPresetsFor(const juce::String& target, const juce::String& blockPrefix) {
    std::vector<const FactoryPreset*> out;
    for (const auto& p : factoryPresets()) {
        const juce::String t(p.target), b(p.blockPrefix);
        if ((t.isEmpty() || t == target) && (blockPrefix.isEmpty() || b == blockPrefix)) out.push_back(&p);
    }
    return out;
}

namespace {
int findSuffix(Mod module, const juce::String& suffix) {
    const auto& mi = ParamBank::info(module);
    if (module == Mod::Macro) return -1;
    for (int i = 0; i < mi.defCount; ++i)
        if (suffix == mi.defs[i].suffix) return i;
    return -1;
}
bool setActual(juce::AudioProcessorValueTreeState& apvts, Mod module, int instance, int paramEnum, float value) {
    const auto id = ParamBank::paramId(module, instance, paramEnum);
    auto* param = apvts.getParameter(id);
    if (param == nullptr || !std::isfinite(value)) return false;
    const float norm = juce::jlimit(0.0f, 1.0f, param->convertTo0to1(value));
    if (std::abs(param->getValue() - norm) < 1.0e-6f) return false;
    param->setValueNotifyingHost(norm);
    return true;
}
}

juce::ValueTree capture(juce::AudioProcessorValueTreeState& apvts, Mod module, int instance, const juce::String& blockPrefix) {
    juce::ValueTree t("Preset");
    t.setProperty("schemaVersion", kPresetSchemaVersion, nullptr);
    t.setProperty("module", static_cast<int>(module), nullptr);
    const auto& mi = ParamBank::info(module);
    for (int i = 0; i < mi.defCount && module != Mod::Macro; ++i) {
        const juce::String suffix(mi.defs[i].suffix);
        if (blockPrefix.isNotEmpty() && !suffix.startsWith(blockPrefix)) continue;
        auto* param = apvts.getParameter(ParamBank::paramId(module, instance, i));
        if (param == nullptr) continue;
        juce::ValueTree p("P");
        p.setProperty("s", suffix, nullptr);
        p.setProperty("v", param->convertFrom0to1(param->getValue()), nullptr);
        t.appendChild(p, nullptr);
    }
    return t;
}

int apply(juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& preset, Mod module, int instance) {
    if (!preset.isValid() || preset.getType().toString() != "Preset") return 0;
    int changed = 0;
    for (auto p : preset) {
        const int e = findSuffix(module, p["s"].toString());
        if (e >= 0 && setActual(apvts, module, instance, e, static_cast<float>(static_cast<double>(p["v"])))) ++changed;
    }
    return changed;
}

int applyFactory(juce::AudioProcessorValueTreeState& apvts, const FactoryPreset& preset, Mod module, int instance) {
    int changed = 0;
    for (const auto& [suffix, value] : preset.values) {
        const int e = findSuffix(module, suffix);
        if (e >= 0 && setActual(apvts, module, instance, e, value)) ++changed;
    }
    return changed;
}

juce::ValueTree captureRouting(const RoutingState& r) {
    juce::ValueTree t("Preset");
    t.setProperty("schemaVersion", kPresetSchemaVersion, nullptr);
    t.setProperty("scope", "routing", nullptr);
    t.appendChild(r.toValueTree(), nullptr);
    return t;
}
bool applyRouting(RoutingState& r, const juce::ValueTree& preset) {
    return preset.isValid() && preset["scope"].toString() == "routing" && r.fromValueTree(preset.getChildWithName("Routing"));
}

juce::ValueTree captureShow(juce::AudioProcessorValueTreeState& apvts, const RoutingState& routing, const ShowModel& show) {
    juce::ValueTree t("Preset");
    t.setProperty("schemaVersion", kPresetSchemaVersion, nullptr);
    t.setProperty("scope", "show", nullptr);
    juce::ValueTree params("Params");
    for (int i = 0; i < ParamBank::totalCount(); ++i) {
        const auto id = ParamBank::idForIndex(i);
        if (auto* p = apvts.getParameter(id)) params.setProperty(id, p->convertFrom0to1(p->getValue()), nullptr);
    }
    t.appendChild(params, nullptr);
    t.appendChild(routing.toValueTree(), nullptr);
    t.appendChild(show.toValueTree(), nullptr);
    return t;
}

bool applyShow(juce::AudioProcessorValueTreeState& apvts, RoutingState& routing, ShowModel& show, const juce::ValueTree& t) {
    if (!t.isValid() || t["scope"].toString() != "show" || static_cast<int>(t["schemaVersion"]) > kPresetSchemaVersion) return false;
    const auto params = t.getChildWithName("Params");
    for (int i = 0; i < ParamBank::totalCount(); ++i) {
        const auto id = ParamBank::idForIndex(i);
        auto* p = apvts.getParameter(id);
        if (p == nullptr || !params.hasProperty(juce::Identifier(id))) continue;
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->convertTo0to1(static_cast<float>(static_cast<double>(params[juce::Identifier(id)])))));
    }
    routing.fromValueTree(t.getChildWithName("Routing"));
    show.fromValueTree(t.getChildWithName("Show"));
    return true;
}

bool saveToFile(const juce::ValueTree& preset, const juce::File& file) {
    if (!preset.isValid()) return false;
    auto xml = preset.createXml();
    if (xml == nullptr) return false;
    xml->setTagName(kPresetTag);
    return file.getParentDirectory().createDirectory().wasOk() && xml->writeTo(file);
}

juce::ValueTree loadFromFile(const juce::File& file) {
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(kPresetTag)) return {};
    if (xml->getIntAttribute("schemaVersion", 0) > kPresetSchemaVersion) return {};
    xml->setTagName("Preset");
    return juce::ValueTree::fromXml(*xml);
}

juce::File userPresetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CrowdMike").getChildFile("Presets");
}
}
