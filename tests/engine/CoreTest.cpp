#include "TestHarness.h"
#include "Control/OscAddress.h"
#include "Core/PresetManager.h"
#include "Core/StateIO.h"
#include "PluginProcessor.h"
#include "Routing/RouteValidator.h"
#include "Routing/WiringTemplates.h"

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
int idx(Mod m, int inst, int e) { return ParamBank::index(m, inst, e); }
float rawValue(CrowdMikeAudioProcessor& p, const juce::String& id) { return p.getParameters().getRawParameterValue(id)->load(); }
}

CM_TEST(param_bank_ids_round_trip) {
    for (int i = 0; i < ParamBank::totalCount(); ++i) {
        const auto id = ParamBank::idForIndex(i);
        CM_EXPECT(id.isNotEmpty());
        CM_EXPECT(ParamBank::indexForId(id) == i);
    }
    CM_EXPECT(ParamBank::indexForId("input1.trimDb") == idx(Mod::Input, 0, InputP::Trim));
    CM_EXPECT(ParamBank::indexForId("limiterCeilingDb") == idx(Mod::Global, 0, GlobalP::OutCeiling));
    CM_EXPECT(ParamBank::indexForId("nope") < 0);
}

CM_TEST(processor_registers_every_parameter) {
    CrowdMikeAudioProcessor p;
    for (int i = 0; i < ParamBank::totalCount(); ++i) CM_EXPECT(p.getParameters().getParameter(ParamBank::idForIndex(i)) != nullptr);
}

CM_TEST(routing_state_clamps_and_round_trips) {
    RoutingState r;
    CM_EXPECT(r.setDirect(0, 1, 5.0f));
    CM_EXPECT_NEAR(r.getDirect(0, 1), 2.0f, 1e-6);
    CM_EXPECT(!r.setDirect(-1, 0, 1.0f));
    CM_EXPECT(!r.setDirect(0, 99, 1.0f));
    CM_EXPECT(!r.setAssign(0, 0, NAN));
    r.setAssign(3, 2, 0.5f);
    r.setBus(RoutingState::busRowForMaster(1), 5, 0.25f);
    const auto rev = r.revision();
    r.setAssign(3, 2, 0.5f);   // unchanged: no revision bump
    CM_EXPECT(r.revision() == rev);
    RoutingState copy;
    CM_EXPECT(copy.fromValueTree(r.toValueTree()));
    CM_EXPECT_NEAR(copy.getDirect(0, 1), 2.0f, 1e-3);
    CM_EXPECT_NEAR(copy.getAssign(3, 2), 0.5f, 1e-3);
    CM_EXPECT_NEAR(copy.getBus(RoutingState::busRowForMaster(1), 5), 0.25f, 1e-3);
    juce::ValueTree bad("Routing");
    bad.setProperty("version", 1, nullptr);
    bad.setProperty("direct", "1 2 3", nullptr);
    CM_EXPECT(!copy.fromValueTree(bad));
    CM_EXPECT_NEAR(copy.getAssign(3, 2), 0.5f, 1e-3);   // untouched on failure
}

CM_TEST(route_validator_legality_and_cycles) {
    using N = NodeClass;
    const N all[] = { N::RawInput, N::ProcInput, N::Group, N::FxSend, N::FxReturn, N::Playback, N::Master, N::Recorder, N::Output };
    // Legal topology is acyclic: build a node graph out of every legal class pair and check it has no cycle.
    std::vector<Edge> edges;
    int a = 0;
    for (auto s : all) { int b = 0; for (auto d : all) { if (RouteValidator::isLegalRoute(s, d)) edges.push_back({ a, b }); ++b; } ++a; }
    CM_EXPECT(!RouteValidator::hasCycle(9, edges));
    CM_EXPECT(!RouteValidator::isLegalRoute(N::Master, N::Group));
    CM_EXPECT(!RouteValidator::isLegalRoute(N::Output, N::ProcInput));
    CM_EXPECT(!RouteValidator::isLegalRoute(N::Recorder, N::Output));
    CM_EXPECT(RouteValidator::wouldCreateCycle(4, { { 0, 1 }, { 1, 2 }, { 2, 3 } }, { 3, 0 }));
    CM_EXPECT(!RouteValidator::wouldCreateCycle(4, { { 0, 1 }, { 1, 2 } }, { 0, 3 }));
    CM_EXPECT(RouteValidator::wouldCreateCycle(2, {}, { 1, 1 }));
    const auto reach = RouteValidator::reachableFrom(4, { { 0, 1 }, { 1, 2 } }, 0);
    CM_EXPECT(reach.size() == 3);
    RoutingState r;
    r.setAssign(0, 0, 1.0f);
    CM_EXPECT(!RouteValidator::hasCycle(RouteValidator::nodeCount(), RouteValidator::edgesFromState(r)));
    auto issues = RouteValidator::validate(r, 2, 2);
    bool silentGroup = false;
    for (auto& i : issues) silentGroup = silentGroup || i.message.contains("reaches no output");
    CM_EXPECT(silentGroup);
    r.setBus(RoutingState::busRowForGroup(0, 0), 0, 1.0f);
    issues = RouteValidator::validate(r, 2, 2);
    silentGroup = false;
    for (auto& i : issues) silentGroup = silentGroup || i.message.contains("reaches no output");
    CM_EXPECT(!silentGroup);
}

