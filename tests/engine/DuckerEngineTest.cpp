// Performer ducker wired through the whole engine (docs/DUCKER_SPEC.md sections 4 and 8).
#include "TestHarness.h"
#include "Control/OscAddress.h"
#include "Engine/TapMap.h"
#include "PluginProcessor.h"
#include <cmath>
#include <functional>

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

void setParam(CrowdMikeAudioProcessor& p, const juce::String& id, float actual) {
    auto* q = p.getParameters().getParameter(id);
    CM_EXPECT(q != nullptr);
    if (q != nullptr) q->setValueNotifyingHost(q->convertTo0to1(actual));
}
float getParam(CrowdMikeAudioProcessor& p, const juce::String& id) {
    auto* q = p.getParameters().getParameter(id);
    return q != nullptr ? q->convertFrom0to1(q->getValue()) : -999.0f;
}
bool layout(CrowdMikeAudioProcessor& p, int in, int out) {
    auto l = p.getBusesLayout();
    l.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(in);
    l.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(out);
    return p.setBusesLayout(l);
}
float tone(double hz, double amp, long n) { return static_cast<float>(amp * std::sin(6.283185307179586 * hz * static_cast<double>(n) / kSr)); }
double rmsDb(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += static_cast<double>(x) * x;
    return 10.0 * std::log10(std::max(s / std::max<size_t>(1, v.size()), 1.0e-20));
}

// 4 in / 4 out rig. Input 1 (index 0) is the performer key and is not routed anywhere; input 2 (index 1) is a crowd mic feeding
// group 1. Group 1 L -> out 1, Delay return L -> out 3, master L -> out 2.
struct Rig {
    CrowdMikeAudioProcessor p;
    long pos = 0;
    Rig() {
        CM_EXPECT(layout(p, 4, 4));
        p.prepareToPlay(kSr, kBlock);
        auto& r = p.getRouting();
        r.clearAll();
        r.setHasSavedDirect(true);
        r.setAssign(1, 0, 1.0f);
        r.setBus(RoutingState::busRowForGroup(0, 0), 0, 1.0f);
        r.setBus(RoutingState::busRowForMaster(0), 1, 1.0f);
        r.setBus(RoutingState::busRowForFx(0, 0), 2, 1.0f);
        setParam(p, "input2.pan", -1.0f);               // hard left: group L carries the crowd mic at unity
        setParam(p, "master.limOn", 0.0f);
        setParam(p, "group1.fxSend1", 0.0f);
        setParam(p, "fx.delay.mix", 1.0f);
        setParam(p, "fx.delay.timeMs", 100.0f);
    }
    // Runs `seconds`; gen(channel, sampleIndex) supplies input samples. Returns the output channels' samples of the last `tail` seconds.
    std::vector<std::vector<float>> run(double seconds, const std::function<float(int, long)>& gen, double tail = 0.25) {
        juce::AudioBuffer<float> b(4, kBlock);
        juce::MidiBuffer midi;
        std::vector<std::vector<float>> out(4);
        const long total = static_cast<long>(seconds * kSr);
        const long keepFrom = total - static_cast<long>(tail * kSr);
        for (long done = 0; done < total; done += kBlock) {
            for (int c = 0; c < 4; ++c) for (int s = 0; s < kBlock; ++s) b.setSample(c, s, gen(c, pos + s));
            p.processBlock(b, midi);
            for (int s = 0; s < kBlock; ++s)
                if (done + s >= keepFrom) for (int c = 0; c < 4; ++c) out[static_cast<size_t>(c)].push_back(b.getSample(c, s));
            pos += kBlock;
        }
        return out;
    }
};
auto keyAndCrowd(float keyAmp = 0.3f, float crowdAmp = 0.05f) {
    return [=](int c, long n) { return c == 0 ? tone(1000.0, keyAmp, n) : (c == 1 ? tone(300.0, crowdAmp, n) : 0.0f); };
}
void enableDucker(CrowdMikeAudioProcessor& p, float depth, int target = 2) {
    setParam(p, "duckKey", 1.0f);
    setParam(p, "duckDepthDb", depth);
    setParam(p, "duckTarget", static_cast<float>(target));
    setParam(p, "duckThrDb", -35.0f);
    setParam(p, "duckAttackMs", 5.0f);
    setParam(p, "duckOn", 1.0f);
}
}

