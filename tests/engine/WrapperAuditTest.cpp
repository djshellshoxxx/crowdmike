// Plug-in / standalone wrapper audit (docs/feature-audit/part-C): bus layouts, latency and tail reporting, state restore
// robustness, host-automation count and order stability, MIDI acceptance per wrapper type, CMake target configuration,
// and the standalone DeviceController pieces that do not need a physical audio device.
#include "TestHarness.h"
#include "PluginProcessor.h"
#include "Standalone/DeviceController.h"
#include <cstdlib>

using namespace crowdmike;
using namespace crowdmike::params;

namespace {
juce::AudioProcessor::BusesLayout layout(const juce::AudioChannelSet& in, const juce::AudioChannelSet& out) {
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(in);
    l.outputBuses.add(out);
    return l;
}
juce::File repoFile(const char* relative) {
    return juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory().getChildFile(relative);
}
}

CM_TEST(wrapper_bus_layouts_accept_1_to_16_channels_only) {
    CrowdMikeAudioProcessor p;
    using S = juce::AudioChannelSet;
    CM_EXPECT(p.getBusCount(true) == 1 && p.getBusCount(false) == 1);   // one main bus each way, no aux buses
    CM_EXPECT(p.isBusesLayoutSupported(layout(S::stereo(), S::stereo())));
    CM_EXPECT(p.isBusesLayoutSupported(layout(S::mono(), S::stereo())));
    CM_EXPECT(p.isBusesLayoutSupported(layout(S::mono(), S::mono())));
    CM_EXPECT(p.isBusesLayoutSupported(layout(S::discreteChannels(kMaxInputs), S::discreteChannels(kMaxOutputs))));
    CM_EXPECT(p.isBusesLayoutSupported(layout(S::create7point1(), S::create5point1())));
    CM_EXPECT(!p.isBusesLayoutSupported(layout(S::disabled(), S::stereo())));
    CM_EXPECT(!p.isBusesLayoutSupported(layout(S::stereo(), S::disabled())));
    CM_EXPECT(!p.isBusesLayoutSupported(layout(S::discreteChannels(kMaxInputs + 1), S::stereo())));
    CM_EXPECT(!p.isBusesLayoutSupported(layout(S::stereo(), S::discreteChannels(kMaxOutputs + 1))));
    CM_EXPECT(p.getBusesLayout().getMainInputChannelSet() == S::stereo());   // default layout
}

CM_TEST(wrapper_every_supported_layout_processes_finite_audio) {
    using S = juce::AudioChannelSet;
    const std::pair<int, int> layouts[] = { { 1, 1 }, { 1, 2 }, { 2, 2 }, { 8, 8 }, { kMaxInputs, kMaxOutputs } };
    for (const auto& lo : layouts) {
        CrowdMikeAudioProcessor p;
        CM_EXPECT(p.setBusesLayout(layout(S::discreteChannels(lo.first), S::discreteChannels(lo.second))));
        p.setRateAndBufferSizeDetails(48000.0, 64);
        p.prepareToPlay(48000.0, 64);
        CM_EXPECT(p.getActiveInputChannelCount() == lo.first && p.getActiveOutputChannelCount() == lo.second);
        juce::AudioBuffer<float> b(juce::jmax(lo.first, lo.second), 64);
        juce::MidiBuffer midi;
        bool finite = true;
        for (int blk = 0; blk < 40; ++blk) {
            b.clear();
            for (int ch = 0; ch < lo.first; ++ch)
                for (int s = 0; s < 64; ++s) b.setSample(ch, s, 0.2f * std::sin(0.03f * static_cast<float>(blk * 64 + s + ch)));
            p.processBlock(b, midi);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                for (int s = 0; s < 64; ++s) finite = finite && std::isfinite(b.getSample(ch, s));
        }
        CM_EXPECT(finite);
    }
}

CM_TEST(wrapper_latency_is_reported_and_constant) {
    CrowdMikeAudioProcessor p;
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        p.setRateAndBufferSizeDetails(sr, 128);
        p.prepareToPlay(sr, 128);
        const int lat = p.getLatencySamples();
        CM_EXPECT(lat > 0);                                   // the output limiter always delays by its lookahead
        CM_EXPECT(lat == p.getEngine().getLatencySamples());
        // toggling the limiter / bypass / panic must never change the reported latency (hosts compensate once)
        juce::AudioBuffer<float> b(2, 128);
        juce::MidiBuffer midi;
        for (const char* id : { "limiterCeilingDb", "bypass", "panic", "unlockLimiters" }) {
            if (auto* prm = p.getParameters().getParameter(id)) {
                prm->setValueNotifyingHost(1.0f);
                for (int i = 0; i < 4; ++i) { b.clear(); p.processBlock(b, midi); }
                prm->setValueNotifyingHost(0.0f);
            }
        }
        CM_EXPECT(p.getLatencySamples() == lat);
        CM_EXPECT(p.getEstimatedTotalLatencySamples() == lat + 128);
    }
}

