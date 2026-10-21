#include "PluginProcessor.h"
#include "Core/PresetManager.h"
#include "Core/StateIO.h"
#include "Engine/InputRisk.h"
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
    tempo.attachTracker(&engine.beatTracker());
    scenes.onRecallRamp = [this](float ms) { engine.setRecallRampMs(ms); };
    hub.actions.recordStart = [this] { startRecording(); };
    hub.actions.recordStop = [this] { stopRecording(); };
    hub.actions.recordToggle = [this] { if (isRecording()) stopRecording(); else startRecording(); };
    hub.actions.splitFile = [this] { recorder.splitFile(); };
    hub.actions.marker = [this](int type) { addMarker(type); };
    hub.actions.clipTrigger = [this](int slot) { triggerClip(slot); };
    hub.actions.tempoTap = [this] { tempoTap(); };
    hub.actions.tempoResync = [this] { tempoResync(); };
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
    if (autoAnalysisHeld) { beatAnalysis.release(); autoAnalysisHeld = false; }
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
    tempo.prepare(sampleRate, juce::jmax(1, samplesPerBlock));
    prepareRecordingForLayout(sampleRate, samplesPerBlock);
    setLatencySamples(engine.getLatencySamples());
}

void CrowdMikeAudioProcessor::prepareRecordingForLayout(double sampleRate, int blockSize)
{
    recorder.prepare(sampleRate, juce::jmax(1, blockSize));
    playback.prepare(sampleRate, juce::jmax(1, blockSize));
    soundcheck.prepare(sampleRate, juce::jmax(1, blockSize));
    for (auto& k : appliedTrackKeys) k.clear();   // the recorder/engine were re-prepared: re-send everything
    appliedPreRoll = -1;
    rebuildRecorderTracks();
    restorePlaybackFromShow();
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
    if (acceptsMidi() && !midi.isEmpty()) {
        for (const auto meta : midi) {   // MIDI beat clock + transport (system realtime) feeds the TempoHub
            const auto msg = meta.getMessage();
            if (msg.isMidiClock()) tempo.midiClockTick(meta.samplePosition);
            else if (msg.isMidiStart()) tempo.midiStart(meta.samplePosition);
            else if (msg.isMidiContinue()) tempo.midiContinue(meta.samplePosition);
            else if (msg.isMidiStop()) tempo.midiStop(meta.samplePosition);
        }
        hub.queueMidiFromAudioThread(midi);
    }
    midi.clear();

    HostTempoInfo host;
    if (auto* head = getPlayHead()) {
        if (auto pos = head->getPosition()) {
            if (auto bpm = pos->getBpm()) { host.bpm = *bpm; host.hasBpm = true; }
            if (auto ppq = pos->getPpqPosition()) { host.ppq = *ppq; host.hasPpq = true; }
            host.playing = pos->getIsPlaying();
        }
    }
    namespace T = params::TempoP;
    TempoParams tp;
    tp.source = juce::roundToInt(bank.effectiveAt(ParamBank::index(Mod::Tempo, 0, T::Source)));
    tp.manualBpm = bank.effectiveAt(ParamBank::index(Mod::Tempo, 0, T::ManualBpm));
    tp.nudgeBpm = bank.effectiveAt(ParamBank::index(Mod::Tempo, 0, T::Nudge));
    tp.barBeats = params::kBarBeatsTable[juce::jlimit(0, 5, juce::roundToInt(bank.effectiveAt(ParamBank::index(Mod::Tempo, 0, T::BarBeats))))];
    const auto ts = tempo.process(buffer.getNumSamples(), tp, host);

    BlockInfo info;
    info.bpm = ts.bpm;
    info.hasTempo = ts.hasTempo;
    info.ppqPosition = ts.ppq;
    info.playing = ts.playing;
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
    restorePlaybackFromShow();
    if (wrapperType == wrapperType_Standalone) {
        if (show.osc.enabled) { if (!osc.isRunning()) osc.start(show.osc); }
        else osc.stop();
    }
}

