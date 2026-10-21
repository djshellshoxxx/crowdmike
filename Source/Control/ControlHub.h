#pragma once
// Message-thread executor for every external control source: GUI shortcuts, MIDI (CC / note / pitch bend with learn),
// OSC and host-visible command parameters. Audio-thread MIDI is only *queued* (CONTROL_SPEC section 5).
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <functional>
#include <vector>
#include "Control/OscAddress.h"
#include "Core/SceneBank.h"
#include "Core/ShowModel.h"

namespace crowdmike {
struct MidiEvent {
    MidiMsgType type = MidiMsgType::CC;
    int channel = 1;      // 1..16
    int number = 0;
    int value = 0;        // 0..127 (CC / velocity) or 0..16383 (pitch bend)
    bool isOn = true;     // note on (vel>0) / off
};

struct LearnRequest {
    BindTarget target = BindTarget::Param;
    int targetIndex = 0;
    BindMode mode = BindMode::Absolute;
};

// Actions the hub cannot perform itself (owned by recording / playback layers). All optional.
struct HubActions {
    std::function<void()> recordStart, recordStop, recordToggle, splitFile;
    std::function<void(int markerType)> marker;
    std::function<void(int)> clipTrigger, clipStop;
    std::function<void()> stopAllClips;
    std::function<void()> tempoTap, tempoResync;    // Beat Sync (TempoHub owned by the processor)
};

class ControlHub final : private juce::Timer {
public:
    ControlHub(juce::AudioProcessorValueTreeState&, SceneBank&, ShowModel&);
    ~ControlHub() override { stopTimer(); }

    HubActions actions;

    // ---- audio thread (lock-free): parse a MIDI buffer and queue events
    void queueMidiFromAudioThread(const juce::MidiBuffer&) noexcept;
    // ---- message thread (or tests): drain the queue and execute
    void pump();
    void startPolling();      // 5 ms timer; no-op without a MessageManager

    void handleMidi(const MidiEvent&);
    void handleOsc(const juce::String& address, const std::vector<float>& args);
    void execute(const OscAction&);

    // Parameter helpers (message thread). Gestures are wrapped so host automation records the move.
    bool setParamActual(int globalIndex, float actual);
    bool setParamNormalised(int globalIndex, float norm);
    bool pulseParam(int globalIndex, int milliseconds = 60);
    void setPanic(bool engaged);

    // Scene recall scope used by MIDI / OSC / host-button recalls (ShowModel::ui.recallScope, SceneScope bits).
    // Returns false (unchanged) for a mask with no valid bit.
    unsigned getRecallScope() const noexcept { return show.ui.recallScope; }
    bool setRecallScope(unsigned mask);
    std::function<void()> onScopeChanged;     // message thread; lets views refresh their scope toggles

    // MIDI learn
    void startLearn(const LearnRequest&);
    void cancelLearn() { learning.store(false); }
    bool isLearning() const noexcept { return learning.load(); }
    std::function<void(const ControlBinding&)> onLearned;
    int removeBindingsFor(BindTarget, int targetIndex);
    // Last incoming MIDI for the diagnostics / settings page (relaxed, may be stale).
    std::atomic<int> lastMidiNumber { -1 }, lastMidiValue { 0 }, lastMidiChannel { 0 };

private:
    void timerCallback() override { pump(); }
    void applyBinding(const ControlBinding&, const MidiEvent&);

    juce::AudioProcessorValueTreeState& apvts;
    SceneBank& scenes;
    ShowModel& show;

    static constexpr int kQueueSize = 512;
    juce::AbstractFifo fifo { kQueueSize };
    std::array<MidiEvent, kQueueSize> queue {};
    std::atomic<bool> learning { false };
    LearnRequest learnRequest;
};
}
