#include "SceneBank.h"

namespace crowdmike {
SceneBank::SceneBank(juce::AudioProcessorValueTreeState& a, RoutingState& r, ShowModel& s) : apvts(a), routing(r), show(s) {}

bool SceneBank::isTransientParam(int idx) {
    static const std::vector<int> transient = [] {
        std::vector<int> v;
        const auto add = [&](Mod m, int inst, int p) { v.push_back(ParamBank::index(m, inst, p)); };
        add(Mod::Global, 0, params::GlobalP::Panic);
        add(Mod::Global, 0, params::GlobalP::Bypass);
        add(Mod::Global, 0, params::GlobalP::Rehearsal);
        add(Mod::Global, 0, params::GlobalP::UnlockLimiters);
        add(Mod::Global, 0, params::GlobalP::Freeze);
        add(Mod::Global, 0, params::GlobalP::Soundcheck);
        add(Mod::Global, 0, params::GlobalP::RecordAll);
        add(Mod::Global, 0, params::GlobalP::SceneNext);
        add(Mod::Global, 0, params::GlobalP::ScenePrev);
        add(Mod::FxEcho, 0, params::EchoP::Trigger);
        add(Mod::FxEcho, 0, params::EchoP::TailKill);
        add(Mod::FxReverse, 0, params::ReverseP::Trigger);
        add(Mod::Tempo, 0, params::TempoP::Tap);
        add(Mod::Tempo, 0, params::TempoP::Resync);
        add(Mod::FxReverb, 0, params::ReverbP::Freeze);     // performance holds, like Global Freeze
        add(Mod::FxReverb, 0, params::ReverbP::TailKill);
        return v;
    }();
    return std::find(transient.begin(), transient.end(), idx) != transient.end();
}

unsigned SceneBank::scopeForParam(int idx) {
    for (int m = 0; m < static_cast<int>(Mod::Count); ++m) {
        const Mod mod = static_cast<Mod>(m);
        const int base = ParamBank::baseIndex(mod, 0);
        const auto& mi = ParamBank::info(mod);
        if (idx < base || idx >= base + mi.defCount * mi.instances) continue;
        switch (mod) {
            case Mod::Input: return ScopeInputs;
            case Mod::Group: return ScopeGroups;
            case Mod::Output: return ScopeOutputs;
            case Mod::Master: case Mod::Global: return ScopeMaster;
            case Mod::Macro: return ScopeMacros;
            default: return ScopeFx;
        }
    }
    return 0;
}

void SceneBank::capture(int slot, const juce::String& name) {
    if (slot < 0 || slot >= kNumScenes) return;
    auto& scene = scenes[static_cast<size_t>(slot)];
    if (name.isNotEmpty()) scene.name = name;
    else if (scene.name.isEmpty()) scene.name = "Scene " + juce::String(slot + 1);
    juce::ValueTree data("SceneData");
    juce::ValueTree p("Params");
    const int n = ParamBank::totalCount();
    for (int i = 0; i < n; ++i) {
        if (isTransientParam(i)) continue;
        const auto id = ParamBank::idForIndex(i);
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        if (param == nullptr) continue;
        const float actual = param->convertFrom0to1(param->getValue());
        const float def = param->convertFrom0to1(param->getDefaultValue());
        if (std::abs(actual - def) > 1.0e-5f) p.setProperty(id, actual, nullptr);
    }
    data.appendChild(p, nullptr);
    data.appendChild(routing.toValueTree(), nullptr);
    data.appendChild(show.toValueTree(), nullptr);
    scene.data = data;
    scene.used = true;
    currentSlot = slot;
}

bool SceneBank::recall(int slot, unsigned scope, float rampMs) {
    if (slot < 0 || slot >= kNumScenes) return false;
    auto& scene = scenes[static_cast<size_t>(slot)];
    if (!scene.used || !scene.data.isValid()) return false;
    const float ramp = rampMs > 0.0f ? rampMs : show.ui.recallRampMs;
    if (onRecallRamp) onRecallRamp(ramp);

    const auto params = scene.data.getChildWithName("Params");
    const int n = ParamBank::totalCount();
    for (int i = 0; i < n; ++i) {
        if (isTransientParam(i) || show.isLocked(i)) continue;
        const unsigned bit = scopeForParam(i);
        if ((bit & scope) == 0) continue;
        const auto id = ParamBank::idForIndex(i);
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        if (param == nullptr) continue;
        const juce::Identifier key(id);
        const float target = params.hasProperty(key) ? static_cast<float>(static_cast<double>(params[key]))
                                                     : param->convertFrom0to1(param->getDefaultValue());
        const float norm = param->convertTo0to1(std::isfinite(target) ? target : 0.0f);
        if (std::abs(param->getValue() - norm) > 1.0e-6f) param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
    }
    if (scope & ScopeRouting) {
        const auto r = scene.data.getChildWithName("Routing");
        if (r.isValid()) routing.fromValueTree(r);
    }
    if (scope & (ScopeNames | ScopeMacros)) {
        ShowModel incoming;
        if (incoming.fromValueTree(scene.data.getChildWithName("Show"))) {
            if (scope & ScopeNames) {
                for (int i = 0; i < kMaxInputs; ++i) show.input(i) = incoming.input(i);
                for (int g = 0; g < kMaxGroups; ++g) show.group(g) = incoming.group(g);
                for (int o = 0; o < kMaxOutputs; ++o) show.output(o) = incoming.output(o);
            }
            if (scope & ScopeMacros) { show.macros = incoming.macros; show.macroNames = incoming.macroNames; }
        }
    }
    currentSlot = slot;
    if (onRecalled) onRecalled();
    return true;
}

int SceneBank::countUsed() const {
    int c = 0;
    for (const auto& s : scenes) c += s.used ? 1 : 0;
    return c;
}

bool SceneBank::next(unsigned scope) {
    if (countUsed() == 0) return false;
    for (int step = 1; step <= kNumScenes; ++step) {
        const int slot = (juce::jmax(currentSlot, -1) + step) % kNumScenes;
        if (scenes[static_cast<size_t>(slot)].used) return recall(slot, scope);
    }
    return false;
}

bool SceneBank::previous(unsigned scope) {
    if (countUsed() == 0) return false;
    for (int step = 1; step <= kNumScenes; ++step) {
        const int slot = ((juce::jmax(currentSlot, 0) - step) % kNumScenes + kNumScenes) % kNumScenes;
        if (scenes[static_cast<size_t>(slot)].used) return recall(slot, scope);
    }
    return false;
}

void SceneBank::clear(int slot) {
    if (slot < 0 || slot >= kNumScenes) return;
    scenes[static_cast<size_t>(slot)] = {};
    if (currentSlot == slot) currentSlot = -1;
}

void SceneBank::duplicate(int from, int to) {
    if (from < 0 || from >= kNumScenes || to < 0 || to >= kNumScenes || !scenes[static_cast<size_t>(from)].used) return;
    scenes[static_cast<size_t>(to)] = scenes[static_cast<size_t>(from)];
    scenes[static_cast<size_t>(to)].data = scenes[static_cast<size_t>(from)].data.createCopy();
    scenes[static_cast<size_t>(to)].name += " copy";
}

juce::ValueTree SceneBank::toValueTree() const {
    juce::ValueTree t("Scenes");
    t.setProperty("current", currentSlot, nullptr);
    for (int i = 0; i < kNumScenes; ++i) {
        const auto& s = scenes[static_cast<size_t>(i)];
        if (!s.used) continue;
        juce::ValueTree st("Scene");
        st.setProperty("slot", i, nullptr);
        st.setProperty("name", s.name, nullptr);
        st.setProperty("notes", s.notes, nullptr);
        st.appendChild(s.data.createCopy(), nullptr);
        t.appendChild(st, nullptr);
    }
    return t;
}

bool SceneBank::fromValueTree(const juce::ValueTree& t) {
    if (!t.isValid() || t.getType().toString() != "Scenes") return false;
    for (auto& s : scenes) s = {};
    for (auto st : t) {
        const int slot = st["slot"];
        if (slot < 0 || slot >= kNumScenes) continue;
        auto data = st.getChildWithName("SceneData");
        if (!data.isValid()) continue;
        auto& s = scenes[static_cast<size_t>(slot)];
        s.used = true;
        s.name = st["name"].toString();
        s.notes = st["notes"].toString();
        s.data = data.createCopy();
    }
    currentSlot = juce::jlimit(-1, kNumScenes - 1, static_cast<int>(t["current"]));
    return true;
}
}
