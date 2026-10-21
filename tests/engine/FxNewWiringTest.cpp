// Engine wiring of FX slots 6 (Stadium Reverb) and 7 (Crowd Multiplier): sends, returns, bus rows 30..33, taps, meters,
// persistence of the appended rows, scene/preset coverage. (docs/EFFECTS_SPEC.md sections 19-20, ROUTING_SPEC section 3.)
#include "TestHarness.h"
#include "Core/PresetManager.h"
#include "Core/SceneBank.h"
#include "Engine/TapMap.h"
#include "PluginProcessor.h"
#include <cmath>

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
constexpr double kSr = 48000.0;

void setParam(CrowdMikeAudioProcessor& p, const char* id, float actual) {
    auto* q = p.getParameters().getParameter(id);
    CM_EXPECT(q != nullptr);
    if (q != nullptr) q->setValueNotifyingHost(q->convertTo0to1(actual));
}

// Click on input 1 at block 5; energy of output 1 in (0, lateFromMs) and [lateFromMs, 900 ms) after the click.
struct ClickRun { double early = 0.0, late = 0.0; bool finite = true; };
ClickRun clickRun(CrowdMikeAudioProcessor& p, double lateFromMs) {
    juce::AudioBuffer<float> b(2, 256);
    juce::MidiBuffer midi;
    ClickRun r;
    const int total = static_cast<int>(kSr * 0.9 / 256.0);
    for (int blk = 0; blk < total; ++blk) {
        b.clear();
        if (blk == 5) b.setSample(0, 0, 0.8f);
        p.processBlock(b, midi);
        for (int s = 0; s < 256; ++s) {
            const float v = b.getSample(0, s);
            if (!std::isfinite(v)) r.finite = false;
            const double tMs = (blk * 256 + s - 5 * 256) * 1000.0 / kSr;
            if (tMs > 0.0 && tMs < lateFromMs) r.early += static_cast<double>(v) * v;
            if (tMs >= lateFromMs) r.late += static_cast<double>(v) * v;
        }
    }
    return r;
}
void setupReverb(CrowdMikeAudioProcessor& p) {
    p.prepareToPlay(kSr, 256);
    p.applyWiringTemplate(1);
    setParam(p, "fx.reverb.on", 1.0f);
    setParam(p, "fx.reverb.decay", 2.0f);
    setParam(p, "fx.reverb.wetDb", 0.0f);
    setParam(p, "fx.reverb.preDelayMs", 0.0f);
}
}

CM_TEST(fxnew_layout_constants_and_bus_rows) {
    CM_EXPECT(kNumFx == 7 && kNumLegacyFx == 5);
    CM_EXPECT(kBusSrcCount == 34 && kBusSrcLegacyCount == 30 && kBusSrcFxExtraBase == 30);
    CM_EXPECT(kBusSrcPlayback == 26 && kBusSrcMaster == 28);                  // legacy indices are frozen
    CM_EXPECT(RoutingState::busRowForFx(0, 0) == 16 && RoutingState::busRowForFx(4, 1) == 25);
    CM_EXPECT(RoutingState::busRowForFx(5, 0) == 30 && RoutingState::busRowForFx(5, 1) == 31);
    CM_EXPECT(RoutingState::busRowForFx(6, 0) == 32 && RoutingState::busRowForFx(6, 1) == 33);
    for (int f = 0; f < kNumFx; ++f)
        for (int ch = 0; ch < 2; ++ch) {
            const int row = busRowForFxReturn(f, ch);
            CM_EXPECT(busRowIsFxReturn(row) && busRowFxIndex(row) == f);
        }
    CM_EXPECT(!busRowIsFxReturn(kBusSrcPlayback) && !busRowIsFxReturn(kBusSrcMaster + 1) && !busRowIsFxReturn(0));
    CM_EXPECT(RoutingState::busRowName(30).contains("Reverb") && RoutingState::busRowName(33).contains("Multiplier"));
    // send parameters are appended after the legacy ones
    CM_EXPECT(inputSendParam(4) == InputP::Fx5 && inputSendParam(5) == InputP::Fx6 && inputSendParam(6) == InputP::Fx7);
    CM_EXPECT(groupSendParam(4) == GroupP::Fx5 && groupSendParam(6) == GroupP::Fx7);
    CM_EXPECT(InputP::Fx6 > InputP::FxPre && GroupP::Fx6 > GroupP::Fx5);
    // taps
    CM_EXPECT(TapMap::forFx(4) == TapMap::Fx + 4);
    CM_EXPECT(TapMap::forFx(5) == TapMap::FxExtra && TapMap::forFx(6) == TapMap::FxExtra + 1);
    CM_EXPECT(TapMap::fxSlotOf(TapMap::forFx(5)) == 5 && TapMap::fxSlotOf(TapMap::forFx(6)) == 6 && TapMap::fxSlotOf(TapMap::forFx(2)) == 2);
    CM_EXPECT(TapMap::Master == 45 && TapMap::Fx + 4 == 44);                  // persisted ids unchanged
    CM_EXPECT(TapMap::defaultName(TapMap::forFx(5)).contains("Reverb") && TapMap::defaultName(TapMap::forFx(6)).contains("Multiplier"));
}