void CrowdMikeAudioProcessor::rebuildRecorderTracks()
{
    if (appliedPreRoll != show.recorder.preRollSeconds) {
        appliedPreRoll = show.recorder.preRollSeconds;
        recorder.setPreRollSeconds(show.recorder.preRollSeconds);
    }
    for (int t = 0; t < kMaxRecordTracks; ++t) {
        const auto& s = show.recorder.tracks[static_cast<size_t>(t)];
        const bool used = TapMap::isValid(s.sourceId);
        const int channels = used ? juce::jlimit(1, 2, juce::jmin(s.numChannels, TapMap::channelsFor(s.sourceId))) : 1;
        // The engine decides which tap feeds each track (unused / muted tracks too, -1 = no source).
        engine.setTrackSource(t, used ? s.sourceId : -1, channels);
        rec::RecorderEngine::TrackConfig cfg;
        cfg.sourceId = s.sourceId;
        cfg.numChannels = channels;
        cfg.name = s.name.isNotEmpty() ? s.name : (used ? TapMap::defaultName(s.sourceId) : juce::String());
        cfg.armed = used && s.armed && !s.muted;
        // setTrack() may wait briefly for the writer thread: only call it when the configuration changed.
        const auto key = juce::String(cfg.sourceId) + "|" + juce::String(cfg.numChannels) + "|" + (cfg.armed ? "1" : "0") + "|" + cfg.name;
        if (key == appliedTrackKeys[static_cast<size_t>(t)]) continue;
        appliedTrackKeys[static_cast<size_t>(t)] = key;
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

void CrowdMikeAudioProcessor::recallScene(int slot) { scenes.recall(slot, show.ui.recallScope); }

void CrowdMikeAudioProcessor::tempoTap() { tempo.tap(juce::Time::getMillisecondCounterHiRes() * 0.001); }
void CrowdMikeAudioProcessor::tempoResync() { tempo.resyncDownbeat(); }

bool CrowdMikeAudioProcessor::triggerClip(int slot)
{
    if (slot < 0 || slot >= rec::PlaybackEngine::kMaxClips) return false;
    const int mode = juce::roundToInt(bank.rawAt(ParamBank::index(Mod::Tempo, 0, params::TempoP::Quantise)));
    if (mode <= 0) return playback.trigger(slot);       // Off: exactly the legacy path
    engine.requestClipTrigger(slot);
    return true;
}

// ---------------------------------------------------------------- clip deck / bed / soundcheck persistence
// ShowModel::playback = <Playback> <Clip slot file name gainDb ... hotkey/>* <Bed .../> <Soundcheck session loopStart .../> </Playback>
namespace {
const juce::Identifier kClip("Clip"), kBed("Bed"), kSoundcheck("Soundcheck");

juce::File fileFromPath(const juce::String& path)
{
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}
bool pathExists(const juce::String& path)
{
    return path.isNotEmpty() && fileFromPath(path).existsAsFile();
}
void writeClipSettings(juce::ValueTree& n, const rec::PlaybackEngine::ClipSettings& c)
{
    n.setProperty("name", c.name, nullptr);
    n.setProperty("gainDb", c.gainDb, nullptr);
    n.setProperty("fadeInMs", c.fadeInMs, nullptr);
    n.setProperty("fadeOutMs", c.fadeOutMs, nullptr);
    n.setProperty("start", c.startFrac, nullptr);
    n.setProperty("end", c.endFrac, nullptr);
    n.setProperty("loop", c.mode == rec::PlaybackEngine::ClipSettings::Loop, nullptr);
    n.setProperty("reverse", c.reverse, nullptr);
    n.setProperty("route", c.route, nullptr);
    n.setProperty("tempoSync", c.tempoSync, nullptr);
}
rec::PlaybackEngine::ClipSettings readClipSettings(const juce::ValueTree& n)
{
    rec::PlaybackEngine::ClipSettings c;
    c.name = n["name"].toString();
    c.gainDb = juce::jlimit(-60.0f, 12.0f, static_cast<float>(n.getProperty("gainDb", 0.0)));
    c.fadeInMs = juce::jlimit(0.0f, 10000.0f, static_cast<float>(n.getProperty("fadeInMs", 0.0)));
    c.fadeOutMs = juce::jlimit(0.0f, 10000.0f, static_cast<float>(n.getProperty("fadeOutMs", 0.0)));
    c.startFrac = juce::jlimit(0.0, 1.0, static_cast<double>(n.getProperty("start", 0.0)));
    c.endFrac = juce::jlimit(c.startFrac, 1.0, static_cast<double>(n.getProperty("end", 1.0)));
    c.mode = static_cast<bool>(n["loop"]) ? rec::PlaybackEngine::ClipSettings::Loop : rec::PlaybackEngine::ClipSettings::OneShot;
    c.reverse = static_cast<bool>(n["reverse"]);
    c.route = juce::jlimit(0, rec::PlaybackEngine::kNumDestinations - 1, static_cast<int>(n["route"]));
    c.tempoSync = static_cast<bool>(n["tempoSync"]);
    return c;
}
void writeBed(juce::ValueTree& n, const rec::PlaybackEngine::BedSettings& b)
{
    n.setProperty("slot", b.slot, nullptr);
    n.setProperty("enabled", b.enabled, nullptr);
    n.setProperty("minLoop", b.minLoopSec, nullptr);
    n.setProperty("maxLoop", b.maxLoopSec, nullptr);
    n.setProperty("crossfade", b.crossfadeSec, nullptr);
    n.setProperty("variationDb", b.levelVariationDb, nullptr);
    n.setProperty("lowpassHz", b.lowpassHz, nullptr);
    n.setProperty("gainDb", b.gainDb, nullptr);
    n.setProperty("route", b.route, nullptr);
    n.setProperty("seed", static_cast<int>(b.seed), nullptr);
}
rec::PlaybackEngine::BedSettings readBed(const juce::ValueTree& n)
{
    rec::PlaybackEngine::BedSettings b;
    b.slot = juce::jlimit(0, rec::PlaybackEngine::kMaxClips - 1, static_cast<int>(n["slot"]));
    b.enabled = static_cast<bool>(n["enabled"]);
    b.minLoopSec = static_cast<double>(n.getProperty("minLoop", 20.0));
    b.maxLoopSec = static_cast<double>(n.getProperty("maxLoop", 60.0));
    b.crossfadeSec = static_cast<double>(n.getProperty("crossfade", 4.0));
    b.levelVariationDb = static_cast<float>(static_cast<double>(n.getProperty("variationDb", 2.0)));
    b.lowpassHz = static_cast<float>(static_cast<double>(n.getProperty("lowpassHz", 0.0)));
    b.gainDb = static_cast<float>(static_cast<double>(n.getProperty("gainDb", -12.0)));
    b.route = juce::jlimit(0, rec::PlaybackEngine::kNumDestinations - 1, static_cast<int>(n["route"]));
    b.seed = static_cast<unsigned>(static_cast<int>(n.getProperty("seed", 1)));
    return b;
}
}

juce::ValueTree CrowdMikeAudioProcessor::playbackChild(const juce::Identifier& type, int slot, bool create)
{
    for (auto c : show.playback)
        if (c.hasType(type) && (slot < 0 || static_cast<int>(c["slot"]) == slot)) return c;
    if (!create) return {};
    juce::ValueTree c(type);
    if (slot >= 0) c.setProperty("slot", slot, nullptr);
    show.playback.appendChild(c, nullptr);
    return c;
}

void CrowdMikeAudioProcessor::restorePlaybackFromShow()
{
    for (int slot = 0; slot < rec::PlaybackEngine::kMaxClips; ++slot) {
        const auto node = playbackChild(kClip, slot, false);
        auto& applied = appliedClips[static_cast<size_t>(slot)];
        if (!node.isValid() && !applied.isValid()) continue;
        if (node.isValid() && applied.isValid() && node.isEquivalentTo(applied)) continue;
        const auto path = node.isValid() ? node["file"].toString() : juce::String();
        const auto oldPath = applied.isValid() ? applied["file"].toString() : juce::String();
        if (path != oldPath || !applied.isValid()) {
            if (pathExists(path)) playback.loadClip(slot, fileFromPath(path));
            else playback.unloadClip(slot);      // missing or removed file: the UI shows 'missing', never crash
        }
        playback.setSettings(slot, node.isValid() ? readClipSettings(node) : rec::PlaybackEngine::ClipSettings {});
        applied = node.isValid() ? node.createCopy() : juce::ValueTree();
    }
    const auto bedNode = playbackChild(kBed, -1, false);
    if (bedNode.isValid() && !(appliedBed.isValid() && bedNode.isEquivalentTo(appliedBed))) {
        playback.setBed(readBed(bedNode));
        appliedBed = bedNode.createCopy();
    }
    const auto sc = playbackChild(kSoundcheck, -1, false);
    if (sc.isValid() && !(appliedSoundcheck.isValid() && sc.isEquivalentTo(appliedSoundcheck))) {
        const auto path = sc["session"].toString();
        if ((!appliedSoundcheck.isValid() || path != appliedSoundcheck["session"].toString()) && pathExists(path))
            soundcheck.loadSession(fileFromPath(path));
        if (sc.hasProperty("loopStart"))
            soundcheck.setLoopRange(static_cast<double>(sc["loopStart"]), static_cast<double>(sc["loopEnd"]), static_cast<bool>(sc["loopOn"]));
        appliedSoundcheck = sc.createCopy();
    }
}

bool CrowdMikeAudioProcessor::loadClipFile(int slot, const juce::File& file)
{
    if (slot < 0 || slot >= rec::PlaybackEngine::kMaxClips) return false;
    auto node = playbackChild(kClip, slot, true);
    if (!node.hasProperty("gainDb")) writeClipSettings(node, playback.getSettings(slot));
    node.setProperty("file", file.getFullPathName(), nullptr);
    node.setProperty("name", file.getFileNameWithoutExtension(), nullptr);
    appliedClips[static_cast<size_t>(slot)] = {};      // force a (re)load even when the path is unchanged
    restorePlaybackFromShow();
    return file.existsAsFile();
}

void CrowdMikeAudioProcessor::unloadClipFile(int slot)
{
    if (auto node = playbackChild(kClip, slot, false); node.isValid()) show.playback.removeChild(node, nullptr);
    restorePlaybackFromShow();
}

void CrowdMikeAudioProcessor::setClipSettings(int slot, const rec::PlaybackEngine::ClipSettings& settings)
{
    if (slot < 0 || slot >= rec::PlaybackEngine::kMaxClips) return;
    auto node = playbackChild(kClip, slot, true);
    writeClipSettings(node, settings);
    restorePlaybackFromShow();
}

juce::String CrowdMikeAudioProcessor::getClipPath(int slot) const
{
    for (const auto& c : show.playback)
        if (c.hasType(kClip) && static_cast<int>(c["slot"]) == slot) return c["file"].toString();
    return {};
}

bool CrowdMikeAudioProcessor::isClipMissing(int slot) const
{
    const auto path = getClipPath(slot);
    return path.isNotEmpty() && !pathExists(path);
}

void CrowdMikeAudioProcessor::setClipHotkey(int slot, const juce::String& key)
{
    if (slot < 0 || slot >= rec::PlaybackEngine::kMaxClips) return;
    if (key.isNotEmpty())   // one key triggers one clip
        for (auto c : show.playback)
            if (c.hasType(kClip) && c["hotkey"].toString() == key) c.removeProperty("hotkey", nullptr);
    auto node = playbackChild(kClip, slot, true);
    if (key.isEmpty()) node.removeProperty("hotkey", nullptr);
    else node.setProperty("hotkey", key, nullptr);
}

juce::String CrowdMikeAudioProcessor::getClipHotkey(int slot) const
{
    for (const auto& c : show.playback)
        if (c.hasType(kClip) && static_cast<int>(c["slot"]) == slot) return c["hotkey"].toString();
    return {};
}

int CrowdMikeAudioProcessor::findClipForHotkey(const juce::String& key) const
{
    if (key.isEmpty()) return -1;
    for (const auto& c : show.playback)
        if (c.hasType(kClip) && c["hotkey"].toString() == key) return static_cast<int>(c["slot"]);
    return -1;
}

void CrowdMikeAudioProcessor::setBedSettings(const rec::PlaybackEngine::BedSettings& bedSettings)
{
    auto node = playbackChild(kBed, -1, true);
    writeBed(node, bedSettings);
    restorePlaybackFromShow();
}

bool CrowdMikeAudioProcessor::loadSoundcheckSession(const juce::File& sessionJson)
{
    auto node = playbackChild(kSoundcheck, -1, true);
    node.setProperty("session", sessionJson.getFullPathName(), nullptr);
    appliedSoundcheck = {};
    restorePlaybackFromShow();
    return sessionJson.existsAsFile();
}

juce::String CrowdMikeAudioProcessor::getSoundcheckSessionPath() const
{
    for (const auto& c : show.playback)
        if (c.hasType(kSoundcheck)) return c["session"].toString();
    return {};
}

bool CrowdMikeAudioProcessor::isSoundcheckSessionMissing() const
{
    const auto path = getSoundcheckSessionPath();
    return path.isNotEmpty() && !pathExists(path);
}

void CrowdMikeAudioProcessor::setSoundcheckLoop(double startSeconds, double endSeconds, bool enabled)
{
    auto node = playbackChild(kSoundcheck, -1, true);
    node.setProperty("loopStart", startSeconds, nullptr);
    node.setProperty("loopEnd", endSeconds, nullptr);
    node.setProperty("loopOn", enabled, nullptr);
    restorePlaybackFromShow();
}

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
    if (edge(ParamBank::index(Mod::Global, 0, G::SceneNext), lastNextPressed)) scenes.next(show.ui.recallScope);
    if (edge(ParamBank::index(Mod::Global, 0, G::ScenePrev), lastPrevPressed)) scenes.previous(show.ui.recallScope);
    if (edge(ParamBank::index(Mod::Tempo, 0, params::TempoP::Tap), lastTapPressed)) tempoTap();
    if (edge(ParamBank::index(Mod::Tempo, 0, params::TempoP::Resync), lastResyncPressed)) tempoResync();
    {   // the Auto tempo analyser only runs while tempo.source == Auto
        const bool wantAuto = juce::roundToInt(bank.rawAt(ParamBank::index(Mod::Tempo, 0, params::TempoP::Source))) == 4;
        if (wantAuto != autoAnalysisHeld) {
            if (wantAuto) beatAnalysis.acquire(); else beatAnalysis.release();
            autoAnalysisHeld = wantAuto;
        }
    }
    if (edge(ParamBank::index(Mod::Global, 0, G::RecordAll), lastRecordAll)) {
        if (isRecording()) stopRecording(); else startRecording();
    }
    if (++riskTicks >= 4) { riskTicks = 0; updateFeedbackRisk(); }   // 20 Hz timer -> ~200 ms
}

void CrowdMikeAudioProcessor::updateFeedbackRisk()
{
    float severity = 0.0f;
    if (analysis.isActive()) {   // candidates are only fresh while the analysis worker runs
        dsp::FeedbackCandidate c[4];
        const int n = engine.feedback().getCandidates(c, 4);
        for (int k = 0; k < n; ++k) severity = juce::jmax(severity, c[k].severity);
    }
    const InputRiskContext ctx { bank, routing, show, getActiveOutputChannelCount(), engine.getFeedbackTap(), severity };
    const int ins = getActiveInputChannelCount();
    for (int i = 0; i < kMaxInputs; ++i)
        engine.meters.inRisk[static_cast<size_t>(i)].store(i < ins ? evaluateInputRisk(ctx, i).result.score : 0.0f, std::memory_order_relaxed);
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
