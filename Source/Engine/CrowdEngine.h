#pragma once
// CrowdEngine: the format-neutral per-block pipeline of ENGINE_SPEC section 2.
//   host inputs -> input chains -> input->group mix -> groups -> FX sends/returns -> master -> output matrix
//   -> output stage (trim/delay/limiter) -> record taps -> panic / rehearsal / bypass -> host outputs
// Real-time rules: process() never allocates, locks, logs or touches files. Everything the GUI reads lives in `meters`
// or in the lock-free analysis objects exposed below.
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "Core/Limits.h"
#include "Core/ParamBank.h"
#include "DSP/BeatTracker.h"
#include "DSP/ChannelDelay.h"
#include "DSP/CorrelationMeter.h"
#include "DSP/DspCommon.h"
#include "DSP/Ducker.h"
#include "DSP/Envelope.h"
#include "DSP/FeedbackDetector.h"
#include "DSP/FxContext.h"
#include "DSP/LevelHistory.h"
#include "DSP/LoudnessMeter.h"
#include "DSP/RoutingMatrix.h"
#include "DSP/TriggerQuantiser.h"
#include "Engine/BusMatrix.h"
#include "Engine/EngineBuffer.h"
#include "Engine/EngineMeters.h"
#include "Engine/FxRack.h"
#include "Engine/LineCheck.h"
#include "Engine/GroupChannel.h"
#include "Engine/InputChannel.h"
#include "Engine/MasterBus.h"
#include "Engine/OutputStage.h"
#include "Engine/TapMap.h"
#include "Recording/PlaybackEngine.h"
#include "Recording/RecorderEngine.h"
#include "Recording/SoundcheckPlayer.h"
#include "Routing/RoutingState.h"

namespace crowdmike {
struct BlockInfo {
    double bpm = 120.0;
    bool hasTempo = false;
    double ppqPosition = 0.0;
    bool playing = false;
};

class CrowdEngine {
public:
    struct Services {
        ParamBank* bank = nullptr;
        RoutingState* routing = nullptr;
        rec::RecorderEngine* recorder = nullptr;
        rec::PlaybackEngine* playback = nullptr;
        rec::SoundcheckPlayer* soundcheck = nullptr;
    };

    EngineMeters meters;

    void setServices(const Services& s) noexcept { services = s; }
    // Message thread, audio stopped. Allocates everything.
    void prepare(double sampleRate, int maxBlock, int numInputs, int numOutputs);
    int getLatencySamples() const noexcept { return latencySamples; }
    // Audio thread. `buffer` carries the host inputs in channels [0,numInputs) and receives outputs in [0,numOutputs).
    void process(juce::AudioBuffer<float>& buffer, const BlockInfo& info) noexcept;

    // Any thread
    void setRecallRampMs(float ms) noexcept { recallRampMs.store(ms); recallRequested.store(true); }
    void setFeedbackTap(int tap) noexcept { feedbackTap.store(juce::jlimit(0, 24, tap)); }
    int getFeedbackTap() const noexcept { return feedbackTap.load(); }
    // Maps recorder track -> tap id (TapMap). Message thread; read by the audio thread.
    void setTrackSource(int track, int tapId, int channels) noexcept;

    // Beat Sync (docs/BEAT_SYNC_SPEC.md). Message thread: ask for a clip to start on the next grid line of the global
    // quantise setting (immediately when quantise is Off / no usable tempo). Lock-free, drops when 16 are queued.
    void requestClipTrigger(int slot) noexcept;
    // Onset/tempo analyser for the Auto tempo source; fed from the audio thread while tempo.source == Auto.
    dsp::BeatTracker& beatTracker() noexcept { return beat; }

