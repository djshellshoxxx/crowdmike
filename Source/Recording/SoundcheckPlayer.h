#pragma once
// SoundcheckPlayer: virtual soundcheck - recorded raw tracks substitute for the live inputs (RECORDING_SPEC 11).
//
// THREADS
//   Audio thread : process(), isReplacingInputs() (noexcept, lock-free, no alloc, never frees audio data).
//   Control      : everything else, from any non-audio thread. Transport goes through a lock-free command queue
//                  consumed at the top of process(). Loading runs on an internal background thread; the decoded,
//                  immutable data is published with an atomic pointer and the replaced one is released off the
//                  audio thread (retire queue).
//   prepare()/releaseResources() must not run concurrently with process().
//
// SAFETY
//   Default LOCKED. While locked process() never touches the inputs and arm() is refused. After setLocked(false)
//   an explicit arm() is required; the recorded signal then fades in over >= 2 s. disarm() fades out (0.5 s), stop()
//   fades out (250 ms); pause/seek/play use 5 ms de-click ramps. Playback speed is always 1x.
#include "SessionMetadata.h"
#include "LockFreeQueue.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace crowdmike::rec {

class SoundcheckPlayer {
public:
    static constexpr int kMaxInputs = 16;
    static constexpr double kMinArmFadeSeconds = 2.0;
    static constexpr juce::int64 kMaxMemoryBytes = 3ll << 30;   // decoded audio budget

    struct InputFile { int inputIndex = 0; juce::File file; };   // a stereo file fills inputIndex and inputIndex+1
    struct InputMapping {
        int inputIndex = 0;
        int track = -1, sourceId = -1, channel = 0;   // channel within the recorded track (0/1)
        juce::String name;
        juce::File file;                                // first file of the track
    };
    struct LoadState {
        bool loading = false, loaded = false;
        juce::String error;                             // also non-fatal warnings (e.g. tracks that did not fit)
        int numInputs = 0;
        double durationSeconds = 0.0;
    };
    /** Maps a recorded track to its first input index (-1 = automatic). Default rule: sourceId if it is a free
        input index 0..15 (the engine's raw-input taps), otherwise the next free input(s) in track order. */
    using MappingFunction = std::function<int(const SessionInfo::Track&)>;

    SoundcheckPlayer();
    ~SoundcheckPlayer();

    //==== lifecycle (message thread, audio stopped) ==================================================================
    void prepare(double sampleRate, int maxBlock);    // reloads at the new rate if it changed
    void releaseResources();

    //==== loading (non-audio threads, asynchronous) ==================================================================
    bool loadSession(const juce::File& sessionJson, MappingFunction mapping = {});
    bool loadFiles(const std::vector<InputFile>& files);
    LoadState getLoadState() const;              // also releases replaced data (poll from the UI)
    std::vector<InputMapping> getInputMapping() const;
    bool waitForLoad(int timeoutMs) const;

    //==== safety / source switch =====================================================================================
    void setLocked(bool locked);                 // locking takes effect immediately (inputs are live again)
    bool isLocked() const noexcept { return locked.load(); }
    bool arm();                                  // false if locked or nothing loaded
    void disarm();
    bool isArmed() const noexcept { return armedState.load(); }
    void setArmFadeSeconds(double seconds);      // clamped to >= kMinArmFadeSeconds

    //==== transport ==================================================================================================
    void play();
    void pause();
    void stop();                                 // fades out, then rewinds to the loop start (or 0)
    void seekSeconds(double seconds);
    void setLoopRange(double startSeconds, double endSeconds, bool enabled);
    bool isPlaying() const noexcept { return playingState.load(); }
    double getPositionSeconds() const noexcept;
    double getDurationSeconds() const noexcept;

    //==== audio thread ===============================================================================================
    /** When armed (or fading out), REPLACES inputs[0..numInputChannels-1] (mono buffers, up to kMaxInputs) with the
        recorded inputs; inputs without recorded data become silence. Otherwise leaves them untouched. */
    void process(float* const* inputs, int numInputChannels, int numSamples) noexcept;
    bool isReplacingInputs() const noexcept { return replacing.load(std::memory_order_relaxed); }

private:
    struct Data;
    struct Command {
        enum Type : int { Play, Pause, Stop, Seek, Loop, Arm, Disarm } type = Play;
        std::int64_t a = 0, b = 0;
        bool flag = false;
    };
    struct Source { int inputIndex; std::vector<std::pair<juce::File, std::int64_t>> parts; double partRate; int track, sourceId; juce::String name; };
    class Loader;

    void startLoad(std::vector<Source> sources, juce::String warning);
    void runLoad(const std::vector<Source>& sources, juce::uint64 gen);
    void clearAudioData();
    void releaseRetired() const;
    void push(const Command&);

    double sampleRate = 0.0;
    int rampFrames = 240;
    double armFadeSeconds = 2.5;

    // audio-thread state
    std::atomic<Data*> pending { nullptr };
    Data* current = nullptr;
    std::int64_t pos = 0, loopStart = 0, loopEnd = 0, seekTarget = 0;
    bool loopOn = false, playing = false, armedA = false;
    float env = 0.0f;               // arm fade
    float dec = 0.0f, decTarget = 0.0f, decStep = 0.0f;
    int pendingAction = 0;          // 0 none, 1 pause, 2 stop, 3 seek
    std::int64_t xfFrom = 0, xfLen = 0, xfK = 0;

    std::atomic<bool> locked { true }, armedState { false }, playingState { false }, replacing { false };
    std::atomic<std::int64_t> positionFrames { 0 }, durationFrames { 0 };
    CommandQueue<Command, 256> commands;
    mutable RetireQueue<Data*, 16> retire;

    mutable std::mutex controlLock;
    LoadState loadState;
    std::vector<InputMapping> mapping;
    std::vector<Source> lastSources;
    juce::uint64 generation = 0;
    std::atomic<bool> cancel { false };
    std::unique_ptr<Loader> loader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundcheckPlayer)
};

} // namespace crowdmike::rec