CM_TEST(duck_engine_groups_ducked_by_depth) {
    double refDb = 0.0, duckDb = 0.0;
    {   Rig r; refDb = rmsDb(r.run(1.5, keyAndCrowd())[0]); }
    {   Rig r; enableDucker(r.p, -12.0f, DuckTargetGroups); duckDb = rmsDb(r.run(1.5, keyAndCrowd())[0]);
        CM_EXPECT_NEAR(r.p.getMeters().duckGrDb.load(), 12.0, 0.05);
        CM_EXPECT(r.p.getMeters().duckOpen.load()); }
    CM_EXPECT(refDb > -40.0);
    CM_EXPECT_NEAR(duckDb - refDb, -12.0, 0.15);
}

CM_TEST(duck_engine_target_selection) {
    struct Levels { double group, fx, master; };
    const auto measure = [](int target, bool on) {
        Rig r;
        if (on) enableDucker(r.p, -12.0f, target);
        const auto out = r.run(2.0, keyAndCrowd(0.3f, 0.2f));
        return Levels { rmsDb(out[0]), rmsDb(out[2]), rmsDb(out[1]) };
    };
    const auto ref = measure(2, false);
    CM_EXPECT(ref.group > -30.0 && ref.fx > -50.0);
    const auto groups = measure(DuckTargetGroups, true);
    CM_EXPECT_NEAR(groups.group - ref.group, -12.0, 0.2);
    CM_EXPECT_NEAR(groups.fx - ref.fx, 0.0, 0.2);                 // sends are taken before the duck, so the return is unchanged
    const auto fx = measure(DuckTargetFx, true);
    CM_EXPECT_NEAR(fx.group - ref.group, 0.0, 0.2);
    CM_EXPECT_NEAR(fx.fx - ref.fx, -12.0, 0.2);
    const auto both = measure(DuckTargetBoth, true);
    CM_EXPECT_NEAR(both.group - ref.group, -12.0, 0.2);
    CM_EXPECT_NEAR(both.fx - ref.fx, -12.0, 0.2);
    CM_EXPECT(both.master < ref.master - 5.0);
}

CM_TEST(duck_engine_exclude_key_keeps_the_performer_group_open) {
    const auto level = [](bool exclude) {
        Rig r;
        r.p.getRouting().setAssign(0, 0, 1.0f);                   // the key input is also a member of group 1
        setParam(r.p, "input1.pan", -1.0f);
        enableDucker(r.p, -12.0f, DuckTargetGroups);
        setParam(r.p, "duckExcludeKey", exclude ? 1.0f : 0.0f);
        return rmsDb(r.run(1.5, keyAndCrowd())[0]);
    };
    double ref;
    {   Rig r; r.p.getRouting().setAssign(0, 0, 1.0f); setParam(r.p, "input1.pan", -1.0f); ref = rmsDb(r.run(1.5, keyAndCrowd())[0]); }
    CM_EXPECT_NEAR(level(true) - ref, 0.0, 0.1);
    CM_EXPECT_NEAR(level(false) - ref, -12.0, 0.2);
}

CM_TEST(duck_engine_none_key_or_missing_key_input_never_ducks) {
    Rig r;
    enableDucker(r.p, -12.0f);
    setParam(r.p, "duckKey", 0.0f);                               // None
    r.run(0.5, keyAndCrowd());
    CM_EXPECT_NEAR(r.p.getMeters().duckGrDb.load(), 0.0, 1e-6);
    setParam(r.p, "duckKey", 9.0f);                               // Input 9: not active in a 4-channel layout
    r.run(0.5, keyAndCrowd());
    CM_EXPECT_NEAR(r.p.getMeters().duckGrDb.load(), 0.0, 1e-6);
}

