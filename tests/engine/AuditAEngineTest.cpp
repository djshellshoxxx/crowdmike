// Audit A (docs/feature-audit/part-A-engine.md): regression tests for the gaps found while tracing every engine / parameter /
// state requirement and for the merge seams between the independently developed features (ducker x new FX returns,
// tempo x FX sync, panic x Stadium Reverb, playback level, feedback-risk x FX sends, state without scenes).
#include "TestHarness.h"
#include "Core/StateIO.h"
#include "Engine/InputRisk.h"
#include "Engine/TapMap.h"
#include "PluginProcessor.h"
#include "UiTestUtil.h"
#include <cmath>
#include <functional>
#include <set>

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
constexpr double kSr = 48000.0;

void setParam(CrowdMikeAudioProcessor& p, const juce::String& id, float actual) {
    auto* q = p.getParameters().getParameter(id);
    CM_EXPECT(q != nullptr);
    if (q != nullptr) q->setValueNotifyingHost(q->convertTo0to1(actual));
}
bool layout(CrowdMikeAudioProcessor& p, int in, int out) {
    auto l = p.getBusesLayout();
    l.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(in);
    l.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(out);
    return p.setBusesLayout(l);
}
double rmsDb(const std::vector<float>& v) {
    double s = 0.0;
    for (float x : v) s += static_cast<double>(x) * x;
    return 10.0 * std::log10(std::max(s / static_cast<double>(std::max<size_t>(1, v.size())), 1.0e-20));
}
float tone(double hz, double amp, long n) { return static_cast<float>(amp * std::sin(6.283185307179586 * hz * static_cast<double>(n) / kSr)); }

// Generic multichannel runner: gen(channel, absoluteSample) -> input, returns the output of the last `tail` seconds.
struct Runner {
    CrowdMikeAudioProcessor p;
    int ins, outs, block;
    long pos = 0;
    Runner(int i, int o, int blk = 256) : ins(i), outs(o), block(blk) {
        CM_EXPECT(layout(p, i, o));
        p.prepareToPlay(kSr, blk);
        p.getRouting().clearAll();
        p.getRouting().setHasSavedDirect(true);
    }
    std::vector<std::vector<float>> run(double seconds, const std::function<float(int, long)>& gen, double tail = 0.25) {
        juce::AudioBuffer<float> b(std::max(ins, outs), block);
        juce::MidiBuffer midi;
        std::vector<std::vector<float>> out(static_cast<size_t>(outs));
        const long total = static_cast<long>(seconds * kSr);
        const long keepFrom = total - static_cast<long>(tail * kSr);
        for (long done = 0; done < total; done += block) {
            b.clear();
            for (int c = 0; c < ins; ++c) for (int s = 0; s < block; ++s) b.setSample(c, s, gen(c, pos + s));
            p.processBlock(b, midi);
            for (int s = 0; s < block; ++s)
                if (done + s >= keepFrom) for (int c = 0; c < outs; ++c) out[static_cast<size_t>(c)].push_back(b.getSample(c, s));
            pos += block;
        }
        return out;
    }
};
}

// ---------------------------------------------------------------- playback level
CM_TEST(audit_playback_level_scales_clips_on_every_destination) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_audit_pblevel");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto wav = dir.getChildFile("loop.wav");
    CM_EXPECT(uitest::writeSineWav(wav, kSr, 96000, 2, 0.2f));

    const auto measure = [&](int route, float levelDb) {
        Runner r(2, 2);
        auto& rt = r.p.getRouting();
        rt.setBus(route == 0 ? RoutingState::busRowForMaster(0) : RoutingState::busRowForPlayback(0), 0, 1.0f);
        CM_EXPECT(r.p.loadClipFile(0, wav));
        CM_EXPECT(r.p.getPlayback().waitForLoads(5000));
        rec::PlaybackEngine::ClipSettings s;
        s.mode = rec::PlaybackEngine::ClipSettings::Loop;
        s.route = route;
        r.p.setClipSettings(0, s);
        CM_EXPECT(r.p.getPlayback().waitForLoads(5000));
        setParam(r.p, "playbackLevelDb", levelDb);
        setParam(r.p, "master.limOn", 0.0f);
        CM_EXPECT(r.p.triggerClip(0));
        const auto out = r.run(1.0, [](int, long) { return 0.0f; });
        return rmsDb(out[0]);
    };
    for (const int route : { 0, 9 }) {
        const double unity = measure(route, 0.0f);
        CM_EXPECT(unity > -30.0);
        CM_EXPECT_NEAR(measure(route, -12.0f) - unity, -12.0, 0.1);
        CM_EXPECT_NEAR(measure(route, 6.0f) - unity, 6.0, 0.1);
        CM_EXPECT(measure(route, -80.0f) - unity < -50.0);                 // bottom of the range is (near) silence
    }
    dir.deleteRecursively();
}