CM_TEST(osc_addresses_resolve) {
    using K = OscAction::Kind;
    auto a = resolveOsc("/crowdmike/master/level", { -6.0f });
    CM_EXPECT(a.kind == K::SetParam && a.index == idx(Mod::Master, 0, MasterP::Level) && a.value == -6.0f);
    a = resolveOsc("/crowdmike/input/3/mute", { 1.0f });
    CM_EXPECT(a.kind == K::SetParam && a.index == idx(Mod::Input, 2, InputP::Mute));
    a = resolveOsc("/crowdmike/fx/reverse/trigger", {});
    CM_EXPECT(a.kind == K::Pulse && a.index == idx(Mod::FxReverse, 0, ReverseP::Trigger));
    a = resolveOsc("/crowdmike/scene/3/select", {});
    CM_EXPECT(a.kind == K::SceneSelect && a.index == 2);
    CM_EXPECT(resolveOsc("/crowdmike/scene/next", {}).kind == K::SceneNext);
    CM_EXPECT(resolveOsc("/crowdmike/record/start", {}).kind == K::RecordStart);
    a = resolveOsc("/crowdmike/panic", {});
    CM_EXPECT(a.kind == K::Panic && a.value == 1.0f);
    CM_EXPECT(resolveOsc("/crowdmike/panic", { 0.0f }).value == 0.0f);
    a = resolveOsc("/crowdmike/macro/2", { 0.75f });
    CM_EXPECT(a.kind == K::Macro && a.index == 1 && a.value == 0.75f);
    CM_EXPECT(resolveOsc("/crowdmike/input/99/mute", {}).kind == K::None);
    CM_EXPECT(!resolveOsc("/crowdmike/input/99/mute", {}).error.isEmpty());
    CM_EXPECT(resolveOsc("/other/thing", {}).kind == K::None);
    a = resolveOsc("/crowdmike/param/group2.trimDb", { 3.0f });
    CM_EXPECT(a.kind == K::SetParam && a.index == idx(Mod::Group, 1, GroupP::Trim));
    a = resolveOsc("/crowdmike/clip/4/trigger", {});
    CM_EXPECT(a.kind == K::ClipTrigger && a.index == 3);
}

CM_TEST(midi_binding_and_learn) {
    CrowdMikeAudioProcessor p;
    auto& hub = p.getHub();
    const int target = idx(Mod::Master, 0, MasterP::Level);
    hub.startLearn({ BindTarget::Param, target, BindMode::Absolute });
    CM_EXPECT(hub.isLearning());
    MidiEvent e; e.type = MidiMsgType::CC; e.channel = 1; e.number = 7; e.value = 127;
    hub.handleMidi(e);
    CM_EXPECT(!hub.isLearning());
    CM_EXPECT(p.getShow().bindings.size() == 1);
    e.value = 127; hub.handleMidi(e);
    CM_EXPECT_NEAR(rawValue(p, "master.levelDb"), 12.0, 0.2);
    e.value = 0; hub.handleMidi(e);
    CM_EXPECT_NEAR(rawValue(p, "master.levelDb"), -80.0, 0.2);
    // toggle binding on a note
    hub.startLearn({ BindTarget::Param, idx(Mod::Input, 0, InputP::Mute), BindMode::Toggle });
    MidiEvent n; n.type = MidiMsgType::NoteOnOff; n.channel = 2; n.number = 40; n.value = 100; n.isOn = true;
    hub.handleMidi(n);
    hub.handleMidi(n);   // press -> toggles on
    CM_EXPECT(rawValue(p, "input1.mute") > 0.5f);
    hub.handleMidi(n);
    CM_EXPECT(rawValue(p, "input1.mute") < 0.5f);
    // audio-thread queue
    juce::MidiBuffer mb;
    mb.addEvent(juce::MidiMessage::controllerEvent(1, 7, 64), 0);
    hub.queueMidiFromAudioThread(mb);
    hub.pump();
    CM_EXPECT(rawValue(p, "master.levelDb") > -40.0f);
}

