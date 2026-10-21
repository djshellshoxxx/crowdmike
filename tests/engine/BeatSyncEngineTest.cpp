// Beat Sync wiring: tempo params, TempoHub through the processor, state compatibility, scene scope via MIDI/OSC.
#include "TestHarness.h"
#include "Control/OscAddress.h"
#include "Core/StateIO.h"
#include "PluginProcessor.h"

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
int idx(Mod m, int inst, int e) { return ParamBank::index(m, inst, e); }
float rawValue(CrowdMikeAudioProcessor& p, const juce::String& id) { return p.getParameters().getRawParameterValue(id)->load(); }
void setActual(CrowdMikeAudioProcessor& p, const char* id, float v) {
    auto* q = p.getParameters().getParameter(id);
    q->setValueNotifyingHost(q->convertTo0to1(v));
}
void runBlocks(CrowdMikeAudioProcessor& p, int blocks, int n = 256, float amp = 0.1f) {
    juce::AudioBuffer<float> b(2, n);
    juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k) {
        for (int s = 0; s < n; ++s) { b.setSample(0, s, amp * std::sin(0.05f * static_cast<float>(s))); b.setSample(1, s, 0.0f); }
        p.processBlock(b, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < n; ++s) CM_EXPECT(std::isfinite(b.getSample(ch, s)));
    }
}
}

CM_TEST(tempo_params_appended_with_stable_ids) {
    const int total = ParamBank::totalCount();
    CM_EXPECT(ParamBank::idForIndex(idx(Mod::Tempo, 0, TempoP::Source)) == "tempo.source");
    CM_EXPECT(ParamBank::idForIndex(idx(Mod::Tempo, 0, TempoP::ManualBpm)) == "tempo.manualBpm");
    CM_EXPECT(ParamBank::idForIndex(idx(Mod::Tempo, 0, TempoP::Quantise)) == "tempo.quantise");
    CM_EXPECT(ParamBank::idForIndex(idx(Mod::Tempo, 0, TempoP::Resync)) == "tempo.resync");
    // appended last: the tempo block is the tail of the index space, the macros precede it
    CM_EXPECT(idx(Mod::Tempo, 0, TempoP::Resync) == total - 1);
    CM_EXPECT(idx(Mod::Tempo, 0, TempoP::Source) == total - TempoP::Count);
    CM_EXPECT(idx(Mod::Macro, 0, 0) < idx(Mod::Tempo, 0, 0));
}

CM_TEST(tempo_manual_source_drives_hub_through_processor) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    setActual(p, "tempo.source", 2.0f);       // Manual
    setActual(p, "tempo.manualBpm", 150.0f);
    runBlocks(p, 4);
    auto s = p.getTempo().snapshot();
    CM_EXPECT(s.hasTempo && s.playing);
    CM_EXPECT_NEAR(s.bpm, 150.0, 0.01);
    CM_EXPECT(s.ppq > 0.0);
    // nudge adds, result stays inside limits
    setActual(p, "tempo.nudgeBpm", 10.0f);
    runBlocks(p, 2);
    CM_EXPECT_NEAR(p.getTempo().snapshot().bpm, 160.0, 0.01);
    setActual(p, "tempo.manualBpm", 300.0f);
    runBlocks(p, 2);
    CM_EXPECT(p.getTempo().snapshot().bpm <= 300.0);
    // ppq advances at bpm/60 per second across blocks
    const double p0 = p.getTempo().snapshot().ppq;
    runBlocks(p, 10);
    const double p1 = p.getTempo().snapshot().ppq;
    CM_EXPECT_NEAR(p1 - p0, 10.0 * 256.0 / 48000.0 * 300.0 / 60.0, 1e-3);
}

CM_TEST(tempo_tap_via_parameter_and_resync) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    setActual(p, "tempo.source", 1.0f);       // Tap
    runBlocks(p, 2);
    CM_EXPECT(!p.getTempo().snapshot().hasTempo);
    const double t0 = 100.0;
    for (int i = 0; i < 5; ++i) p.getTempo().tap(t0 + 0.5 * i);   // 120 BPM
    runBlocks(p, 2);
    const auto s = p.getTempo().snapshot();
    CM_EXPECT(s.hasTempo);
    CM_EXPECT_NEAR(s.bpm, 120.0, 0.5);
    p.getTempo().resyncDownbeat();
    runBlocks(p, 1);
    CM_EXPECT(p.getTempo().snapshot().ppq < 0.2);
}

CM_TEST(tempo_defaults_leave_host_behaviour_and_quantise_off) {
    CrowdMikeAudioProcessor p;
    CM_EXPECT(rawValue(p, "tempo.source") < 0.5f);
    CM_EXPECT(rawValue(p, "tempo.quantise") < 0.5f);
    CM_EXPECT_NEAR(rawValue(p, "tempo.manualBpm"), 120.0, 0.01);
    p.prepareToPlay(44100.0, 128);
    runBlocks(p, 3, 128);       // no host playhead: no tempo, still finite audio
    CM_EXPECT(!p.getTempo().snapshot().hasTempo);
}