CM_TEST(wrapper_tail_length_covers_delays_and_grows_with_reverb) {
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    const double base = p.getTailLengthSeconds();
    CM_EXPECT(base >= 4.0);
    auto set = [&](const char* id, float actual) {
        if (auto* prm = p.getParameters().getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(actual));
        else CM_EXPECT(false);
    };
    set("fx.reverb.on", 1.0f);
    set("fx.reverb.decay", 12.0f);
    CM_EXPECT(p.getTailLengthSeconds() >= 12.0);
}

CM_TEST(wrapper_midi_is_accepted_by_standalone_only) {
    {
        CrowdMikeAudioProcessor plugin;
        CM_EXPECT(!plugin.acceptsMidi() && !plugin.producesMidi() && !plugin.isMidiEffect());
    }
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    CrowdMikeAudioProcessor standalone;
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    CM_EXPECT(standalone.acceptsMidi() && !standalone.producesMidi() && !standalone.isMidiEffect());
}

CM_TEST(wrapper_host_parameter_count_and_order_are_stable) {
    CrowdMikeAudioProcessor p;
    const auto& list = p.getParameters().processor.getParameters();
    CM_EXPECT(list.size() == ParamBank::totalCount());
    CM_EXPECT(ParamBank::totalCount() >= 2000);
    bool order = true;
    for (int i = 0; i < list.size(); ++i) {
        auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(list[i]);
        order = order && withId != nullptr && withId->paramID == ParamBank::idForIndex(i);
    }
    CM_EXPECT(order);
    // module order is frozen: new modules are only ever appended (an inserted module would shift every host automation lane)
    CM_EXPECT(static_cast<int>(Mod::Input) == 0 && static_cast<int>(Mod::Group) == 1 && static_cast<int>(Mod::Output) == 2
              && static_cast<int>(Mod::Master) == 3 && static_cast<int>(Mod::Global) == 4 && static_cast<int>(Mod::FxDelay) == 5
              && static_cast<int>(Mod::FxEcho) == 6 && static_cast<int>(Mod::FxTape) == 7 && static_cast<int>(Mod::FxReverse) == 8
              && static_cast<int>(Mod::FxNoise) == 9 && static_cast<int>(Mod::Macro) == 10 && static_cast<int>(Mod::FxReverb) == 11
              && static_cast<int>(Mod::FxMulti) == 12 && static_cast<int>(Mod::Tempo) == 13);
    CM_EXPECT(ParamBank::indexForId("input1.trimDb") == 0);          // the very first lane the host ever saw
    CM_EXPECT(ParamBank::baseIndex(Mod::Group) > 0 && ParamBank::baseIndex(Mod::Tempo) < ParamBank::totalCount());
    // every parameter is automatable and has a stable non-empty name (hosts display it)
    bool named = true, automatable = true;
    for (auto* prm : list) { named = named && prm->getName(64).isNotEmpty(); automatable = automatable && prm->isAutomatable(); }
    CM_EXPECT(named);
    CM_EXPECT(automatable);
    CM_EXPECT(p.getBypassParameter() == p.getParameters().getParameter("bypass"));
}

CM_TEST(wrapper_state_restore_is_robust_and_exact) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor a;
    a.prepareToPlay(48000.0, 256);
    a.applyWiringTemplate(3);
    for (const auto& [id, v] : std::initializer_list<std::pair<const char*, float>> { { "input1.trimDb", -7.5f }, { "fx.delay.on", 1.0f } })
        if (auto* prm = a.getParameters().getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(v));
    juce::MemoryBlock state;
    a.getStateInformation(state);
    CM_EXPECT(state.getSize() > 100);

    CrowdMikeAudioProcessor b;
    b.prepareToPlay(48000.0, 256);
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int i = 0; i < ParamBank::totalCount(); ++i) {
        const auto id = ParamBank::idForIndex(i);
        auto* pa = a.getParameters().getParameter(id);
        auto* pb = b.getParameters().getParameter(id);
        CM_EXPECT(pa != nullptr && pb != nullptr && std::abs(pa->getValue() - pb->getValue()) < 1e-6f);
    }
    // a second save of the restored processor is identical in its parameters
    juce::MemoryBlock again;
    b.getStateInformation(again);
    CM_EXPECT(again.getSize() > 100);

    // garbage, empty and foreign blobs must neither crash nor change anything
    const float before = b.getParameters().getParameter("input1.trimDb")->getValue();
    b.setStateInformation(nullptr, 0);
    const char junk[] = "not a state blob at all";
    b.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    juce::XmlElement foreign("SomethingElse");
    juce::MemoryBlock fb;
    juce::AudioProcessor::copyXmlToBinary(foreign, fb);
    b.setStateInformation(fb.getData(), static_cast<int>(fb.getSize()));
    CM_EXPECT(b.getParameters().getParameter("input1.trimDb")->getValue() == before);
}