// ---------------------------------------------------------------- panic x Stadium Reverb
CM_TEST(audit_panic_kills_a_frozen_reverb_tail_so_it_cannot_resurface) {
    const auto tailAfterPanic = [](bool panic) {
        Runner r(2, 2);
        r.p.getRouting().setBus(RoutingState::busRowForMaster(0), 0, 1.0f);
        setParam(r.p, "master.limOn", 0.0f);
        setParam(r.p, "fx.reverb.on", 1.0f);
        setParam(r.p, "fx.reverb.decay", 8.0f);
        setParam(r.p, "fx.reverb.wetDb", 0.0f);
        setParam(r.p, "input1.fxSend6", 0.0f);
        // pseudo noise excites the network, then the tail is frozen and the input removed
        const auto noise = [](int c, long n) {
            if (c != 0 || n > static_cast<long>(0.6 * kSr)) return 0.0f;
            std::uint32_t x = static_cast<std::uint32_t>(n) * 2654435761u + 12345u;
            x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
            return 0.3f * (static_cast<float>(x & 0xFFFFu) / 32768.0f - 1.0f);
        };
        r.run(0.7, noise);
        setParam(r.p, "fx.reverb.freeze", 1.0f);
        const double held = rmsDb(r.run(0.5, noise, 0.2)[0]);
        if (panic) { setParam(r.p, "panic", 1.0f); r.run(0.3, noise, 0.1); setParam(r.p, "panic", 0.0f); }
        const double after = rmsDb(r.run(0.5, noise, 0.2)[0]);
        return std::make_pair(held, after);
    };
    const auto ref = tailAfterPanic(false);
    CM_EXPECT(ref.first > -60.0);
    CM_EXPECT_NEAR(ref.second - ref.first, 0.0, 1.0);                        // a frozen tail holds its level
    const auto killed = tailAfterPanic(true);
    CM_EXPECT(killed.first > -60.0);
    CM_EXPECT(killed.second < killed.first - 40.0);                          // after panic the held tail is gone
}