CM_TEST(macro_modulates_destination) {
    CrowdMikeAudioProcessor p;
    const int dest = idx(Mod::Master, 0, MasterP::Level);
    MacroDest d; d.paramIndex = dest; d.min01 = 0.0f; d.max01 = 1.0f;
    p.getShow().macros.macros[0].dests[0] = d;
    p.getShow().macros.macros[0].count = 1;
    p.applyShowChanges();
    p.getParameters().getParameter("macro1")->setValueNotifyingHost(1.0f);
    p.getBank().refresh();
    CM_EXPECT_NEAR(p.getBank().effectiveAt(dest), 12.0, 0.01);
    p.getParameters().getParameter("macro1")->setValueNotifyingHost(0.0f);
    p.getBank().refresh();
    CM_EXPECT_NEAR(p.getBank().effectiveAt(dest), -80.0, 0.01);
    // inverted
    p.getShow().macros.macros[0].dests[0].invert = true;
    p.applyShowChanges();
    p.getBank().refresh();
    CM_EXPECT_NEAR(p.getBank().effectiveAt(dest), 12.0, 0.01);
}

CM_TEST(scenes_capture_recall_partial_and_locks) {
    CrowdMikeAudioProcessor p;
    auto set = [&](const char* id, float actual) { auto* q = p.getParameters().getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(actual)); };
    set("input1.trimDb", 6.0f);
    set("group1.trimDb", -3.0f);
    p.getRouting().setAssign(0, 0, 0.7f);
    p.getScenes().capture(0, "A");
    set("input1.trimDb", -10.0f);
    set("group1.trimDb", -9.0f);
    p.getRouting().setAssign(0, 0, 0.1f);
    CM_EXPECT(p.getScenes().recall(0, ScopeInputs));      // inputs only
    CM_EXPECT_NEAR(rawValue(p, "input1.trimDb"), 6.0, 0.05);
    CM_EXPECT_NEAR(rawValue(p, "group1.trimDb"), -9.0, 0.05);
    CM_EXPECT_NEAR(p.getRouting().getAssign(0, 0), 0.1, 1e-3);
    p.getShow().setLocked(idx(Mod::Input, 0, InputP::Trim), true);
    set("input1.trimDb", -20.0f);
    p.getScenes().recall(0);
    CM_EXPECT_NEAR(rawValue(p, "input1.trimDb"), -20.0, 0.05);   // locked
    CM_EXPECT_NEAR(rawValue(p, "group1.trimDb"), -3.0, 0.05);
    CM_EXPECT_NEAR(p.getRouting().getAssign(0, 0), 0.7, 1e-3);
    CM_EXPECT(!p.getScenes().recall(5));                          // empty slot
    p.getScenes().capture(3, "B");
    CM_EXPECT(p.getScenes().countUsed() == 2);
    p.getScenes().recall(0);
    CM_EXPECT(p.getScenes().next() && p.getScenes().current() == 3);
    CM_EXPECT(p.getScenes().next() && p.getScenes().current() == 0);
    CM_EXPECT(p.getScenes().previous() && p.getScenes().current() == 3);
    // transient params never part of a scene
    set("panic", 1.0f);
    p.getScenes().capture(7, "C");
    set("panic", 0.0f);
    p.getScenes().recall(7);
    CM_EXPECT(rawValue(p, "panic") < 0.5f);
}