CM_TEST(fxnew_parameters_exist_with_frozen_ids_and_defaults) {
    for (const char* id : { "fx.reverb.on", "fx.reverb.size", "fx.reverb.decay", "fx.reverb.preDelayMs", "fx.reverb.hfDampHz",
                            "fx.reverb.lowCutHz", "fx.reverb.diffusion", "fx.reverb.modDepth", "fx.reverb.modRate", "fx.reverb.width",
                            "fx.reverb.wetDb", "fx.reverb.freeze", "fx.reverb.tailKill", "fx.reverb.returnDb",
                            "fx.multi.on", "fx.multi.voices", "fx.multi.depthMs", "fx.multi.rateHz", "fx.multi.detuneCents",
                            "fx.multi.scatter", "fx.multi.tone", "fx.multi.seed", "fx.multi.levelDb", "fx.multi.returnDb",
                            "input1.fxSend6", "input16.fxSend7", "group1.fxSend6", "group8.fxSend7" })
        CM_EXPECT(ParamBank::indexForId(id) >= 0);
    CrowdMikeAudioProcessor p;
    auto def = [&](const char* id) { auto* q = p.getParameters().getParameter(id); return q->convertFrom0to1(q->getDefaultValue()); };
    CM_EXPECT(def("fx.reverb.on") == 0.0f && def("fx.multi.on") == 0.0f);
    CM_EXPECT(def("input1.fxSend6") <= -79.9f && def("group3.fxSend7") <= -79.9f);
    CM_EXPECT(ParamBank::baseIndex(Mod::FxReverb) > ParamBank::baseIndex(Mod::Macro));   // new modules only append
    CM_EXPECT(ParamBank::idForIndex(ParamBank::index(Mod::FxMulti, 0, MultiP::Voices)) == "fx.multi.voices");
    CM_EXPECT(ParamBank::idForIndex(ParamBank::index(Mod::Input, 3, inputSendParam(6))) == "input4.fxSend7");
}

CM_TEST(fxnew_group_send_reaches_reverb_through_the_engine) {
    CrowdMikeAudioProcessor p;
    setupReverb(p);
    const auto closed = clickRun(p, 120.0);   // send closed: nothing
    CM_EXPECT(closed.finite);                 // (a little energy after 120 ms is the dry path itself, e.g. the output limiter release)
    CrowdMikeAudioProcessor q;
    setupReverb(q);
    setParam(q, "group1.fxSend6", 0.0f);
    const auto open = clickRun(q, 120.0);
    CM_EXPECT(open.finite);
    CM_EXPECT(open.late > 20.0 * closed.late + 1.0e-6);   // a reverb tail long after the click
    CM_EXPECT(q.getMeters().fxPeak[5].getAndReset() > 0.0f);
    CM_EXPECT(q.getMeters().fxPeak[6].getAndReset() == 0.0f);
    setParam(q, "fx.reverb.on", 0.0f);        // switching the effect off silences the return
    clickRun(q, 120.0);
    const auto off = clickRun(q, 120.0);
    CM_EXPECT(off.late < 2.0 * closed.late + 1.0e-9);
}

CM_TEST(fxnew_input_send_reaches_multiplier_through_the_engine) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 256);
    p.applyWiringTemplate(1);
    setParam(p, "fx.multi.on", 1.0f);
    setParam(p, "fx.multi.voices", 8.0f);
    setParam(p, "input1.fxSend7", 0.0f);
    // the multiplier adds delayed copies a few ms after the click and nothing later than ~60 ms
    const auto r = clickRun(p, 60.0);
    CM_EXPECT(r.finite);
    CM_EXPECT(r.early > 1.0e-4);
    CM_EXPECT(r.late < r.early * 0.01);
    CM_EXPECT(p.getMeters().fxPeak[6].getAndReset() > 0.0f);
}