// ---------------------------------------------------------------- ducker x FX 6 / 7 returns
CM_TEST(audit_ducker_ducks_the_new_fx_returns_and_keeps_their_recorder_taps_clean) {
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_audit_duck_fx");
    struct Out { double verb, multi, tapVerb, tapMulti; };
    const auto crowd = [](int c, long n) { return c == 0 ? tone(1000.0, 0.3, n) : (c == 1 ? tone(300.0, 0.2, n) : 0.0f); };
    const auto run = [&](int target, bool duck) {
        folder.deleteRecursively();
        Runner r(4, 4);
        auto& rt = r.p.getRouting();
        rt.setAssign(1, 0, 0.0f);
        rt.setBus(RoutingState::busRowForFx(5, 0), 2, 1.0f);                 // reverb return L -> out 3
        rt.setBus(RoutingState::busRowForFx(6, 0), 3, 1.0f);                 // multiplier return L -> out 4
        setParam(r.p, "input2.pan", -1.0f);
        setParam(r.p, "input2.fxSend6", 0.0f);
        setParam(r.p, "input2.fxSend7", 0.0f);
        setParam(r.p, "fx.reverb.on", 1.0f);
        setParam(r.p, "fx.multi.on", 1.0f);
        auto& rs = r.p.getShow().recorder;
        rs.folder = folder.getFullPathName();
        rs.format = 2;
        rs.pattern = "{source}";
        rs.tracks[0] = { TapMap::forFx(5), 2, true, false, false, "tapverb" };
        rs.tracks[1] = { TapMap::forFx(6), 2, true, false, false, "tapmulti" };
        r.p.applyShowChanges();
        if (duck) {
            setParam(r.p, "duckKey", 1.0f);
            setParam(r.p, "duckDepthDb", -12.0f);
            setParam(r.p, "duckTarget", static_cast<float>(target));
            setParam(r.p, "duckThrDb", -35.0f);
            setParam(r.p, "duckAttackMs", 5.0f);
            setParam(r.p, "duckOn", 1.0f);
        }
        r.run(1.5, crowd);
        CM_EXPECT(r.p.startRecording());
        const auto out = r.run(1.0, crowd);
        r.p.getRecorder().stopRecordingNow();
        Out o { rmsDb(out[2]), rmsDb(out[3]), -999.0, -999.0 };
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        for (auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.wav")) {
            std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(f));
            if (rd == nullptr || rd->lengthInSamples < 24000) continue;
            juce::AudioBuffer<float> b(static_cast<int>(rd->numChannels), 24000);
            rd->read(&b, 0, 24000, rd->lengthInSamples - 24000, true, true);
            const std::vector<float> v(b.getReadPointer(0), b.getReadPointer(0) + 24000);
            if (f.getFileName().contains("tapverb")) o.tapVerb = rmsDb(v);
            if (f.getFileName().contains("tapmulti")) o.tapMulti = rmsDb(v);
        }
        folder.deleteRecursively();
        return o;
    };
    const auto off = run(DuckTargetBoth, false);
    CM_EXPECT(off.verb > -70.0 && off.multi > -50.0 && off.tapVerb > -70.0 && off.tapMulti > -50.0);
    const auto both = run(DuckTargetBoth, true);
    CM_EXPECT_NEAR(both.verb - off.verb, -12.0, 0.3);                        // FX 6 return ducked
    CM_EXPECT_NEAR(both.multi - off.multi, -12.0, 0.3);                      // FX 7 return ducked
    CM_EXPECT_NEAR(both.tapVerb, off.tapVerb, 0.05);                         // their recorder taps keep the pre-duck return
    CM_EXPECT_NEAR(both.tapMulti, off.tapMulti, 0.05);
    const auto groupsOnly = run(DuckTargetGroups, true);
    CM_EXPECT_NEAR(groupsOnly.verb, off.verb, 0.2);                          // Target = crowd groups leaves every return alone
    CM_EXPECT_NEAR(groupsOnly.multi, off.multi, 0.2);
}

// ---------------------------------------------------------------- tempo x FX context with oversized host blocks
CM_TEST(audit_beat_synced_fx_see_the_same_ppq_for_oversized_host_blocks) {
    const auto render = [](int hostBlock) {
        CrowdMikeAudioProcessor p;
        p.prepareToPlay(kSr, 128);
        setParam(p, "tempo.source", 2.0f);                                   // Manual 97 BPM (beats fall between samples, so float noise cannot move a trigger)
        setParam(p, "tempo.manualBpm", 97.0f);
        setParam(p, "fx.reverse.mode", 0.0f);
        setParam(p, "fx.reverse.trigMode", 3.0f);                            // beat-sync trigger
        setParam(p, "fx.reverse.beatDiv", 0.0f);                             // every beat
        setParam(p, "fx.reverse.chunkSec", 0.25f);
        setParam(p, "fx.reverse.mix", 1.0f);
        setParam(p, "input1.fxSend4", 0.0f);
        setParam(p, "fx.reverse.returnDb", 0.0f);
        p.getRouting().clearAll();
        p.getRouting().setBus(RoutingState::busRowForMaster(0), 0, 1.0f);
        setParam(p, "master.limOn", 0.0f);
        juce::AudioBuffer<float> b(2, hostBlock);
        juce::MidiBuffer midi;
        std::vector<float> out;
        long pos = 0;
        for (long done = 0; done < 49152L + 147456L; done += hostBlock) {
            if (done == 49152L) p.tempoResync();                             // parameter ramps settled: both runs restart the grid together
            for (int s = 0; s < hostBlock; ++s) { const float v = tone(440.0, 0.3, pos + s); b.setSample(0, s, v); b.setSample(1, s, v); }
            p.processBlock(b, midi);
            if (done >= 49152L) for (int s = 0; s < hostBlock; ++s) out.push_back(b.getSample(0, s));
            pos += hostBlock;
        }
        return out;
    };
    const auto small = render(128), big = render(1024);                      // 1024 = 8 engine chunks per host block
    CM_EXPECT(small.size() == big.size());
    double maxDiff = 0.0, energy = 0.0;
    for (size_t i = 0; i < small.size(); ++i) {
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(small[i] - big[i])));
        energy += static_cast<double>(small[i]) * small[i];
    }
    CM_EXPECT(energy > 1.0);                                                 // the reverse effect actually produced signal
    CM_EXPECT(maxDiff < 1.0e-4);
}

