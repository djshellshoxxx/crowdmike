// Feedback-risk gathering (Engine/InputRisk) and the processor's periodic meters.inRisk update.
#include "TestHarness.h"
#include "Engine/InputRisk.h"
#include "PluginProcessor.h"

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
InputRiskReport riskOf(CrowdMikeAudioProcessor& p, int input, int tap = 0, float sev = 0.0f) {
    const InputRiskContext ctx { p.getBank(), p.getRouting(), p.getShow(), p.getActiveOutputChannelCount(), tap, sev };
    return evaluateInputRisk(ctx, input);
}
void setActual(CrowdMikeAudioProcessor& p, Mod m, int inst, int id, float v) { p.getHub().setParamActual(ParamBank::index(m, inst, id), v); }
}

CM_TEST(risk_default_direct_route_scores_and_mute_breaks_it) {
    CrowdMikeAudioProcessor p;   // default: 2x2 identity direct routing, unity gains
    const auto base = riskOf(p, 0);
    CM_EXPECT(base.inputs.routeExists);
    CM_EXPECT(base.worstOutput == 0);
    CM_EXPECT_NEAR(base.result.score, 0.3, 0.01);        // unity loop gain, different zone
    setActual(p, Mod::Input, 0, InputP::Mute, 1.0f);
    CM_EXPECT(riskOf(p, 0).result.score == 0.0f);
    setActual(p, Mod::Input, 0, InputP::Mute, 0.0f);
    setActual(p, Mod::Output, 0, OutputP::Mute, 1.0f);
    CM_EXPECT(riskOf(p, 0).result.score == 0.0f);        // only output 0 carried input 0
}

CM_TEST(risk_monotonic_in_trim_zone_feedback_and_severity) {
    CrowdMikeAudioProcessor p;
    const float s0 = riskOf(p, 0).result.score;
    setActual(p, Mod::Input, 0, InputP::Trim, 6.0f);
    const float s1 = riskOf(p, 0).result.score;
    CM_EXPECT(s1 > s0);
    p.getShow().input(0).zone = "FOH";
    p.getShow().output(0).zone = "FOH";
    const auto zoned = riskOf(p, 0);
    CM_EXPECT(zoned.inputs.sameZone);
    CM_EXPECT(zoned.result.score > s1);
    // Delay send open + feedback adds recirculation gain.
    setActual(p, Mod::FxDelay, 0, DelayP::Feedback, 0.9f);
    CM_EXPECT_NEAR(riskOf(p, 0).inputs.delayFeedback, 0.0, 1e-6);   // no send yet
    setActual(p, Mod::Input, 0, InputP::Fx1, 0.0f);
    const auto withFx = riskOf(p, 0);
    CM_EXPECT_NEAR(withFx.inputs.delayFeedback, 0.9, 1e-3);
    CM_EXPECT(withFx.result.score >= zoned.result.score);
    // Persistent peak only counts when the detector listens to this input.
    CM_EXPECT(riskOf(p, 0, 2, 1.0f).inputs.persistentPeakSeverity == 0.0f);
    CM_EXPECT(riskOf(p, 0, 1, 1.0f).inputs.persistentPeakSeverity == 1.0f);
}

CM_TEST(risk_group_and_master_paths) {
    CrowdMikeAudioProcessor p;
    auto& r = p.getRouting();
    r.clearAll();
    CM_EXPECT(!riskOf(p, 0).inputs.routeExists);
    r.setAssign(0, 2, 1.0f);
    CM_EXPECT(!riskOf(p, 0).inputs.routeExists);           // group goes nowhere yet
    r.setBus(RoutingState::busRowForMaster(0), 1, 1.0f);
    auto viaMaster = riskOf(p, 0);
    CM_EXPECT(viaMaster.inputs.routeExists && viaMaster.worstOutput == 1);
    setActual(p, Mod::Master, 0, MasterP::Level, 6.0f);
    CM_EXPECT_NEAR(riskOf(p, 0).inputs.busGainDb, 6.0, 0.05);
    CM_EXPECT(riskOf(p, 0, 0, 1.0f).inputs.persistentPeakSeverity == 1.0f);   // master tap, path via master
    setActual(p, Mod::Group, 2, GroupP::Mute, 1.0f);
    CM_EXPECT(!riskOf(p, 0).inputs.routeExists);
}

CM_TEST(risk_processor_updates_meters) {
    CrowdMikeAudioProcessor p;
    setActual(p, Mod::Input, 1, InputP::Trim, 12.0f);
    p.getShow().input(1).zone = "Stage";
    p.getShow().output(1).zone = "Stage";
    p.updateFeedbackRisk();
    auto& m = p.getMeters();
    CM_EXPECT(m.inRisk[0].load() > 0.2f);
    CM_EXPECT(m.inRisk[1].load() > m.inRisk[0].load());
    CM_EXPECT(m.inRisk[1].load() >= dsp::risk::kHighThreshold);
    CM_EXPECT(m.inRisk[5].load() == 0.0f);                 // inactive input
}

CM_TEST(risk_analysis_thread_runs_only_while_acquired) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    auto& t = p.getAnalysisThread();
    CM_EXPECT(!t.isActive());
    p.setFeedbackAnalysisActive(true);
    p.setFeedbackAnalysisActive(true);
    CM_EXPECT(t.isActive());
    for (int k = 0; k < 100 && t.getRunCount() < 2; ++k) juce::Thread::sleep(10);
    CM_EXPECT(t.getRunCount() >= 2);
    p.setFeedbackAnalysisActive(false);
    CM_EXPECT(t.isActive());                                // still one user
    p.setFeedbackAnalysisActive(false);
    CM_EXPECT(!t.isActive());
    const auto runs = t.getRunCount();
    juce::Thread::sleep(80);
    CM_EXPECT(t.getRunCount() == runs);
}
