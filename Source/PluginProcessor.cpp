#include "PluginProcessor.h"
#include "Core/PresetManager.h"
#include "Core/StateIO.h"
#include "PluginEditor.h"
#include "Routing/WiringTemplates.h"
#include <cmath>

using namespace crowdmike;

CrowdMikeAudioProcessor::CrowdMikeAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, &undoManager, StateIO::kStateType, ParamBank::createLayout()),
      scenes(parameters, routing, show),
      hub(parameters, scenes, show),
      osc(hub)
{
    bank.attach(parameters);
    bank.publishMacros(show.macros);
    routing.resetDirectDefault(2, 2);
    engine.setServices({ &bank, &routing, &recorder, &playback, &soundcheck });
    scenes.onRecallRamp = [this](float ms) { engine.setRecallRampMs(ms); };
    hub.actions.recordStart = [this] { startRecording(); };
    hub.actions.recordStop = [this] { stopRecording(); };
    hub.actions.recordToggle = [this] { if (isRecording()) stopRecording(); else startRecording(); };
    hub.actions.splitFile = [this] { recorder.splitFile(); };
    hub.actions.marker = [this](int type) { addMarker(type); };
    hub.actions.clipTrigger = [this](int slot) { playback.trigger(slot); };
    hub.actions.clipStop = [this](int slot) { playback.stop(slot); };
    hub.actions.stopAllClips = [this] { playback.stopAll(); };
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr) {
        hub.startPolling();
        startTimerHz(20);
    }
}

CrowdMikeAudioProcessor::~CrowdMikeAudioProcessor()
{
    stopTimer();
    osc.stop();
    recorder.shutdown();
}

juce::AudioProcessorParameter* CrowdMikeAudioProcessor::getBypassParameter() const
{
    return parameters.getParameter(ParamBank::paramId(Mod::Global, 0, params::GlobalP::Bypass));
}

double CrowdMikeAudioProcessor::getTailLengthSeconds() const { return 4.0; }

void CrowdMikeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const int ins = juce::jlimit(1, kMaxInputs, getTotalNumInputChannels());
    const int outs = juce::jlimit(1, kMaxOutputs, getTotalNumOutputChannels());
    activeInputChannels.store(ins, std::memory_order_relaxed);
    activeOutputChannels.store(outs, std::memory_order_relaxed);
    if (!routing.hasSavedDirect())
        routing.resetDirectDefault(ins, outs);
    preparedSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    engine.prepare(sampleRate, juce::jmax(1, samplesPerBlock), ins, outs);
    prepareRecordingForLayout(sampleRate, samplesPerBlock);
    setLatencySamples(engine.getLatencySamples());
}

void CrowdMikeAudioProcessor::prepareRecordingForLayout(double sampleRate, int blockSize)
{
    recorder.prepare(sampleRate, juce::jmax(1, blockSize));
    playback.prepare(sampleRate, juce::jmax(1, blockSize));
    soundcheck.prepare(sampleRate, juce::jmax(1, blockSize));
    rebuildRecorderTracks();
}

bool CrowdMikeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return ! in.isDisabled() && ! out.isDisabled()
        && in.size() >= 1 && in.size() <= kMaxInputs
        && out.size() >= 1 && out.size() <= kMaxOutputs;
}

void CrowdMikeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    if (acceptsMidi() && !midi.isEmpty())
        hub.queueMidiFromAudioThread(midi);
    midi.clear();

    BlockInfo info;
    if (auto* head = getPlayHead()) {
        if (auto pos = head->getPosition()) {
            if (auto bpm = pos->getBpm()) { info.bpm = *bpm; info.hasTempo = true; }
            if (auto ppq = pos->getPpqPosition()) info.ppqPosition = *ppq;
            info.playing = pos->getIsPlaying();
        }
    }
    engine.process(buffer, info);
}

void CrowdMikeAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    StateIO::EngineConfigInfo cfg { preparedSampleRate, preparedBlockSize,
                                    activeInputChannels.load(), activeOutputChannels.load() };
    if (auto xml = StateIO::serialize({ parameters, routing, show, scenes }, cfg))
        copyXmlToBinary(*xml, destination);
}

void CrowdMikeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;
    const auto result = StateIO::restore({ parameters, routing, show, scenes }, *xml,
                                         activeInputChannels.load(std::memory_order_relaxed),
                                         activeOutputChannels.load(std::memory_order_relaxed));
    if (result == StateIO::Result::Ok)
        applyShowChanges();
}

void CrowdMikeAudioProcessor::applyShowChanges()
{
    bank.publishMacros(show.macros);
    rebuildRecorderTracks();
    if (wrapperType == wrapperType_Standalone) {
        if (show.osc.enabled) { if (!osc.isRunning()) osc.start(show.osc); }
        else osc.stop();
    }
}

void CrowdMikeAudioProcessor::rebuildRecorderTracks()
{
    recorder.setPreRollSeconds(show.recorder.preRollSeconds);
    for (int t = 0; t < kMaxRecordTracks; ++t) {
        const auto& s = show.recorder.tracks[static_cast<size_t>(t)];
        rec::RecorderEngine::TrackConfig cfg;
        cfg.sourceId = s.sourceId;
        cfg.numChannels = juce::jlimit(1, 2, s.numChannels);
        cfg.name = s.name.isNotEmpty() ? s.name : (s.sourceId >= 0 ? TapMap::defaultName(s.sourceId) : juce::String());
        cfg.armed = s.sourceId >= 0 && s.armed && !s.muted;
        recorder.setTrack(t, cfg);
    }
}

