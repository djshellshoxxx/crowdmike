#include "OscAddress.h"
#include <map>
#include "Core/ParamBank.h"

namespace crowdmike {
namespace {
const char* const kRoot = "crowdmike";

OscAction paramAction(const juce::String& id, const std::vector<float>& args, bool normalised) {
    OscAction a;
    a.index = ParamBank::indexForId(id);
    if (a.index < 0) { a.error = "unknown parameter " + id; return a; }
    const bool momentary = [&] {
        const juce::String s = ParamBank::idForIndex(a.index);
        return s.endsWith(".trigger") || s.endsWith(".tailKill");
    }();
    if (args.empty()) {
        // A bare message on a trigger is a pulse; on a toggle it presses it.
        a.kind = momentary ? OscAction::Kind::Pulse : (normalised ? OscAction::Kind::SetParamNorm : OscAction::Kind::SetParam);
        a.hasValue = !momentary;
        a.value = 1.0f;
        return a;
    }
    a.kind = momentary ? OscAction::Kind::Pulse : (normalised ? OscAction::Kind::SetParamNorm : OscAction::Kind::SetParam);
    a.hasValue = true;
    a.value = args[0];
    if (momentary && args[0] <= 0.0f) a.kind = OscAction::Kind::None;   // release of a momentary button
    return a;
}

bool parseIndex(const juce::String& s, int maxCount, int& zeroBased) {
    if (!s.containsOnly("0123456789") || s.isEmpty()) return false;
    const int v = s.getIntValue();
    if (v < 1 || v > maxCount) return false;
    zeroBased = v - 1;
    return true;
}
}

juce::String oscAliasSuffix(const juce::String& moduleKey, const juce::String& name) {
    static const std::map<juce::String, juce::String> common = {
        { "trim", "trimDb" }, { "level", "levelDb" }, { "pol", "polarity" }, { "delay", "delayMs" },
        { "hpf", "highPassHz" }, { "lpf", "lowPassHz" }, { "ceiling", "limiterCeilingDb" }, { "align", "alignDelayMs" } };
    if (moduleKey == "group" || moduleKey == "output") {
        if (name == "hpf") return "hpHz";
        if (name == "lpf") return "lpHz";
    }
    if (moduleKey == "master" && name == "level") return "levelDb";
    if (moduleKey == "fx") return name;
    const auto it = common.find(name);
    return it == common.end() ? name : it->second;
}

OscAction resolveOsc(const juce::String& address, const std::vector<float>& args) {
    OscAction act;
    auto parts = juce::StringArray::fromTokens(address, "/", "");
    parts.removeEmptyStrings();
    if (parts.size() < 2 || parts[0] != kRoot) return act;
    const auto& p1 = parts[1];

    if (p1 == "panic") {
        act.kind = OscAction::Kind::Panic;
        act.hasValue = true;
        act.value = args.empty() ? 1.0f : (args[0] >= 0.5f ? 1.0f : 0.0f);
        return act;
    }
    if (p1 == "param" && parts.size() >= 3) return paramAction(juce::StringArray(parts.begin() + 2, parts.size() - 2).joinIntoString("/"), args, false);
    if (p1 == "paramnorm" && parts.size() >= 3) return paramAction(juce::StringArray(parts.begin() + 2, parts.size() - 2).joinIntoString("/"), args, true);

    if (p1 == "scene") {
        if (parts.size() == 3 && parts[2] == "next") { act.kind = OscAction::Kind::SceneNext; return act; }
        if (parts.size() == 3 && (parts[2] == "prev" || parts[2] == "previous")) { act.kind = OscAction::Kind::ScenePrev; return act; }
        int idx = 0;
        if (parts.size() == 4 && parseIndex(parts[2], kNumScenes, idx) && parts[3] == "select") { act.kind = OscAction::Kind::SceneSelect; act.index = idx; return act; }
        if (parts.size() == 3 && parts[2] == "scope") {
            if (args.empty() || !std::isfinite(args[0])) { act.error = "scope needs a mask (1..255)"; return act; }
            act.kind = OscAction::Kind::SceneScopeSet;
            act.index = static_cast<int>(std::lround(args[0]));
            return act;
        }
        if (parts.size() == 4 && parts[2] == "scope") {
            static const char* const names[] = { "inputs", "groups", "fx", "master", "outputs", "routing", "names", "macros", "all" };
            for (int b = 0; b < 9; ++b) {
                if (parts[3].equalsIgnoreCase(names[b])) {
                    act.kind = OscAction::Kind::SceneScopeBit;
                    act.index = b;
                    act.hasValue = !args.empty();
                    act.value = args.empty() ? 0.0f : args[0];
                    return act;
                }
            }
            act.error = "bad scope name";
            return act;
        }
        act.error = "bad scene address";
        return act;
    }
    if (p1 == "tempo" && parts.size() == 3) {
        const auto& what = parts[2];
        if (what == "tap" || what == "resync" || what == "downbeat") {
            if (!args.empty() && args[0] <= 0.0f) return act;   // button release
            act.kind = what == "tap" ? OscAction::Kind::TempoTap : OscAction::Kind::TempoResync;
            return act;
        }
        static const std::map<juce::String, juce::String> ids = {
            { "bpm", "tempo.manualBpm" }, { "source", "tempo.source" }, { "quantise", "tempo.quantise" },
            { "quantize", "tempo.quantise" }, { "nudge", "tempo.nudgeBpm" }, { "autoSource", "tempo.autoSource" },
            { "barBeats", "tempo.barBeats" } };
        const auto it = ids.find(what);
        if (it == ids.end()) { act.error = "bad tempo address"; return act; }
        return paramAction(it->second, args, false);
    }
    if (p1 == "record" && parts.size() == 3) {
        if (parts[2] == "start") act.kind = OscAction::Kind::RecordStart;
        else if (parts[2] == "stop") act.kind = OscAction::Kind::RecordStop;
        else if (parts[2] == "toggle") act.kind = OscAction::Kind::RecordToggle;
        else if (parts[2] == "marker") { act.kind = OscAction::Kind::RecordMarker; act.index = args.empty() ? 5 : static_cast<int>(args[0]); }
        else if (parts[2] == "split") act.kind = OscAction::Kind::RecordSplit;
        else act.error = "bad record address";
        return act;
    }
    if (p1 == "clips" && parts.size() == 3 && parts[2] == "stop") { act.kind = OscAction::Kind::StopAllClips; return act; }
    if (p1 == "clip" && parts.size() == 4) {
        int idx = 0;
        if (!parseIndex(parts[2], kMaxClips, idx)) { act.error = "bad clip index"; return act; }
        if (parts[3] == "trigger") act.kind = OscAction::Kind::ClipTrigger;
        else if (parts[3] == "stop") act.kind = OscAction::Kind::ClipStop;
        else { act.error = "bad clip address"; return act; }
        act.index = idx;
        return act;
    }
    if (p1 == "macro" && parts.size() == 3) {
        int idx = 0;
        if (!parseIndex(parts[2], kNumMacros, idx)) { act.error = "bad macro index"; return act; }
        act.kind = OscAction::Kind::Macro;
        act.index = idx;
        act.hasValue = true;
        act.value = args.empty() ? 1.0f : juce::jlimit(0.0f, 1.0f, args[0]);
        return act;
    }

    // Parameter addresses.
    juce::String id;
    int idx = 0;
    if ((p1 == "input" || p1 == "group" || p1 == "output") && parts.size() == 4) {
        const int count = p1 == "input" ? kMaxInputs : (p1 == "group" ? kMaxGroups : kMaxOutputs);
        if (!parseIndex(parts[2], count, idx)) { act.error = "bad " + p1 + " index"; return act; }
        id = (p1 == "output" ? juce::String("out") : p1) + juce::String(idx + 1) + "." + oscAliasSuffix(p1, parts[3]);
    } else if (p1 == "master" && parts.size() == 3) {
        id = "master." + oscAliasSuffix("master", parts[2]);
    } else if (p1 == "fx" && parts.size() == 4) {
        id = "fx." + parts[2] + "." + parts[3];
    } else if (p1 == "global" && parts.size() == 3) {
        id = oscAliasSuffix("global", parts[2]);
    } else {
        return act;
    }
    return paramAction(id, args, false);
}
}