CM_TEST(presets_apply_between_instances) {
    CrowdMikeAudioProcessor p;
    auto& a = p.getParameters();
    const auto* dense = [] { for (auto& f : PresetManager::factoryPresets()) if (juce::String(f.name) == "Dense Crowd") return &f; return static_cast<const FactoryPreset*>(nullptr); }();
    CM_EXPECT(dense != nullptr);
    CM_EXPECT(PresetManager::applyFactory(a, *dense, Mod::Input, 2) > 0);
    CM_EXPECT_NEAR(rawValue(p, "input3.compThr"), -24.0, 0.2);
    auto captured = PresetManager::capture(a, Mod::Input, 2, "comp");
    CM_EXPECT(PresetManager::apply(a, captured, Mod::Group, 1) > 0);
    CM_EXPECT_NEAR(rawValue(p, "group2.compThr"), -24.0, 0.2);
    CM_EXPECT_NEAR(rawValue(p, "group2.compRatio"), 4.0, 0.2);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_preset_test.cmpreset");
    CM_EXPECT(PresetManager::saveToFile(captured, file));
    CM_EXPECT(PresetManager::loadFromFile(file).isValid());
    file.deleteFile();
    auto show = PresetManager::captureShow(a, p.getRouting(), p.getShow());
    CM_EXPECT(PresetManager::applyShow(a, p.getRouting(), p.getShow(), show));
    CM_EXPECT(!PresetManager::applyShow(a, p.getRouting(), p.getShow(), juce::ValueTree("Preset")));
}

CM_TEST(state_round_trip_v2_and_v1_migration) {
    CrowdMikeAudioProcessor src;
    auto setv = [&](CrowdMikeAudioProcessor& p, const char* id, float v) { auto* q = p.getParameters().getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(v)); };
    setv(src, "input2.trimDb", 4.0f);
    src.getRouting().setBus(RoutingState::busRowForMaster(0), 0, 0.8f);
    src.getShow().input(1).name = "Balcony Left";
    src.getShow().macros.macros[2].dests[0].paramIndex = idx(Mod::FxDelay, 0, DelayP::Feedback);
    src.getShow().macros.macros[2].count = 1;
    src.getScenes().capture(1, "Opening");
    juce::MemoryBlock blob;
    src.getStateInformation(blob);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
    CM_EXPECT(xml != nullptr && xml->getIntAttribute("schemaVersion") == StateIO::kSchemaVersion);
    CM_EXPECT(xml->hasAttribute("appVersion") && xml->hasAttribute("platform") && xml->getChildByName("EngineConfig") != nullptr);
    CrowdMikeAudioProcessor dst;
    dst.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CM_EXPECT_NEAR(rawValue(dst, "input2.trimDb"), 4.0, 0.05);
    CM_EXPECT_NEAR(dst.getRouting().getBus(RoutingState::busRowForMaster(0), 0), 0.8, 1e-3);
    CM_EXPECT(dst.getShow().input(1).name == "Balcony Left");
    CM_EXPECT(dst.getShow().macros.macros[2].count == 1);
    CM_EXPECT(dst.getScenes().at(1).used && dst.getScenes().at(1).name == "Opening");

    // Hand-built first-release state: parameters + RoutingMatrix attributes.
    juce::XmlElement v1("CrowdMikeState");
    v1.setAttribute("schemaVersion", 1);
    auto* param = v1.createNewChildElement("PARAM");
    param->setAttribute("id", "input1.trimDb");
    param->setAttribute("value", 7.5);
    auto* routes = v1.createNewChildElement("RoutingMatrix");
    routes->setAttribute("r1_2", 0.35);
    juce::MemoryBlock v1blob;
    juce::AudioProcessor::copyXmlToBinary(v1, v1blob);
    CrowdMikeAudioProcessor legacy;
    legacy.setStateInformation(v1blob.getData(), static_cast<int>(v1blob.getSize()));
    CM_EXPECT_NEAR(rawValue(legacy, "input1.trimDb"), 7.5, 0.05);
    CM_EXPECT_NEAR(legacy.getRequestedRouteGain(0, 1), 0.35, 1e-3);
    // Newer / invalid states are rejected without touching anything.
    juce::XmlElement future("CrowdMikeState");
    future.setAttribute("schemaVersion", 99);
    juce::MemoryBlock fblob;
    juce::AudioProcessor::copyXmlToBinary(future, fblob);
    const float before = rawValue(legacy, "input1.trimDb");
    legacy.setStateInformation(fblob.getData(), static_cast<int>(fblob.getSize()));
    CM_EXPECT_NEAR(rawValue(legacy, "input1.trimDb"), before, 1e-4);
}

CM_TEST(wiring_templates_produce_valid_routing) {
    for (int t = 0; t < static_cast<int>(WiringTemplates::Id::Count); ++t) {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(48000.0, 128);
        p.applyWiringTemplate(t);
        CM_EXPECT(!RouteValidator::hasCycle(RouteValidator::nodeCount(), RouteValidator::edgesFromState(p.getRouting())));
        for (auto& issue : RouteValidator::validate(p.getRouting(), 2, 2)) CM_EXPECT(issue.severity != RouteIssue::Error);
    }
}