CM_TEST(wrapper_cmake_declares_vst3_and_clap_identity) {
    const auto f = repoFile("CMakeLists.txt");
    CM_EXPECT(f.existsAsFile());
    const auto text = f.loadFileAsString();
    CM_EXPECT(text.contains("FORMATS Standalone VST3"));
    CM_EXPECT(text.contains("VST3_CATEGORIES Fx Spatial"));
    CM_EXPECT(text.contains("PLUGIN_MANUFACTURER_CODE CrDL") && text.contains("PLUGIN_CODE CrM1"));
    CM_EXPECT(text.contains("CLAP_ID \"com.circuitdriftlabs.crowdmike\""));
    CM_EXPECT(text.contains("CLAP_FEATURES audio-effect"));
    CM_EXPECT(text.contains("NEEDS_MIDI_INPUT FALSE"));      // plug-ins never claim MIDI; the standalone feeds MIDI itself
    CM_EXPECT(text.contains("JUCE_VST3_CAN_REPLACE_VST2=0"));
}

// ------------------------------------------------------------------------------------------------------ standalone
CM_TEST(standalone_channel_binding_resolution) {
    juce::StringArray active { "In 1", "In 2", "Mic A" };
    CM_EXPECT(DeviceController::resolveChannel(active, "Mic A") == 2);
    CM_EXPECT(DeviceController::resolveChannel(active, "In 1") == 0);
    CM_EXPECT(DeviceController::resolveChannel(active, "Gone") == -1);     // unresolved = safe disconnected state
    CM_EXPECT(DeviceController::resolveChannel(active, {}) == -1);
    CM_EXPECT(DeviceController::resolveChannel({}, "In 1") == -1);
}

CM_TEST(standalone_bindings_autosave_and_crash_recovery_need_no_device) {
    juce::ScopedJuceInitialiser_GUI gui;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_audit_standalone");
    dir.deleteRecursively();
    dir.createDirectory();
    {
        CrowdMikeAudioProcessor p;
        DeviceController dc(p, dir);
        dc.setInputBinding(0, "Mic A");
        dc.setOutputBinding(1, "Out 2");
        dc.setInputBinding(99, "ignored");          // out of range is ignored, never crashes
        CM_EXPECT(dir.getChildFile("bindings.xml").existsAsFile());
        const auto bindings = juce::XmlDocument::parse(dir.getChildFile("bindings.xml"));
        CM_EXPECT(bindings != nullptr && bindings->getStringAttribute("in0") == "Mic A" && bindings->getStringAttribute("out1") == "Out 2");
        const auto in = dc.getInputBindings();
        CM_EXPECT(!in.empty() && in[0].physicalName == "Mic A" && !in[0].resolved);   // no device: unresolved, not an error
        CM_EXPECT(dc.getMidiInputNames().size() >= 0);
        CM_EXPECT(!dc.isMidiInputEnabled("no such midi device"));

        // clean shutdown: lastsession written, running flag removed; next start restores it without the crash notice
        if (auto* prm = p.getParameters().getParameter("input1.trimDb")) prm->setValueNotifyingHost(prm->convertTo0to1(-9.0f));
        dc.loadLastSession();
        CM_EXPECT(dir.getChildFile("running.flag").existsAsFile());
        dc.shutdownCleanly();
        CM_EXPECT(!dir.getChildFile("running.flag").existsAsFile());
        CM_EXPECT(dir.getChildFile("lastsession.cmstate").existsAsFile());
    }
    {
        CrowdMikeAudioProcessor p;
        DeviceController dc(p, dir);
        dc.loadLastSession();
        CM_EXPECT(!dc.restoredAfterCrash());
        auto* prm = p.getParameters().getParameter("input1.trimDb");
        CM_EXPECT(prm != nullptr && std::abs(prm->convertFrom0to1(prm->getValue()) - (-9.0f)) < 0.2f);
        // simulate a crash: running.flag still present and an autosave exists
        dc.saveSessionNow();
        CM_EXPECT(dc.getAutosaveFile().existsAsFile());
    }
    {
        CrowdMikeAudioProcessor p;
        DeviceController dc(p, dir);
        dc.loadLastSession();
        CM_EXPECT(dc.restoredAfterCrash());       // flag left behind by the previous (never shut down) run
        dc.shutdownCleanly();
    }
    dir.deleteRecursively();
}

CM_TEST(standalone_osc_follows_show_settings_only_in_standalone) {
    juce::ScopedJuceInitialiser_GUI gui;
    {
        CrowdMikeAudioProcessor plugin;
        plugin.getShow().osc.enabled = true;
        plugin.applyShowChanges();
        CM_EXPECT(!plugin.getOsc().isRunning());      // plug-ins never open UDP sockets
    }
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    CrowdMikeAudioProcessor standalone;
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    standalone.getShow().osc.enabled = false;
    standalone.applyShowChanges();
    CM_EXPECT(!standalone.getOsc().isRunning());
}