CM_TEST(fxnew_returns_route_through_the_bus_matrix_and_persist) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 256);
    p.applyWiringTemplate(1);
    setParam(p, "fx.multi.on", 1.0f);
    setParam(p, "group1.fxSend7", 0.0f);
    auto& r = p.getRouting();
    CM_EXPECT(r.getBus(RoutingState::busRowForFx(6, 0), 0) == 0.0f);
    CM_EXPECT(r.setBus(RoutingState::busRowForFx(6, 0), 1, 1.0f));
    CM_EXPECT(r.setBus(RoutingState::busRowForFx(6, 1), 0, -0.5f));
    CM_EXPECT(r.getBus(32, 1) == 1.0f && r.getBus(33, 0) == -0.5f);
    CM_EXPECT(!r.setBus(kBusSrcCount, 0, 1.0f));
    const auto vt = r.toValueTree();
    CrowdMikeAudioProcessor q;
    CM_EXPECT(q.getRouting().fromValueTree(vt));
    CM_EXPECT(q.getRouting().getBus(32, 1) == 1.0f && q.getRouting().getBus(33, 0) == -0.5f);
    CM_EXPECT(q.getRouting().getBus(31, 1) == 0.0f);
    CM_EXPECT(vt.hasProperty("bus") && vt.hasProperty("busFx2"));   // legacy 30-row stride + appended rows
    auto legacy = vt.createCopy();                                  // a state saved by the 5-FX build
    legacy.removeProperty("busFx2", nullptr);
    CrowdMikeAudioProcessor w;
    CM_EXPECT(w.getRouting().fromValueTree(legacy));
    CM_EXPECT(w.getRouting().getBus(32, 1) == 0.0f);
    // the return reaches output 2 through bus row 32
    juce::AudioBuffer<float> b(2, 256);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int blk = 0; blk < 30; ++blk) {
        b.clear();
        if (blk == 2) b.setSample(0, 0, 0.8f);
        p.processBlock(b, midi);
        peak = std::max(peak, b.getMagnitude(1, 0, 256));
    }
    CM_EXPECT(peak > 1.0e-4f);
}

CM_TEST(fxnew_scene_and_preset_coverage) {
    CM_EXPECT(SceneBank::isTransientParam(ParamBank::index(Mod::FxReverb, 0, ReverbP::Freeze)));
    CM_EXPECT(SceneBank::isTransientParam(ParamBank::index(Mod::FxReverb, 0, ReverbP::TailKill)));
    CM_EXPECT(!SceneBank::isTransientParam(ParamBank::index(Mod::FxReverb, 0, ReverbP::Decay)));
    CM_EXPECT(!SceneBank::isTransientParam(ParamBank::index(Mod::FxMulti, 0, MultiP::Voices)));
    int reverbPresets = 0, multiPresets = 0;
    for (const auto& f : PresetManager::factoryPresets()) {
        if (juce::String(f.target) == "reverb") ++reverbPresets;
        if (juce::String(f.target) == "multi") ++multiPresets;
    }
    CM_EXPECT(reverbPresets >= 3 && multiPresets >= 3);
    CrowdMikeAudioProcessor p;
    for (const auto* f : PresetManager::factoryPresetsFor("reverb"))
        CM_EXPECT(PresetManager::applyFactory(p.getParameters(), *f, Mod::FxReverb, 0) >= 0);
    setParam(p, "fx.reverb.decay", 4.0f);
    p.getScenes().capture(0, "S1");
    setParam(p, "fx.reverb.decay", 9.0f);
    setParam(p, "group2.fxSend6", -10.0f);
    CM_EXPECT(p.getScenes().recall(0));
    auto* d = p.getParameters().getParameter("fx.reverb.decay");
    CM_EXPECT(std::fabs(d->convertFrom0to1(d->getValue()) - 4.0f) < 0.01f);
}

CM_TEST(fxnew_tail_length_and_stress_stay_finite) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, 128);
    setParam(p, "fx.reverb.on", 1.0f); setParam(p, "fx.reverb.decay", 20.0f);
    setParam(p, "fx.multi.on", 1.0f); setParam(p, "fx.multi.voices", 16.0f);
    for (int i = 1; i <= 16; ++i) {
        setParam(p, (juce::String("input") + juce::String(i) + ".fxSend6").toRawUTF8(), -3.0f);
        setParam(p, (juce::String("input") + juce::String(i) + ".fxSend7").toRawUTF8(), -3.0f);
    }
    juce::AudioBuffer<float> b(16, 128);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int blk = 0; blk < 400; ++blk) {
        for (int c = 0; c < 16; ++c)
            for (int s = 0; s < 128; ++s) b.setSample(c, s, 0.5f * std::sin(0.01f * static_cast<float>(blk * 128 + s) * static_cast<float>(c + 1)));
        p.processBlock(b, midi);
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int s = 0; s < 128; ++s) finite = finite && std::isfinite(b.getSample(c, s));
    }
    CM_EXPECT(finite);
    CM_EXPECT(p.getTailLengthSeconds() >= 20.0);
}