// ---------------------------------------------------------------- feedback risk x effect sends
CM_TEST(audit_feedback_risk_follows_input_sends_into_fx_returns) {
    CrowdMikeAudioProcessor p;
    auto& r = p.getRouting();
    r.clearAll();
    const auto riskOf = [&](int i) {
        const InputRiskContext ctx { p.getBank(), p.getRouting(), p.getShow(), p.getActiveOutputChannelCount(), 0, 0.0f };
        return evaluateInputRisk(ctx, i);
    };
    const auto setActual = [&](Mod m, int inst, int id, float v) { p.getHub().setParamActual(ParamBank::index(m, inst, id), v); };
    CM_EXPECT(!riskOf(0).inputs.routeExists);
    // reverb return on its own bus row -> output 2, but the effect is off and the send closed: no route
    r.setBus(RoutingState::busRowForFx(5, 0), 1, 1.0f);
    CM_EXPECT(!riskOf(0).inputs.routeExists);
    setActual(Mod::Input, 0, inputSendParam(5), 0.0f);
    CM_EXPECT(!riskOf(0).inputs.routeExists);                                // reverb is still off
    setActual(Mod::FxReverb, 0, ReverbP::On, 1.0f);
    const auto verb = riskOf(0);
    CM_EXPECT(verb.inputs.routeExists && verb.worstOutput == 1);
    CM_EXPECT_NEAR(verb.inputs.groupGainDb, -3.0, 0.01);                     // send 0 dB + return 0 dB + wet -3 dB
    // the multiplier reaches the loudspeakers through the master bus (returns always sum into the master)
    r.clearAll();
    setActual(Mod::Input, 0, inputSendParam(5), -80.0f);
    setActual(Mod::Input, 0, inputSendParam(6), -6.0f);
    r.setBus(RoutingState::busRowForMaster(1), 0, 1.0f);
    CM_EXPECT(!riskOf(0).inputs.routeExists);                                // multiplier off
    setActual(Mod::FxMulti, 0, MultiP::On, 1.0f);
    setActual(Mod::Master, 0, MasterP::Level, 3.0f);
    const auto multi = riskOf(0);
    CM_EXPECT(multi.inputs.routeExists && multi.worstOutput == 0);
    CM_EXPECT_NEAR(multi.inputs.groupGainDb, -6.0, 0.01);
    CM_EXPECT_NEAR(multi.inputs.busGainDb, 3.0, 0.05);
    setActual(Mod::Master, 0, MasterP::Mute, 1.0f);
    CM_EXPECT(!riskOf(0).inputs.routeExists);                                // muted master breaks the path
    // a louder send scores higher
    setActual(Mod::Master, 0, MasterP::Mute, 0.0f);
    const float low = riskOf(0).result.score;
    setActual(Mod::Input, 0, inputSendParam(6), 6.0f);
    CM_EXPECT(riskOf(0).result.score > low);
}

