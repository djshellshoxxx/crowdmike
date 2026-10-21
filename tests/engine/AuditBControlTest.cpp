// Regression tests from the part-B control/recording audit.
#include "TestHarness.h"
#include "PluginProcessor.h"

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
int idx(Mod m, int inst, int e) { return ParamBank::index(m, inst, e); }
}

// Scene recall that comes from MIDI/OSC/host buttons never goes through the GUI's showModelChanged(), so the
// recalled macro assignments must be republished to the audio thread by the recall itself.
CM_TEST(auditB_scene_recall_republishes_macros) {
    CrowdMikeAudioProcessor p;
    const int dest = idx(Mod::Master, 0, MasterP::Level);
    MacroDest d; d.paramIndex = dest; d.min01 = 0.0f; d.max01 = 1.0f;
    p.getShow().macros.macros[0].dests[0] = d;
    p.getShow().macros.macros[0].count = 1;
    p.applyShowChanges();
    p.getParameters().getParameter("macro1")->setValueNotifyingHost(1.0f);
    p.getScenes().capture(0, "with macro");

    // Drop the assignment and publish that state, as if the user cleared it.
    p.getShow().macros.macros[0].count = 0;
    p.applyShowChanges();
    p.getBank().refresh();
    CM_EXPECT_NEAR(p.getBank().effectiveAt(dest), 0.0, 0.01);   // unmodulated default level

    CM_EXPECT(p.getScenes().recall(0));                // no applyShowChanges() afterwards
    p.getBank().refresh();
    CM_EXPECT_NEAR(p.getBank().effectiveAt(dest), 12.0, 0.01);
}

CM_TEST(auditB_loaded_empty_recall_scope_falls_back_to_all) {
    CrowdMikeAudioProcessor a;
    a.getShow().ui.recallScope = 0;
    const auto tree = a.getShow().toValueTree();
    ShowModel b;
    CM_EXPECT(b.fromValueTree(tree));
    CM_EXPECT(b.ui.recallScope == 0xFFu);
}

CM_TEST(auditB_report_export_reachable_by_osc_and_midi) {
    CrowdMikeAudioProcessor p;
    int calls = 0;
    p.getHub().actions.reportExport = [&] { ++calls; };
    p.getHub().handleOsc("/crowdmike/report/export", {});
    CM_EXPECT(calls == 1);
    p.getHub().handleOsc("/crowdmike/report/export", { 0.0f });   // release does nothing
    CM_EXPECT(calls == 1);

    ControlBinding b;
    b.type = MidiMsgType::NoteOnOff; b.number = 60; b.target = BindTarget::ReportExport; b.mode = BindMode::Trigger;
    p.getShow().bindings.push_back(b);
    ShowModel q;
    CM_EXPECT(q.fromValueTree(p.getShow().toValueTree()));   // the new target must survive the load clamp
    CM_EXPECT(q.bindings.size() == 1 && q.bindings[0].target == BindTarget::ReportExport);

    MidiEvent n; n.type = MidiMsgType::NoteOnOff; n.channel = 1; n.number = 60; n.value = 100; n.isOn = true;
    p.getHub().handleMidi(n);
    CM_EXPECT(calls == 2);
}