CM_TEST(duck_engine_key_is_post_trim_and_ignores_mute_and_polarity) {
    // Key tone at -40 dBFS raw is below the -35 dB threshold; +12 dB trim on the key input lifts it above (post-trim detection).
    Rig quiet;
    enableDucker(quiet.p, -12.0f);
    quiet.run(1.0, keyAndCrowd(0.01f));
    CM_EXPECT_NEAR(quiet.p.getMeters().duckGrDb.load(), 0.0, 1e-6);
    Rig lifted;
    enableDucker(lifted.p, -12.0f);
    setParam(lifted.p, "input1.trimDb", 12.0f);
    setParam(lifted.p, "input1.mute", 1.0f);                      // muting the key input does not stop detection
    setParam(lifted.p, "input1.polarity", 1.0f);
    lifted.run(1.0, keyAndCrowd(0.01f));
    CM_EXPECT_NEAR(lifted.p.getMeters().duckGrDb.load(), 12.0, 0.05);
}

CM_TEST(duck_engine_recorder_taps_are_not_ducked_but_master_is) {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_duck_tap_test");
    struct Taps { double raw, proc, group, master; };
    const auto record = [&](bool duck) {
        folder.deleteRecursively();
        Rig r;
        auto& rs = r.p.getShow().recorder;
        rs.folder = folder.getFullPathName();
        rs.format = 2;
        rs.pattern = "{source}";
        rs.tracks[0] = { TapMap::forRawInput(1), 1, true, false, false, "tapraw" };
        rs.tracks[1] = { TapMap::forProcessedInput(1), 1, true, false, false, "tapproc" };
        rs.tracks[2] = { TapMap::forGroup(0), 2, true, false, false, "tapgroup" };
        rs.tracks[3] = { TapMap::Master, 2, true, false, false, "tapmaster" };
        r.p.applyShowChanges();
        if (duck) enableDucker(r.p, -12.0f);
        r.run(0.5, keyAndCrowd());                                // let the ducker settle before the take
        CM_EXPECT(r.p.startRecording());
        r.run(1.0, keyAndCrowd());
        r.p.getRecorder().stopRecordingNow();
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        Taps t { -999, -999, -999, -999 };
        for (auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.wav")) {
            std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
            if (rd == nullptr || rd->lengthInSamples < 24000) continue;
            juce::AudioBuffer<float> b(static_cast<int>(rd->numChannels), 24000);
            rd->read(&b, 0, 24000, rd->lengthInSamples - 24000, true, true);
            std::vector<float> v(b.getReadPointer(0), b.getReadPointer(0) + 24000);
            const double d = rmsDb(v);
            const auto n = f.getFileName();
            if (n.contains("tapraw")) t.raw = d; else if (n.contains("tapproc")) t.proc = d;
            else if (n.contains("tapgroup")) t.group = d; else if (n.contains("tapmaster")) t.master = d;
        }
        folder.deleteRecursively();
        return t;
    };
    const auto off = record(false), on = record(true);
    CM_EXPECT(off.raw > -100.0 && off.group > -100.0 && off.master > -100.0);
    CM_EXPECT_NEAR(on.raw, off.raw, 0.05);
    CM_EXPECT_NEAR(on.proc, off.proc, 0.05);
    CM_EXPECT_NEAR(on.group, off.group, 0.05);                    // group tap keeps the pre-duck bus
    CM_EXPECT_NEAR(on.master - off.master, -12.0, 0.3);           // master tap records what the PA gets
}