bool CrowdMikeAudioProcessor::startRecording()
{
    rebuildRecorderTracks();
    rec::RecorderEngine::RecordOptions options;
    const auto& r = show.recorder;
    options.folder = r.folder.isNotEmpty() ? juce::File(r.folder)
                                           : juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("CrowdMike");
    options.format = static_cast<rec::RecorderEngine::RecordOptions::Format>(juce::jlimit(0, 2, r.format));
    options.namePattern = r.pattern;
    options.scene = getCurrentSceneName();
    options.show = r.show;
    options.venue = r.venue;
    options.preRollSeconds = r.preRollSeconds;
    options.postRollSeconds = r.postRollSeconds;
    options.maxFileSeconds = r.maxFileSeconds;
    options.maxFileBytes = static_cast<juce::int64>(r.maxFileMegabytes * 1024.0 * 1024.0);
    options.appVersion = kAppVersion;
    options.routeSnapshot = juce::var(routing.toValueTree().toXmlString());
    return recorder.startRecording(options);
}

void CrowdMikeAudioProcessor::stopRecording() { recorder.stopRecording(); }
bool CrowdMikeAudioProcessor::isRecording() const { return recorder.getStatus().recording; }

void CrowdMikeAudioProcessor::addMarker(int markerType, const juce::String& label)
{
    static const char* names[] = { "Big Cheer", "Chant", "Applause", "Performer Moment", "Problem/Feedback", "Marker" };
    const int t = juce::jlimit(0, 5, markerType);
    recorder.addMarker(label.isNotEmpty() ? label : juce::String(names[t]), static_cast<rec::MarkerType>(t));
}

juce::String CrowdMikeAudioProcessor::getCurrentSceneName() const
{
    const int cur = scenes.current();
    return cur >= 0 ? scenes.at(cur).name : juce::String("NoScene");
}

void CrowdMikeAudioProcessor::recallScene(int slot) { scenes.recall(slot); }

int CrowdMikeAudioProcessor::getEstimatedTotalLatencySamples() const noexcept
{
    return getLatencySamples() + preparedBlockSize;
}

void CrowdMikeAudioProcessor::applyNotchSuggestion(int inputIndex, float hz, float q, float depthDb)
{
    if (inputIndex < 0 || inputIndex >= kMaxInputs) return;
    undoManager.beginNewTransaction("Apply notch suggestion");
    hub.setParamActual(ParamBank::index(Mod::Input, inputIndex, params::InputP::NotchHz), hz);
    hub.setParamActual(ParamBank::index(Mod::Input, inputIndex, params::InputP::NotchQ), q);
    hub.setParamActual(ParamBank::index(Mod::Input, inputIndex, params::InputP::NotchDepth), depthDb);
    hub.setParamNormalised(ParamBank::index(Mod::Input, inputIndex, params::InputP::NotchOn), 1.0f);
}

void CrowdMikeAudioProcessor::applyWiringTemplate(int templateIndex)
{
    undoManager.beginNewTransaction("Apply wiring template");
    WiringTemplates::TemplateTarget target { routing, show, activeInputChannels.load(), activeOutputChannels.load(),
        [this](const juce::String& id, float actual) {
            if (auto* p = parameters.getParameter(id)) p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->convertTo0to1(actual)));
        } };
    WiringTemplates::apply(static_cast<WiringTemplates::Id>(templateIndex), target);
    applyShowChanges();
}

void CrowdMikeAudioProcessor::timerCallback()
{
    // Host-visible "button" parameters: act on the rising edge, then release them.
    const auto edge = [this](int global, bool& last) {
        const bool now = bank.rawAt(global) >= 0.5f;
        const bool risen = now && !last;
        last = now;
        if (risen) {   // behave like a button: release it after acting (the host sees the edge, then the release)
            if (auto* p = bank.parameterAt(global)) p->setValueNotifyingHost(0.0f);
            last = false;
        }
        return risen;
    };
    namespace G = params::GlobalP;
    if (edge(ParamBank::index(Mod::Global, 0, G::SceneNext), lastNextPressed)) scenes.next();
    if (edge(ParamBank::index(Mod::Global, 0, G::ScenePrev), lastPrevPressed)) scenes.previous();
    if (edge(ParamBank::index(Mod::Global, 0, G::RecordAll), lastRecordAll)) {
        if (isRecording()) stopRecording(); else startRecording();
    }
}

float CrowdMikeAudioProcessor::getAndResetInputPeak(int inputIndex) noexcept
{
    if (inputIndex < 0 || inputIndex >= kMaxInputs)
        return 0.0f;
    return engine.meters.inPeak[static_cast<size_t>(inputIndex)].getAndReset();
}

float CrowdMikeAudioProcessor::getRequestedRouteGain(int inputIndex, int outputIndex) const noexcept
{
    return routing.getDirect(inputIndex, outputIndex);
}

bool CrowdMikeAudioProcessor::setRequestedRouteGain(int inputIndex, int outputIndex, float linearGain) noexcept
{
    if (!routing.setDirect(inputIndex, outputIndex, linearGain))
        return false;
    routing.setHasSavedDirect(true);
    return true;
}

juce::AudioProcessorEditor* CrowdMikeAudioProcessor::createEditor()
{
    return new CrowdMikeAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CrowdMikeAudioProcessor();
}