// ---------------------------------------------------------------- state
CM_TEST(audit_state_without_scenes_clears_the_scenes_of_the_target_instance) {
    CrowdMikeAudioProcessor src;
    const auto xml = StateIO::serialize({ src.getParameters(), src.getRouting(), src.getShow(), src.getScenes() }, StateIO::EngineConfigInfo {});
    CM_EXPECT(xml != nullptr);
    if (xml == nullptr) return;
    juce::XmlElement noScenes(*xml);
    if (auto* sc = noScenes.getChildByName("Scenes")) noScenes.removeChildElement(sc, true);
    CM_EXPECT(noScenes.getChildByName("Scenes") == nullptr);

    CrowdMikeAudioProcessor dst;
    dst.getScenes().capture(3, "Leftover");
    CM_EXPECT(dst.getScenes().countUsed() == 1 && dst.getScenes().current() == 3);
    const auto res = StateIO::restore({ dst.getParameters(), dst.getRouting(), dst.getShow(), dst.getScenes() }, noScenes, 2, 2);
    CM_EXPECT(res == StateIO::Result::Ok);
    CM_EXPECT(dst.getScenes().countUsed() == 0);
    CM_EXPECT(dst.getScenes().current() == -1);
    // a state that does carry scenes still restores them
    CrowdMikeAudioProcessor withScenes;
    withScenes.getScenes().capture(5, "Kept");
    juce::MemoryBlock b2;
    withScenes.getStateInformation(b2);
    CrowdMikeAudioProcessor again;
    again.setStateInformation(b2.getData(), static_cast<int>(b2.getSize()));
    CM_EXPECT(again.getScenes().countUsed() == 1 && again.getScenes().at(5).name == "Kept");
}

// ---------------------------------------------------------------- tables that must cover every FX slot
CM_TEST(audit_fx_slot_tables_are_consistent_for_all_seven_slots) {
    CM_EXPECT(kNumFx == 7);
    std::set<juce::String> tapNames, rowNames, sendIds, groupSendIds;
    for (int id = 0; id < TapMap::Count; ++id) {
        const auto n = TapMap::defaultName(id);
        CM_EXPECT(n.isNotEmpty());
        CM_EXPECT(tapNames.insert(n).second);                                // every tap id has its own name
    }
    for (int f = 0; f < kNumFx; ++f) {
        CM_EXPECT(TapMap::fxSlotOf(TapMap::forFx(f)) == f);
        CM_EXPECT(TapMap::channelsFor(TapMap::forFx(f)) == 2);
        CM_EXPECT(TapMap::defaultName(TapMap::forFx(f)).endsWith("Return"));
        for (int ch = 0; ch < 2; ++ch) {
            const int row = RoutingState::busRowForFx(f, ch);
            CM_EXPECT(busRowIsFxReturn(row) && busRowFxIndex(row) == f && row < kBusSrcCount);
        }
        const auto id = ParamBank::idForIndex(ParamBank::index(Mod::Input, 0, inputSendParam(f)));
        CM_EXPECT(id == "input1.fxSend" + juce::String(f + 1));
        CM_EXPECT(sendIds.insert(id).second);
        const auto gid = ParamBank::idForIndex(ParamBank::index(Mod::Group, 0, groupSendParam(f)));
        CM_EXPECT(gid == "group1.fxSend" + juce::String(f + 1));
        CM_EXPECT(groupSendIds.insert(gid).second);
    }
    for (int row = 0; row < kBusSrcCount; ++row) {
        const auto n = RoutingState::busRowName(row);
        CM_EXPECT(n.isNotEmpty());
        CM_EXPECT(rowNames.insert(n).second);
    }
    CM_EXPECT(TapMap::fxSlotOf(TapMap::Master) == -1 && TapMap::fxSlotOf(TapMap::forOutputPair(0)) == -1);
    // meters and per-slot arrays are sized for every slot
    CrowdMikeAudioProcessor p;
    CM_EXPECT(p.getMeters().fxPeak.size() == static_cast<size_t>(kNumFx));
    CM_EXPECT(p.getMeters().fxSendPeak.size() == static_cast<size_t>(kNumFx));
}