CM_TEST(duck_engine_panic_and_bypass_interplay) {
    {   // panic: output silent, ducker keeps tracking, nothing is unmuted when panic is released
        Rig r;
        enableDucker(r.p, -12.0f);
        r.run(1.0, keyAndCrowd());
        setParam(r.p, "panic", 1.0f);
        const auto out = r.run(0.5, keyAndCrowd(), 0.1);
        for (int c = 0; c < 4; ++c) for (float v : out[static_cast<size_t>(c)]) CM_EXPECT(std::abs(v) < 1e-5f);
        CM_EXPECT_NEAR(r.p.getMeters().duckGrDb.load(), 12.0, 0.05);
        setParam(r.p, "panic", 0.0f);
        const auto back = r.run(1.0, keyAndCrowd());
        Rig ref;
        enableDucker(ref.p, -12.0f);
        const auto refOut = ref.run(1.0, keyAndCrowd());
        CM_EXPECT_NEAR(rmsDb(back[0]) - rmsDb(refOut[0]), 0.0, 0.1);
    }
    {   // bypass: dry path (raw input) is never ducked and the ducker releases
        Rig r;
        enableDucker(r.p, -12.0f);
        setParam(r.p, "duckReleaseMs", 50.0f);
        setParam(r.p, "duckHoldMs", 0.0f);
        r.run(1.0, keyAndCrowd());
        CM_EXPECT_NEAR(r.p.getMeters().duckGrDb.load(), 12.0, 0.05);
        setParam(r.p, "bypass", 1.0f);
        const auto out = r.run(0.6, keyAndCrowd());
        CM_EXPECT_NEAR(r.p.getMeters().duckGrDb.load(), 0.0, 0.01);
        std::vector<float> dryRef;
        for (long n = 0; n < 12000; ++n) dryRef.push_back(tone(1000.0, 0.3, n));
        CM_EXPECT_NEAR(rmsDb(out[0]), rmsDb(dryRef), 0.05);       // channel 0 passes the key input untouched
    }
}

CM_TEST(duck_engine_lookahead_delays_the_buses_exactly) {
    const auto impulseIndex = [](float lookMs) {
        Rig r;
        setParam(r.p, "duckKey", 1.0f);
        setParam(r.p, "duckOn", 1.0f);
        setParam(r.p, "duckLookaheadMs", lookMs);
        r.run(1.0, [](int, long) { return 0.0f; });               // settle the delay glide
        long first = -1;
        const long at = r.pos + 1000;
        const auto out = r.run(0.2, [&](int c, long n) { return (c == 1 && n == at) ? 0.5f : 0.0f; }, 0.2);
        const long startOfCapture = r.pos - static_cast<long>(0.2 * kSr);
        for (size_t i = 0; i < out[1].size() && first < 0; ++i) if (std::abs(out[1][i]) > 0.05f) first = startOfCapture + static_cast<long>(i);
        return first - at;
    };
    const long base = impulseIndex(0.0f);
    const long delayed = impulseIndex(5.0f);
    CM_EXPECT(base >= 0);
    CM_EXPECT(delayed - base == static_cast<long>(std::lround(0.005 * kSr)));
}