CM_TEST(tempo_state_round_trip_and_schema_v2_without_tempo_loads_identically) {
    CrowdMikeAudioProcessor src;
    setActual(src, "input2.trimDb", 4.0f);
    setActual(src, "tempo.source", 2.0f);
    setActual(src, "tempo.manualBpm", 97.5f);
    setActual(src, "tempo.quantise", 3.0f);
    juce::MemoryBlock blob;
    src.getStateInformation(blob);
    CrowdMikeAudioProcessor dst;
    dst.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CM_EXPECT_NEAR(rawValue(dst, "tempo.manualBpm"), 97.5, 0.01);
    CM_EXPECT(juce::roundToInt(rawValue(dst, "tempo.source")) == 2);
    CM_EXPECT(juce::roundToInt(rawValue(dst, "tempo.quantise")) == 3);
    CM_EXPECT_NEAR(rawValue(dst, "input2.trimDb"), 4.0, 0.05);

    // A schema-v2 state written before Beat Sync has no tempo parameters: everything else loads identically, tempo = defaults.
    auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
    CM_EXPECT(xml != nullptr);
    for (int i = xml->getNumChildElements(); --i >= 0;) {
        auto* c = xml->getChildElement(i);
        if (c->hasTagName("PARAM") && c->getStringAttribute("id").startsWith("tempo.")) xml->removeChildElement(c, true);
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    CrowdMikeAudioProcessor legacy;
    legacy.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CM_EXPECT_NEAR(rawValue(legacy, "input2.trimDb"), 4.0, 0.05);
    CM_EXPECT(juce::roundToInt(rawValue(legacy, "tempo.source")) == 0);
    CM_EXPECT(juce::roundToInt(rawValue(legacy, "tempo.quantise")) == 0);
    CM_EXPECT_NEAR(rawValue(legacy, "tempo.manualBpm"), 120.0, 0.01);
    for (int i = 0; i < ParamBank::totalCount() - TempoP::Count; ++i) {
        const auto id = ParamBank::idForIndex(i);
        if (id.startsWith("macro")) continue;
        CM_EXPECT_NEAR(rawValue(legacy, id), rawValue(dst, id), 1e-4);
    }
}

CM_TEST(scenes_recall_tempo_settings_but_never_tap_or_resync) {
    CrowdMikeAudioProcessor p;
    setActual(p, "tempo.source", 2.0f);
    setActual(p, "tempo.manualBpm", 90.0f);
    setActual(p, "tempo.quantise", 2.0f);
    setActual(p, "tempo.tap", 1.0f);
    p.getScenes().capture(0, "T");
    setActual(p, "tempo.tap", 0.0f);
    setActual(p, "tempo.manualBpm", 140.0f);
    setActual(p, "tempo.quantise", 0.0f);
    p.getScenes().recall(0);
    CM_EXPECT_NEAR(rawValue(p, "tempo.manualBpm"), 90.0, 0.05);
    CM_EXPECT(juce::roundToInt(rawValue(p, "tempo.quantise")) == 2);
    CM_EXPECT(rawValue(p, "tempo.tap") < 0.5f);
    // scoped to Inputs only: tempo untouched
    setActual(p, "tempo.manualBpm", 140.0f);
    p.getScenes().recall(0, ScopeInputs);
    CM_EXPECT_NEAR(rawValue(p, "tempo.manualBpm"), 140.0, 0.05);
}

CM_TEST(osc_tempo_and_scope_addresses_resolve) {
    using K = OscAction::Kind;
    CM_EXPECT(resolveOsc("/crowdmike/tempo/tap", {}).kind == K::TempoTap);
    CM_EXPECT(resolveOsc("/crowdmike/tempo/resync", {}).kind == K::TempoResync);
    CM_EXPECT(resolveOsc("/crowdmike/tempo/tap", { 0.0f }).kind == K::None);   // button release ignored
    auto a = resolveOsc("/crowdmike/tempo/bpm", { 128.0f });
    CM_EXPECT(a.kind == K::SetParam && a.index == idx(Mod::Tempo, 0, TempoP::ManualBpm));
    a = resolveOsc("/crowdmike/tempo/quantise", { 2.0f });
    CM_EXPECT(a.kind == K::SetParam && a.index == idx(Mod::Tempo, 0, TempoP::Quantise));
    CM_EXPECT(!resolveOsc("/crowdmike/tempo/bogus", {}).error.isEmpty());
    a = resolveOsc("/crowdmike/scene/scope", { 5.0f });
    CM_EXPECT(a.kind == K::SceneScopeSet && a.index == 5);
    a = resolveOsc("/crowdmike/scene/scope/fx", { 1.0f });
    CM_EXPECT(a.kind == K::SceneScopeBit && a.index == 2 && a.hasValue);
    a = resolveOsc("/crowdmike/scene/scope/all", {});
    CM_EXPECT(a.kind == K::SceneScopeBit && a.index == 8);
    CM_EXPECT(!resolveOsc("/crowdmike/scene/scope/bogus", {}).error.isEmpty());
}

CM_TEST(osc_and_midi_scene_recall_honour_recall_scope) {
    CrowdMikeAudioProcessor p;
    auto& hub = p.getHub();
    setActual(p, "input1.trimDb", 6.0f);
    setActual(p, "group1.trimDb", -3.0f);
    p.getScenes().capture(0, "A");
    p.getScenes().capture(1, "B");

    // OSC: scope = Inputs only, then select scene 1
    setActual(p, "input1.trimDb", -10.0f);
    setActual(p, "group1.trimDb", -9.0f);
    hub.handleOsc("/crowdmike/scene/scope", { static_cast<float>(ScopeInputs) });
    CM_EXPECT(p.getShow().ui.recallScope == ScopeInputs);
    hub.handleOsc("/crowdmike/scene/1/select", {});
    CM_EXPECT_NEAR(rawValue(p, "input1.trimDb"), 6.0, 0.05);
    CM_EXPECT_NEAR(rawValue(p, "group1.trimDb"), -9.0, 0.05);    // groups out of scope: untouched

    // named bit: add groups
    hub.handleOsc("/crowdmike/scene/scope/groups", { 1.0f });
    CM_EXPECT(p.getShow().ui.recallScope == (ScopeInputs | ScopeGroups));
    hub.handleOsc("/crowdmike/scene/2/select", {});
    CM_EXPECT_NEAR(rawValue(p, "group1.trimDb"), -3.0, 0.05);

    // an empty scope is refused
    hub.handleOsc("/crowdmike/scene/scope", { 0.0f });
    CM_EXPECT(p.getShow().ui.recallScope == (ScopeInputs | ScopeGroups));
    hub.handleOsc("/crowdmike/scene/scope/all", {});
    CM_EXPECT(p.getShow().ui.recallScope == ScopeAll);

    // MIDI binding: note triggers scene next with scope Inputs only
    hub.handleOsc("/crowdmike/scene/scope", { static_cast<float>(ScopeInputs) });
    setActual(p, "input1.trimDb", -20.0f);
    setActual(p, "group1.trimDb", -20.0f);
    ControlBinding b;
    b.type = MidiMsgType::NoteOnOff; b.channel = 1; b.number = 36;
    b.target = BindTarget::SceneSelect; b.targetIndex = 0; b.mode = BindMode::Trigger;
    p.getShow().bindings.push_back(b);
    MidiEvent e; e.type = MidiMsgType::NoteOnOff; e.channel = 1; e.number = 36; e.value = 100; e.isOn = true;
    hub.handleMidi(e);
    CM_EXPECT_NEAR(rawValue(p, "input1.trimDb"), 6.0, 0.05);
    CM_EXPECT_NEAR(rawValue(p, "group1.trimDb"), -20.0, 0.05);   // honoured scope over MIDI too

    // MIDI scope toggle binding flips a bit
    ControlBinding sb;
    sb.type = MidiMsgType::NoteOnOff; sb.channel = 1; sb.number = 40;
    sb.target = BindTarget::SceneScopeToggle; sb.targetIndex = 1; sb.mode = BindMode::Trigger;   // groups
    p.getShow().bindings.push_back(sb);
    e.number = 40;
    hub.handleMidi(e);
    CM_EXPECT((p.getShow().ui.recallScope & ScopeGroups) != 0);
    hub.handleMidi(e);
    CM_EXPECT((p.getShow().ui.recallScope & ScopeGroups) == 0);

    // scene next/prev over OSC use the scope too
    setActual(p, "input1.trimDb", -20.0f);
    setActual(p, "group1.trimDb", -20.0f);
    hub.handleOsc("/crowdmike/scene/next", {});
    CM_EXPECT_NEAR(rawValue(p, "group1.trimDb"), -20.0, 0.05);
}

CM_TEST(tempo_binding_targets_execute) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    auto& hub = p.getHub();
    setActual(p, "tempo.source", 1.0f);
    double t = 10.0;
    for (int i = 0; i < 4; ++i) { p.getTempo().tap(t); t += 0.6; }   // 100 BPM
    runBlocks(p, 2);
    CM_EXPECT_NEAR(p.getTempo().snapshot().bpm, 100.0, 0.5);
    runBlocks(p, 10);
    hub.handleOsc("/crowdmike/tempo/resync", {});
    runBlocks(p, 1);
    CM_EXPECT(p.getTempo().snapshot().ppq < 0.2);
}
