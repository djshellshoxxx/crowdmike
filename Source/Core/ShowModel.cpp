#include "ShowModel.h"
#include <algorithm>
#include <cmath>

namespace crowdmike {
namespace {
const char* const kDefaultGroupNames[kMaxGroups] = { "Front Crowd", "Rear Crowd", "Balcony", "Stage/Room", "Broadcast Crowd", "Playback", "", "" };

juce::ValueTree portsToTree(const char* type, int count, const std::function<const PortMeta&(int)>& get) {
    juce::ValueTree t(type);
    for (int i = 0; i < count; ++i) {
        const auto& p = get(i);
        juce::ValueTree c("Port");
        c.setProperty("i", i, nullptr);
        c.setProperty("name", p.name, nullptr);
        c.setProperty("label", p.label, nullptr);
        c.setProperty("zone", p.zone, nullptr);
        t.appendChild(c, nullptr);
    }
    return t;
}
void treeToPorts(const juce::ValueTree& t, int count, const std::function<PortMeta&(int)>& get) {
    for (auto c : t) {
        const int i = c["i"];
        if (i < 0 || i >= count) continue;
        auto& p = get(i);
        p.name = c["name"].toString().substring(0, 48);
        p.label = c["label"].toString().substring(0, 48);
        p.zone = c["zone"].toString().substring(0, 48);
    }
}
}

ShowModel::ShowModel() { resetToDefaults(); }

void ShowModel::resetToDefaults() {
    for (auto& p : inputs) p = {};
    for (auto& p : outputs) p = {};
    for (int g = 0; g < kMaxGroups; ++g) groups[static_cast<size_t>(g)] = { kDefaultGroupNames[g], {}, {} };
    macros = {};
    static const char* const macroDefaults[kNumMacros] = { "Crowd Size", "Excitement", "Space", "Echo", "Reverse", "Broadcast Density", "Dark/Bright", "Safety Margin" };
    for (int m = 0; m < kNumMacros; ++m) macroNames[static_cast<size_t>(m)] = macroDefaults[m];
    bindings.clear();
    osc = {};
    recorder = {};
    ui = {};
    report = {};
    lockedParams.clear();
    playback = juce::ValueTree("Playback");
}

bool ShowModel::isLocked(int paramIndex) const {
    return std::find(lockedParams.begin(), lockedParams.end(), paramIndex) != lockedParams.end();
}
void ShowModel::setLocked(int paramIndex, bool locked) {
    const auto it = std::find(lockedParams.begin(), lockedParams.end(), paramIndex);
    if (locked && it == lockedParams.end()) lockedParams.push_back(paramIndex);
    else if (!locked && it != lockedParams.end()) lockedParams.erase(it);
}

juce::ValueTree ShowModel::toValueTree() const {
    juce::ValueTree t("Show");
    t.setProperty("version", 1, nullptr);
    t.appendChild(portsToTree("Inputs", kMaxInputs, [this](int i) -> const PortMeta& { return inputs[static_cast<size_t>(i)]; }), nullptr);
    t.appendChild(portsToTree("Groups", kMaxGroups, [this](int i) -> const PortMeta& { return groups[static_cast<size_t>(i)]; }), nullptr);
    t.appendChild(portsToTree("Outputs", kMaxOutputs, [this](int i) -> const PortMeta& { return outputs[static_cast<size_t>(i)]; }), nullptr);

    juce::ValueTree macroTree("Macros");
    for (int m = 0; m < kNumMacros; ++m) {
        juce::ValueTree mt("Macro");
        mt.setProperty("i", m, nullptr);
        mt.setProperty("name", macroNames[static_cast<size_t>(m)], nullptr);
        const auto& map = macros.macros[static_cast<size_t>(m)];
        for (int d = 0; d < map.count; ++d) {
            const auto& dest = map.dests[static_cast<size_t>(d)];
            juce::ValueTree dt("Dest");
            dt.setProperty("param", ParamBank::idForIndex(dest.paramIndex), nullptr);
            dt.setProperty("min", dest.min01, nullptr);
            dt.setProperty("max", dest.max01, nullptr);
            dt.setProperty("curve", dest.curve, nullptr);
            dt.setProperty("invert", dest.invert, nullptr);
            dt.setProperty("bipolar", dest.bipolar, nullptr);
            mt.appendChild(dt, nullptr);
        }
        macroTree.appendChild(mt, nullptr);
    }
    t.appendChild(macroTree, nullptr);

    juce::ValueTree midi("Bindings");
    for (const auto& b : bindings) {
        juce::ValueTree bt("Binding");
        bt.setProperty("type", static_cast<int>(b.type), nullptr);
        bt.setProperty("channel", b.channel, nullptr);
        bt.setProperty("number", b.number, nullptr);
        bt.setProperty("target", static_cast<int>(b.target), nullptr);
        bt.setProperty("targetIndex", b.targetIndex, nullptr);
        bt.setProperty("targetParam", b.target == BindTarget::Param ? ParamBank::idForIndex(b.targetIndex) : juce::String(), nullptr);
        bt.setProperty("mode", static_cast<int>(b.mode), nullptr);
        bt.setProperty("min", b.min01, nullptr);
        bt.setProperty("max", b.max01, nullptr);
        midi.appendChild(bt, nullptr);
    }
    t.appendChild(midi, nullptr);

    juce::ValueTree o("Osc");
    o.setProperty("enabled", osc.enabled, nullptr);
    o.setProperty("port", osc.port, nullptr);
    o.setProperty("localhostOnly", osc.localhostOnly, nullptr);
    o.setProperty("bindAddress", osc.bindAddress, nullptr);
    t.appendChild(o, nullptr);

    juce::ValueTree r("Recorder");
    r.setProperty("folder", recorder.folder, nullptr);
    r.setProperty("format", recorder.format, nullptr);
    r.setProperty("preRoll", recorder.preRollSeconds, nullptr);
    r.setProperty("postRoll", recorder.postRollSeconds, nullptr);
    r.setProperty("maxSeconds", recorder.maxFileSeconds, nullptr);
    r.setProperty("maxMB", recorder.maxFileMegabytes, nullptr);
    r.setProperty("pattern", recorder.pattern, nullptr);
    r.setProperty("show", recorder.show, nullptr);
    r.setProperty("venue", recorder.venue, nullptr);
    for (int i = 0; i < kMaxRecordTracks; ++i) {
        const auto& tr = recorder.tracks[static_cast<size_t>(i)];
        if (tr.sourceId < 0) continue;
        juce::ValueTree tt("Track");
        tt.setProperty("i", i, nullptr);
        tt.setProperty("source", tr.sourceId, nullptr);
        tt.setProperty("channels", tr.numChannels, nullptr);
        tt.setProperty("armed", tr.armed, nullptr);
        tt.setProperty("muted", tr.muted, nullptr);
        tt.setProperty("monitor", tr.monitor, nullptr);
        tt.setProperty("name", tr.name, nullptr);
        r.appendChild(tt, nullptr);
    }
    t.appendChild(r, nullptr);

    juce::ValueTree u("Ui");
    u.setProperty("highContrast", ui.highContrast, nullptr);
    u.setProperty("compact", ui.compact, nullptr);
    u.setProperty("tooltips", ui.tooltips, nullptr);
    u.setProperty("scale", ui.scale, nullptr);
    u.setProperty("recallRampMs", ui.recallRampMs, nullptr);
    {
        juce::StringArray order;
        for (int f : ui.fxOrder) order.add(juce::String(f));
        u.setProperty("fxOrder", order.joinIntoString(","), nullptr);
    }
    u.setProperty("recallScope", static_cast<int>(ui.recallScope), nullptr);
    t.appendChild(u, nullptr);

    juce::ValueTree rp("Report");
    rp.setProperty("enabled", report.enabled, nullptr);
    rp.setProperty("autoMarkers", report.autoMarkers, nullptr);
    rp.setProperty("autoExport", report.autoExport, nullptr);
    rp.setProperty("peakSensitivity", report.peakSensitivity, nullptr);
    t.appendChild(rp, nullptr);

    juce::StringArray locked;
    for (int idx : lockedParams) locked.add(ParamBank::idForIndex(idx));
    t.setProperty("locked", locked.joinIntoString(","), nullptr);
    t.appendChild(playback.createCopy(), nullptr);
    return t;
}

bool ShowModel::fromValueTree(const juce::ValueTree& t) {
    if (!t.isValid() || t.getType().toString() != "Show") return false;
    resetToDefaults();
    treeToPorts(t.getChildWithName("Inputs"), kMaxInputs, [this](int i) -> PortMeta& { return inputs[static_cast<size_t>(i)]; });
    treeToPorts(t.getChildWithName("Groups"), kMaxGroups, [this](int i) -> PortMeta& { return groups[static_cast<size_t>(i)]; });
    treeToPorts(t.getChildWithName("Outputs"), kMaxOutputs, [this](int i) -> PortMeta& { return outputs[static_cast<size_t>(i)]; });

    for (auto mt : t.getChildWithName("Macros")) {
        const int m = mt["i"];
        if (m < 0 || m >= kNumMacros) continue;
        macroNames[static_cast<size_t>(m)] = mt["name"].toString();
        auto& map = macros.macros[static_cast<size_t>(m)];
        map.count = 0;
        for (auto dt : mt) {
            const int idx = ParamBank::indexForId(dt["param"].toString());
            if (idx < 0 || map.count >= MacroMap::kMaxDests) continue;
            MacroDest d;
            d.paramIndex = idx;
            d.min01 = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(dt["min"])));
            d.max01 = juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(dt["max"])));
            d.curve = juce::jlimit(-1.0f, 1.0f, static_cast<float>(static_cast<double>(dt["curve"])));
            d.invert = static_cast<bool>(dt["invert"]);
            d.bipolar = static_cast<bool>(dt["bipolar"]);
            map.dests[static_cast<size_t>(map.count++)] = d;
        }
    }
    for (auto bt : t.getChildWithName("Bindings")) {
        ControlBinding b;
        b.type = static_cast<MidiMsgType>(juce::jlimit(0, 2, static_cast<int>(bt["type"])));
        b.channel = juce::jlimit(0, 16, static_cast<int>(bt["channel"]));
        b.number = juce::jlimit(0, 127, static_cast<int>(bt["number"]));
        b.target = static_cast<BindTarget>(juce::jlimit(0, static_cast<int>(BindTarget::MacroValue), static_cast<int>(bt["target"])));
        b.targetIndex = bt["targetIndex"];
        if (b.target == BindTarget::Param) {
            b.targetIndex = ParamBank::indexForId(bt["targetParam"].toString());   // IDs are the stable key, not indices
            if (b.targetIndex < 0) continue;
        }
        b.mode = static_cast<BindMode>(juce::jlimit(0, 3, static_cast<int>(bt["mode"])));
        b.min01 = static_cast<float>(static_cast<double>(bt["min"]));
        b.max01 = static_cast<float>(static_cast<double>(bt["max"]));
        bindings.push_back(b);
    }
    if (auto o = t.getChildWithName("Osc"); o.isValid()) {
        osc.enabled = static_cast<bool>(o["enabled"]);
        osc.port = juce::jlimit(1, 65535, static_cast<int>(o["port"]));
        osc.localhostOnly = o.hasProperty("localhostOnly") ? static_cast<bool>(o["localhostOnly"]) : true;
        osc.bindAddress = o["bindAddress"].toString();
    }
    if (auto r = t.getChildWithName("Recorder"); r.isValid()) {
        recorder.folder = r["folder"].toString();
        recorder.format = juce::jlimit(0, 2, static_cast<int>(r["format"]));
        recorder.preRollSeconds = juce::jlimit(0, 60, static_cast<int>(r["preRoll"]));
        recorder.postRollSeconds = juce::jlimit(0, 30, static_cast<int>(r["postRoll"]));
        recorder.maxFileSeconds = juce::jmax(0.0, static_cast<double>(r["maxSeconds"]));
        recorder.maxFileMegabytes = juce::jmax(0.0, static_cast<double>(r["maxMB"]));
        if (r["pattern"].toString().isNotEmpty()) recorder.pattern = r["pattern"].toString();
        recorder.show = r["show"].toString();
        recorder.venue = r["venue"].toString();
        for (auto tt : r) {
            const int i = tt["i"];
            if (i < 0 || i >= kMaxRecordTracks) continue;
            auto& tr = recorder.tracks[static_cast<size_t>(i)];
            tr.sourceId = tt["source"];
            tr.numChannels = juce::jlimit(1, 2, static_cast<int>(tt["channels"]));
            tr.armed = static_cast<bool>(tt["armed"]);
            tr.muted = static_cast<bool>(tt["muted"]);
            tr.monitor = static_cast<bool>(tt["monitor"]);
            tr.name = tt["name"].toString();
        }
    }
    if (auto u = t.getChildWithName("Ui"); u.isValid()) {
        ui.highContrast = static_cast<bool>(u["highContrast"]);
        ui.compact = static_cast<bool>(u["compact"]);
        ui.tooltips = u.hasProperty("tooltips") ? static_cast<bool>(u["tooltips"]) : true;
        ui.scale = juce::jlimit(0.5f, 3.0f, static_cast<float>(static_cast<double>(u["scale"])));
        if (ui.scale <= 0.0f) ui.scale = 1.0f;
        if (u.hasProperty("recallRampMs"))
            ui.recallRampMs = juce::jlimit(10.0f, 500.0f, static_cast<float>(static_cast<double>(u["recallRampMs"])));
        // fxOrder: accepted only when it is a permutation of 0..kNumFx-1
        const auto tokens = juce::StringArray::fromTokens(u["fxOrder"].toString(), ",", "");
        if (tokens.size() == kNumFx) {
            std::array<int, kNumFx> order {};
            std::array<bool, kNumFx> seen {};
            bool ok = true;
            for (int k = 0; k < kNumFx && ok; ++k) {
                const int f = tokens[k].getIntValue();
                ok = f >= 0 && f < kNumFx && !seen[static_cast<size_t>(f)];
                if (ok) { seen[static_cast<size_t>(f)] = true; order[static_cast<size_t>(k)] = f; }
            }
            if (ok) ui.fxOrder = order;
        }
        if (u.hasProperty("recallScope"))
            ui.recallScope = static_cast<unsigned>(static_cast<int>(u["recallScope"])) & 0xFFu;
    }
    if (auto rp = t.getChildWithName("Report"); rp.isValid()) {   // absent in states saved before the Show Report feature
        report.enabled = rp.hasProperty("enabled") ? static_cast<bool>(rp["enabled"]) : true;
        report.autoMarkers = rp.hasProperty("autoMarkers") ? static_cast<bool>(rp["autoMarkers"]) : true;
        report.autoExport = rp.hasProperty("autoExport") ? static_cast<bool>(rp["autoExport"]) : true;
        const double s = rp.hasProperty("peakSensitivity") ? static_cast<double>(rp["peakSensitivity"]) : 0.5;
        report.peakSensitivity = std::isfinite(s) ? static_cast<float>(juce::jlimit(0.0, 1.0, s)) : 0.5f;
    }
    for (const auto& id : juce::StringArray::fromTokens(t["locked"].toString(), ",", ""))
        if (const int idx = ParamBank::indexForId(id); idx >= 0) lockedParams.push_back(idx);
    if (auto p = t.getChildWithName("Playback"); p.isValid()) playback = p.createCopy();
    return true;
}
}
