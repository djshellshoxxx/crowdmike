#pragma once
// PlaybackEngine: clip player + crowd bed (RECORDING_SPEC sections 9, 10, 12).
//
// THREADS
//   Audio thread : process() and setTempo() only (noexcept, no locks/alloc/I/O, never frees a clip).
//   Control      : every other public function, from any NON-audio thread. Transport/settings are delivered to the
//                  audio thread through a lock-free command queue consumed at the top of process(), so they take
//                  effect on the next block (sample-accurate offsets via trigger(slot, offset)).
//   Loader       : internal juce::Thread decodes short clips into memory ("resident") or opens long clips for
//                  streaming, computes the waveform and (streamed clips) the pre-buffered head, then publishes an
//                  immutable ref-counted ClipData through an atomic pointer. Clips replaced/unloaded on the audio
//                  thread are pushed to a retire queue and released on the loader thread.
//   Streamer     : internal juce::Thread (StreamPool) that keeps the read-ahead ring buffers of streamed clips and
//                  beds filled (RECORDING_SPEC 9.1). The audio thread never waits for it: a late ring is an underrun
//                  (silence + counter), see getStreamStats().
//   prepare()/releaseResources() must not run concurrently with process().
#include "LockFreeQueue.h"
#include "StreamPool.h"
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
    static constexpr double kMaxClipSeconds = 24.0 * 3600.0; // hard limit for streamed clips
    static constexpr double kDefaultResidentSeconds = 120.0; // clips up to this length are decoded into memory
    static constexpr juce::int64 kMaxResidentBytes = 1ll << 30;   // decoded memory budget of all resident clips
    static constexpr double kHeadSeconds = 1.0;              // pre-buffered start of a streamed clip
    static constexpr int kWaveformPoints = 1024;
    static constexpr double kClickRampMs = 5.0;              // start/stop/panic ramp (>= 2 ms)
    static constexpr int kNumQuantise = 6;                   // tempo-sync grid choices, see quantiseBeats()
    static constexpr int kMaxMinLoopStreamSeconds = 3;       // streamed beds use at least this region length

    /** Grid of a tempo-synced trigger in quarter-note beats: 1/16, 1/8, 1/4 (beat), 1/2 (2 beats), 1 bar (4 beats),
        2 bars (8 beats). 4/4 is assumed for "bar". */
    static double quantiseBeats(int index) noexcept {
        static constexpr double t[kNumQuantise] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
        return t[index < 0 ? 0 : (index >= kNumQuantise ? kNumQuantise - 1 : index)];
    }

    struct ClipSettings {
        float gainDb = 0.0f;
        float fadeInMs = 0.0f, fadeOutMs = 0.0f;   // fade-in from (re)trigger; fade-out before a one-shot end / on stop
        double startFrac = 0.0, endFrac = 1.0;     // trim, 0..1 of the file
        enum Mode { OneShot, Loop } mode = OneShot;
        bool reverse = false;                      // plays [start,end) backwards (index-wise, no live effect)
        int route = 0;                             // 0 master, 1..8 group k, 9 direct playback bus
        bool tempoSync = false;                    // quantise triggers (and loop restarts) to the tempo grid
        juce::String name;                         // display name (empty -> file name)
        // ---- appended (tempo sync, RECORDING_SPEC 9.2); defaults keep older saved shows unchanged
        int syncQuantise = 4;                      // index into quantiseBeats(): trigger grid (default 1 bar)
        double syncLoopBeats = 0.0;                // Loop + sync: loop period in beats, 0 = natural region length
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
        // ---- appended
        bool streaming = false;                    // read ahead from disk (long clip) instead of resident memory
        bool syncPending = false;                  // a tempo-synced trigger is waiting for its grid line
        int headFrames = 0;                        // pre-buffered head of a streamed clip
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

    /** Tempo inputs of the current block (audio thread). Supplied by the engine from the host play head (or later a
        standalone tempo source) through setTempo(). */
    struct TempoInfo {
        double bpm = 120.0;
        double ppqPosition = 0.0;      // quarter-note position of the FIRST sample of the block
        bool hasTempo = false;
        bool playing = false;
    };

    struct MemoryStats {
        juce::int64 residentBytes = 0;   // decoded audio of resident clips
        juce::int64 headBytes = 0;       // pre-buffered heads of streamed clips
        juce::int64 ringBytes = 0;       // read-ahead rings (fixed after prepare)
        juce::int64 totalBytes = 0;
        int residentClips = 0, streamedClips = 0;
    };

    PlaybackEngine();
    ~PlaybackEngine();

    //==== lifecycle (message thread, audio stopped) ==================================================================
    /** Starts the loader thread (and the streamer thread unless startStreamThread is false: tests then call
        pumpStreamsForTesting()). If the sample rate changed, loaded clips are re-decoded at the new rate. */
    void prepare(double sampleRate, int maxBlock, bool startStreamThread = true);
    void releaseResources();

    //==== control (any non-audio thread) =============================================================================
    /** Asynchronous; see getClipInfo(). Before prepare() the load is deferred until prepare(). */
    bool loadClip(int slot, const juce::File& file);
    void unloadClip(int slot);
    /** Starts (or restarts, crossfading) the clip `startOffsetSamples` into the next processed block. With tempoSync
        enabled in the clip settings and tempo available the start is deferred to the next grid line. Ignored while
        the panic mute is active or no clip is loaded. Returns false if the command queue is full. */
    bool trigger(int slot, int startOffsetSamples = 0);
    /** AUDIO THREAD ONLY (the thread that calls process(), before process() in the same block): starts the clip
        `startOffsetSamples` into that block. Lock-free; used by the engine's beat-quantised clip triggers. */
    bool triggerFromAudioThread(int slot, int startOffsetSamples, bool alreadyQuantised = true) noexcept;
    /** Like trigger() but never quantised. */
    bool triggerNow(int slot, int startOffsetSamples = 0);
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
    /** Clips longer than this many seconds (at the engine rate) are streamed; applies to the next load. */
    void setResidentLimitSeconds(double seconds);
    StreamPool::Stats getStreamStats() const { return pool.getStats(); }
    void resetStreamStats() { pool.resetStats(); }
    MemoryStats getMemoryStats() const;
    /** One streamer iteration on the calling thread (tests with prepare(..., false)). */
    void pumpStreamsForTesting() { pool.pump(); }

    //==== audio thread ===============================================================================================
    /** Tempo of the block about to be processed; call before process(). Without it (or with hasTempo = false) tempo
        sync degrades to immediate starts. */
    void setTempo(const TempoInfo& t) noexcept { tempoIn = t; tempoSet = true; }
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
        int syncQuantise = 4;
        double syncLoopBeats = 0.0;
    };
    struct Command {
        enum Type : int { Trigger, Stop, StopAll, SetParams, SetBed, Unload } type = Trigger;
        int slot = 0, offset = 0;
        bool immediate = false;
        Params params;
        BedSettings bed;
    };
    struct Job {
        int slot = 0;
        juce::File file;
        juce::uint64 generation = 0;
        bool rehead = false;
    };
    class LoaderThread;

    static Params toParams(const ClipSettings&);
    void runJob(const Job&);
    void runLoad(const Job&);
    void runRehead(const Job&);
    void publish(int slot, ClipData* clip);    // takes one reference
    void clearAudioState();
    void handleCommand(const Command&, int numSamples) noexcept;
    void startVoice(AudioSlot&, int offset, std::int64_t pos, std::int64_t played, bool firstPass, bool fadeFromZero,
                    int route, int numSamples, double startBeat, bool hasBeat) noexcept;
    void renderVoice(AudioSlot&, Voice&, float* const* dL, float* const* dR, int n) noexcept;
    void renderBed(float* const* dL, float* const* dR, int n, bool mute) noexcept;
    void renderBedStreamed(bool want, bool switching, float* const* dL, float* const* dR, int n, bool mute) noexcept;
    void endVoice(Voice&) noexcept;
    bool attachRing(Voice&, const ClipData&, const Params&) noexcept;
    void updateBeatClock(int n) noexcept;
    void fireDueTriggers(int n) noexcept;

    double sampleRate = 0.0;
    int maxBlockSize = 512;
    int rampFrames = 240;

    StreamPool pool;   // before the slots so it outlives every ClipData / voice
    std::atomic<juce::int64> clipBytesLive { 0 }, headBytesLive { 0 };
    std::atomic<int> residentClipsLive { 0 }, streamedClipsLive { 0 };
    std::atomic<double> residentLimitSeconds { kDefaultResidentSeconds };

    std::unique_ptr<std::array<AudioSlot, kMaxClips>> audioSlots;
    std::unique_ptr<Bed> bed;
    bool muted = false;

    // tempo (audio thread)
    TempoInfo tempoIn;
    bool tempoSet = false;
    bool clockValid = false, wasPlaying = false;
    double beat0 = 0.0, bps = 0.0;      // beat position of the first sample of this block, beats per sample
    double freeBeat = 0.0;              // free-running beat clock while the host transport is stopped
    bool beatKnown = false;             // tempo valid for this block

    CommandQueue<Command, 512> commands;
    SpscQueue<Command, 32> audioCommands;   // single producer = the audio thread (triggerFromAudioThread)
    RetireQueue<ClipData*, 256> retire;

    mutable std::mutex controlLock;
    std::unique_ptr<std::array<ControlSlot, kMaxClips>> controlSlots;
    BedSettings bedSettings;
    std::deque<Job> jobs;
    std::atomic<int> jobsPending { 0 };
    std::unique_ptr<LoaderThread> loader;
    std::atomic<bool> cancelLoads { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaybackEngine)
};

} // namespace crowdmike::rec