CM_TEST(duck_engine_state_scene_macro_osc_midi_addressing) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, kBlock);
    // OSC
    const auto act = resolveOsc("/crowdmike/global/duckThrDb", { -20.0f });
    CM_EXPECT(act.index == ParamBank::index(Mod::Global, 0, duckBase() + DuckP::Thr));
    p.getHub().handleOsc("/crowdmike/global/duckThrDb", { -20.0f });
    CM_EXPECT_NEAR(getParam(p, "duckThrDb"), -20.0, 0.11);
    p.getHub().handleOsc("/crowdmike/param/duckOn", { 1.0f });
    CM_EXPECT(getParam(p, "duckOn") > 0.5f);
    // MIDI binding (toggle) on a ducker parameter
    ControlBinding b;
    b.type = MidiMsgType::CC; b.channel = 1; b.number = 21; b.target = BindTarget::Param;
    b.targetIndex = ParamBank::index(Mod::Global, 0, duckBase() + DuckP::ExcludeKey);
    b.mode = BindMode::Toggle;
    p.getShow().bindings.push_back(b);
    const float before = getParam(p, "duckExcludeKey");
    MidiEvent e; e.type = MidiMsgType::CC; e.channel = 1; e.number = 21; e.value = 127; e.isOn = true;
    p.getHub().handleMidi(e);
    CM_EXPECT((getParam(p, "duckExcludeKey") > 0.5f) != (before > 0.5f));
    // macro modulates the depth
    setParam(p, "duckDepthDb", -6.0f);
    auto& macros = p.getShow().macros.macros[0];
    macros.count = 1;
    macros.dests[0].paramIndex = ParamBank::index(Mod::Global, 0, duckBase() + DuckP::Depth);
    macros.dests[0].min01 = 0.0f; macros.dests[0].max01 = 1.0f;
    p.applyShowChanges();
    setParam(p, "macro1", 0.5f);
    juce::AudioBuffer<float> blk(2, kBlock);
    juce::MidiBuffer midi;
    blk.clear();
    p.processBlock(blk, midi);
    CM_EXPECT_NEAR(p.getBank().effectiveAt(macros.dests[0].paramIndex), -15.0, 0.2);
    macros.count = 0;
    p.applyShowChanges();
    // scenes: capture, change, recall
    setParam(p, "duckDepthDb", -9.0f);
    setParam(p, "duckKey", 3.0f);
    p.getScenes().capture(0, "duck");
    setParam(p, "duckDepthDb", -3.0f);
    setParam(p, "duckKey", 0.0f);
    CM_EXPECT(p.getScenes().recall(0, ScopeAll, 10.0f));
    CM_EXPECT_NEAR(getParam(p, "duckDepthDb"), -9.0, 0.11);
    CM_EXPECT(std::lround(getParam(p, "duckKey")) == 3);
    // state round trip
    setParam(p, "duckLookaheadMs", 4.0f);
    setParam(p, "duckHoldMs", 321.0f);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    CrowdMikeAudioProcessor q;
    q.prepareToPlay(kSr, kBlock);
    q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CM_EXPECT_NEAR(getParam(q, "duckLookaheadMs"), 4.0, 0.11);
    CM_EXPECT_NEAR(getParam(q, "duckHoldMs"), 321.0, 1.1);
    CM_EXPECT(std::lround(getParam(q, "duckKey")) == 3);
}

CM_TEST(duck_engine_regression_state_without_ducker_ids_loads_with_defaults) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(kSr, kBlock);
    setParam(p, "input1.trimDb", -7.0f);
    setParam(p, "master.levelDb", -3.0f);
    setParam(p, "limiterCeilingDb", -2.0f);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    CM_EXPECT(xml != nullptr);
    if (xml == nullptr) return;
    for (int i = xml->getNumChildElements() - 1; i >= 0; --i) {
        auto* c = xml->getChildElement(i);
        if (c->hasTagName("PARAM") && c->getStringAttribute("id").startsWith("duck")) xml->removeChildElement(c, true);
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    CrowdMikeAudioProcessor q;
    q.prepareToPlay(kSr, kBlock);
    q.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CM_EXPECT_NEAR(getParam(q, "input1.trimDb"), -7.0, 0.02);
    CM_EXPECT_NEAR(getParam(q, "master.levelDb"), -3.0, 0.11);
    CM_EXPECT_NEAR(getParam(q, "limiterCeilingDb"), -2.0, 0.11);
    CM_EXPECT(getParam(q, "duckOn") < 0.5f);
    CM_EXPECT(std::lround(getParam(q, "duckKey")) == 0);
    CM_EXPECT_NEAR(getParam(q, "duckDepthDb"), -6.0, 0.11);
    CM_EXPECT_NEAR(getParam(q, "duckReleaseMs"), 400.0, 1.1);
    CM_EXPECT(getParam(q, "duckExcludeKey") > 0.5f);
}
