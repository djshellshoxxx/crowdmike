#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include "Control/ControlHub.h"
#include "Control/OscServer.h"
#include "Core/ParamBank.h"
#include "Core/SceneBank.h"
#include "Core/ShowModel.h"
#include "DSP/InputStrip.h"
#include "DSP/RoutingMatrix.h"
#include "DSP/SafetyLimiter.h"
#include "Engine/CrowdEngine.h"
#include "Recording/PlaybackEngine.h"
#include "Recording/RecorderEngine.h"
#include "Recording/SoundcheckPlayer.h"
#include "Routing/RoutingState.h"
#include "Standalone/HostServices.h"

class CrowdMikeAudioProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    CrowdMikeAudioProcessor();
    ~CrowdMikeAudioProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CrowdMike"; }
    bool acceptsMidi() const override { return wrapperType == wrapperType_Standalone; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // ---- first-release API (frozen)
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    float getAndResetInputPeak(int inputIndex) noexcept;
    float getRequestedRouteGain(int inputIndex, int outputIndex) const noexcept;
    bool setRequestedRouteGain(int inputIndex, int outputIndex, float linearGain) noexcept;
    int getActiveInputChannelCount() const noexcept { return activeInputChannels.load(std::memory_order_relaxed); }
    int getActiveOutputChannelCount() const noexcept { return activeOutputChannels.load(std::memory_order_relaxed); }

    // ---- model / services (message thread unless noted)
    crowdmike::ParamBank& getBank() noexcept { return bank; }
    crowdmike::RoutingState& getRouting() noexcept { return routing; }
    crowdmike::ShowModel& getShow() noexcept { return show; }
    crowdmike::SceneBank& getScenes() noexcept { return scenes; }
    crowdmike::ControlHub& getHub() noexcept { return hub; }
    crowdmike::OscServer& getOsc() noexcept { return osc; }
    crowdmike::CrowdEngine& getEngine() noexcept { return engine; }
    crowdmike::EngineMeters& getMeters() noexcept { return engine.meters; }
    crowdmike::rec::RecorderEngine& getRecorder() noexcept { return recorder; }
    crowdmike::rec::PlaybackEngine& getPlayback() noexcept { return playback; }
    crowdmike::rec::SoundcheckPlayer& getSoundcheck() noexcept { return soundcheck; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    void setHostServices(crowdmike::HostServices* services) noexcept { hostServices = services; }
    crowdmike::HostServices* getHostServices() const noexcept { return hostServices; }

    // Call after editing ShowModel (macros, recorder settings, OSC, bindings, ui) so the engine/services pick it up.
    void applyShowChanges();
    // Wiring templates (ROUTING_SPEC section 8): configure routing + a few parameters in one undoable step.
    void applyWiringTemplate(int templateIndex);
    // Recorder control wrappers (build RecordOptions from ShowModel::recorder).
    bool startRecording();
    void stopRecording();
    bool isRecording() const;
    void addMarker(int markerType, const juce::String& label = {});
    // Scene recall from any control source; stretches engine smoothers for the ramp.
    void recallScene(int slot);
    juce::String getCurrentSceneName() const;
    // Total latency estimate for the status bar: algorithmic + host block.
    int getEstimatedTotalLatencySamples() const noexcept;
    // Sets the active tap for the feedback detector (0 = master, 1..16 inputs, 17..24 groups).
    void setFeedbackTap(int tap) noexcept { engine.setFeedbackTap(tap); }
    // Applies a feedback-detector suggestion as a dedicated notch on an input (operator-initiated only).
    void applyNotchSuggestion(int inputIndex, float hz, float q, float depthDb);

    static constexpr const char* kAppVersion = "0.2.0";

private:
    void timerCallback() override;
    void rebuildRecorderTracks();
    void prepareRecordingForLayout(double sampleRate, int blockSize);

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState parameters;
    crowdmike::ParamBank bank;
    crowdmike::RoutingState routing;
    crowdmike::ShowModel show;
    crowdmike::SceneBank scenes;
    crowdmike::ControlHub hub;
    crowdmike::OscServer osc;
    crowdmike::CrowdEngine engine;
    crowdmike::rec::RecorderEngine recorder;
    crowdmike::rec::PlaybackEngine playback;
    crowdmike::rec::SoundcheckPlayer soundcheck;
    crowdmike::HostServices* hostServices = nullptr;

    std::atomic<int> activeInputChannels { 2 }, activeOutputChannels { 2 };
    std::array<float, crowdmike::kMaxInputs> inputPeakCache {};
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    int lastScenePulse = 0;
    bool lastNextPressed = false, lastPrevPressed = false, lastRecordAll = false;
};
