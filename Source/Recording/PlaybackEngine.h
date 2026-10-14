#pragma once
// PlaybackEngine: clip player + crowd bed (RECORDING_SPEC sections 9, 10, 12).
//
// THREADS
//   Audio thread : process() only (noexcept, no locks/alloc/I/O, never frees a clip).
//   Control      : every other public function, from any NON-audio thread. Transport/settings are delivered to the
//                  audio thread through a lock-free command queue consumed at the top of process(), so they take
//                  effect on the next block (sample-accurate offsets via trigger(slot, offset)).
//   Loader       : internal juce::Thread decodes + resamples files to the engine rate, computes the waveform, then
//                  publishes an immutable ref-counted ClipData through an atomic pointer. Clips replaced/unloaded on
//                  the audio thread are pushed to a retire queue and released on the loader thread.
//   prepare()/releaseResources() must not run concurrently with process().
#include "LockFreeQueue.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <deque>
#include <memory>

namespace crowdmike::rec {

class PlaybackEngine {
public:
    static constexpr int kMaxClips = 32;
    static constexpr int kNumDestinations = 10;              // [0] master, [1..8] group k, [9] direct playback bus
    static constexpr double kMaxClipSeconds = 15.0 * 60.0;
    static constexpr juce::int64 kMaxTotalClipBytes = 3ll << 30;   // decoded memory budget for all clips
    static constexpr int kWaveformPoints = 1024;
    static constexpr double kClickRampMs = 5.0;              // start/stop/panic ramp (>= 2 ms)

    struct ClipSettings {
        float gainDb = 0.0f;
        float fadeInMs = 0.0f, fadeOutMs = 0.0f;   // fade-in from (re)trigger; fade-out before a one-shot end / on stop
        double startFrac = 0.0, endFrac = 1.0;     // trim, 0..1 of the file
        enum Mode { OneShot, Loop } mode = OneShot;
        bool reverse = false;                      // plays [start,end) backwards (index-wise, no live effect)
        int route = 0;                             // 0 master, 1..8 group k, 9 direct playback bus
        bool tempoSync = false;                    // stored only; the engine quantises via trigger(slot, offset)
        juce::String name;                         // display name (empty -> file name)
    };

    struct ClipInfo {
        bool loaded = false, loading = false;
        juce::String error;
        double durationSeconds = 0.0;              // whole file
        juce::String name;
        bool playing = false;
        double playPosition01 = 0.0;               // position inside the trimmed region
        int numChannels = 0;
        double sourceSampleRate = 0.0;
    };

    struct BedSettings {
        int slot = 0;                              // clip slot used as the bed source
        bool enabled = false;
        double minLoopSec = 20.0, maxLoopSec = 60.0;   // random region length
        double crossfadeSec = 4.0;                 // equal-power crossfade between regions
        float levelVariationDb = 2.0f;             // slow random level drift +/- this
        float lowpassHz = 0.0f;                    // one-pole low-pass, 0 = off
        float gainDb = -12.0f;
        int route = 0;
        unsigned seed = 1;                         // deterministic RNG seed (restart on enable/seed change)
    };

    PlaybackEngine();
    ~PlaybackEngine();

    //==== lifecycle (message thread, audio stopped) ==================================================================
    /** Starts the loader thread. If the sample rate changed, loaded clips are re-decoded at the new rate. */
    void prepare(double sampleRate, int maxBlock);
    void releaseResources();

    //==== control (any non-audio thread) =============================================================================
    /** Asynchronous; see getClipInfo(). Before prepare() the load is deferred until prepare(). */
    bool loadClip(int slot, const juce::File& file);
    void unloadClip(int slot);
    /** Starts (or restarts, crossfading) the clip `startOffsetSamples` into the next processed block. Ignored while
        the panic mute is active or no clip is loaded. Returns false if the command queue is full. */
    bool trigger(int slot, int startOffsetSamples = 0);
    bool stop(int slot);
    bool stopAll();
    bool setSettings(int slot, const ClipSettings&);
    ClipSettings getSettings(int slot) const;
    bool setBed(const BedSettings&);
    BedSettings getBed() const;
    ClipInfo getClipInfo(int slot) const;
    /** Min/max envelope (all channels) resampled to `points`. Returns false if no clip is loaded. */
    bool getWaveform(int slot, float* minOut, float* maxOut, int points) const;
    /** Waits until no load is pending (convenience/tests). */
    bool waitForLoads(int timeoutMs) const;
    /** Releases clips retired by the audio thread (the loader thread also does this every ~20 ms). */
    void releaseRetired();

    //==== audio thread ===============================================================================================
    /** ADDS all playing clips and the bed into destL[route]/destR[route] (arrays of kNumDestinations pointers; any
        pointer, or either array, may be nullptr; a mono clip feeds L and R at unity). muteToReinforcement (panic):
        everything ramps out within kClickRampMs and stops; triggers are ignored while it is set; the bed fades back
        in slowly after it clears. */
    void process(float* const* destL, float* const* destR, int numSamples, bool muteToReinforcement) noexcept;

private:
    struct ClipData;
    struct Voice;
    struct AudioSlot;
    struct ControlSlot;
    struct Bed;
    struct Params {
        float gainDb = 0, fadeInMs = 0, fadeOutMs = 0;
        double startFrac = 0, endFrac = 1;
        int mode = 0;
        bool reverse = false;
        int route = 0;
        bool tempoSync = false;
    };
    struct Command {
        enum Type : int { Trigger, Stop, StopAll, SetParams, SetBed, Unload } type = Trigger;
        int slot = 0, offset = 0;
        Params params;
        BedSettings bed;
    };
    class LoaderThread;

    static Params toParams(const ClipSettings&);
    void runJob(int slot, const juce::File&, juce::uint64 generation);
    void publish(int slot, ClipData* clip);    // takes one reference
    void clearAudioState();
    void handleCommand(const Command&, int numSamples) noexcept;
    void startVoice(AudioSlot&, int offset, std::int64_t pos, std::int64_t played, bool firstPass, bool fadeFromZero, int route, int numSamples) noexcept;
    void renderVoice(AudioSlot&, Voice&, float* const* dL, float* const* dR, int n) noexcept;
    void renderBed(float* const* dL, float* const* dR, int n, bool mute) noexcept;

    double sampleRate = 0.0;
    int maxBlockSize = 512;
    int rampFrames = 240;

    std::unique_ptr<std::array<AudioSlot, kMaxClips>> audioSlots;
    std::unique_ptr<Bed> bed;
    bool muted = false;

    CommandQueue<Command, 512> commands;
    RetireQueue<ClipData*, 256> retire;

    mutable std::mutex controlLock;
    std::unique_ptr<std::array<ControlSlot, kMaxClips>> controlSlots;
    BedSettings bedSettings;
    std::deque<std::tuple<int, juce::File, juce::uint64>> jobs;
    std::atomic<int> jobsPending { 0 };
    std::unique_ptr<LoaderThread> loader;
    std::atomic<bool> cancelLoads { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackEngine)
};

} // namespace crowdmike::rec