    // Analysis objects for the GUI (thread-safe by their own contracts)
    dsp::FeedbackDetector& feedback() noexcept { return feedbackDetector; }
    dsp::LevelHistory& inputHistory(int i) noexcept { return inHistory[static_cast<size_t>(i)]; }
    dsp::LevelHistory& groupHistory(int g) noexcept { return groupHistoryBuf[static_cast<size_t>(g)]; }
    dsp::LevelHistory& masterHistory() noexcept { return masterHistoryBuf; }
    dsp::CorrelationMeter& masterCorrelation() noexcept { return correlation; }
    dsp::ReverseChunk::Viz getReverseViz() const noexcept { return fx.getReverseViz(); }
    InputChannel& inputChannelForMeters(int i) noexcept { return inputs[static_cast<size_t>(i)]; }
    int getPreparedInputs() const noexcept { return numIn; }
    int getPreparedOutputs() const noexcept { return numOut; }
    double getSampleRate() const noexcept { return sampleRate; }
    // Line Check probe (docs/LINE_CHECK_SPEC.md): arm from the message thread, read frames with linecheck::Collector.
    linecheck::ProbeBank& lineProbe() noexcept { return probeBank; }
    const dsp::Ducker& performerDucker() const noexcept { return ducker; }

private:
    void processChunk(juce::AudioBuffer<float>& host, int offset, int n, const BlockInfo& info) noexcept;
    void syncRouting(float rampMs) noexcept;
    void pushTaps(int n) noexcept;
    void tapPointers(int tapId, const float* out[2], int& channels) const noexcept;
    // Performer ducker (docs/DUCKER_SPEC.md): fills duckGL/duckGR/duckFL/duckFR with the buses the master sum and the bus matrix use.
    void applyDucker(const float* globalParams, int ins, int n, bool bypass) noexcept;

    Services services;
    double sampleRate = 48000.0;
    int maxBlock = 512, numIn = 2, numOut = 2, latencySamples = 0;
    bool prepared = false;

    // DSP objects
    std::array<InputChannel, kMaxInputs> inputs;
    std::array<GroupChannel, kMaxGroups> groups;
    std::array<OutputStage, kMaxOutputs> outputs;
    FxRack fx;
    MasterBus master;
    RoutingMatrix direct;
    BusMatrix busMatrix;
    dsp::ChannelDelay bypassDelay;
    dsp::DensityFollower density;
    dsp::FeedbackDetector feedbackDetector;
    dsp::CorrelationMeter correlation;
    // Beat Sync
    dsp::BeatTracker beat;
    std::vector<float> beatMono;
    bool beatFeeding = false;
    dsp::TriggerQuantiser echoQuant, reverseQuant, clipQuant;
    bool prevEchoTrig = false, prevReverseTrig = false, echoSuppress = false, reverseSuppress = false;
    juce::AbstractFifo clipFifo { 16 };
    std::array<int, 16> clipRing {};
    std::array<int, 16> clipPending {};
    int clipPendingCount = 0;
    std::array<dsp::LevelHistory, kMaxInputs> inHistory;
    std::array<dsp::LevelHistory, kMaxGroups> groupHistoryBuf;
    dsp::LevelHistory masterHistoryBuf;
    dsp::Ducker ducker;
    dsp::ChannelDelay duckDelay;
    linecheck::ProbeBank probeBank;
    dsp::LoudnessMeter loudness;   // Show Report: master short-term loudness / RMS

    // Show Report event edge state (audio thread only). `init` latches the first chunk without counting.
    struct EventEdges { bool init = false, panic = false, echo = false, freeze = false, clip = false, limiter = false; } edges;

    // Smoothed mixing cells
    std::array<dsp::Smoother, kMaxGroups * kMaxInputs> mixL {}, mixR {};
    std::array<dsp::Smoother, kMaxInputs * kNumFx> inSend {};
    std::array<dsp::Smoother, kMaxGroups * kNumFx> grpSend {};
    dsp::Smoother outGain, bypassMix;

    // Buffers
    EngineBuffer raw, proc, preDyn, groupL, groupR, sendL, sendR, retL, retR, pbL, pbR, mastBuf, preMix, outBuf, dryBuf;
    EngineBuffer duckBuf, duckKey, duckGain;      // ducked copies of the group / FX-return buses (recorder taps keep the originals)
    std::array<const float*, kMaxGroups> duckGL {}, duckGR {};
    std::array<const float*, kNumFx> duckFL {}, duckFR {};
    std::array<const float*, kBusSrcCount> busPointers {};
    std::array<bool, kMaxInputs> linkLead {}, linkFollower {};
    std::array<float, kMaxInputs> contribL {}, contribR {};
    std::array<bool, kMaxGroups> groupActive {};

    // Routing sync
    std::uint32_t lastRoutingRevision = 0xFFFFFFFFu;
    float lastDirectRamp = 0.0f;

    // Control
    std::atomic<float> recallRampMs { 60.0f };
    std::atomic<bool> recallRequested { false };
    int recallSamplesLeft = 0;
    float activeRecallMs = 0.0f;
    std::atomic<int> feedbackTap { 0 };
    std::array<std::atomic<int>, kMaxRecordTracks> trackSource {};
    std::array<std::atomic<int>, kMaxRecordTracks> trackChannels {};
    double cpuSmoothed = 0.0;
    int riskCounter = 0;
};
}
