#pragma once
// RecorderEngine: multitrack WAV recorder (RECORDING_SPEC sections 3-8, 13).
//
// THREADS
//   Audio thread  : isTrackActive(), pushTap(), advance(), getFramePosition()   -- noexcept, no locks/alloc/I/O.
//   Control       : everything else (message thread or any non-audio thread). Control calls may briefly wait for
//                   the writer thread's lock (never the audio thread).
//   Writer thread : internal juce::Thread (wakes every 10 ms): drains FIFOs, maintains pre-roll rings, writes,
//                   splits, flushes, detects disk errors, writes the session JSON.
//   prepare()/shutdown() must not run concurrently with the audio-thread calls.
//
// TIMELINE CONTRACT (engine side)
//   Per audio block:  for each track t: if (isTrackActive(t)) pushTap(t, ...);   then   advance(numSamples).
//   All taps of a block must be pushed BEFORE advance() for that block; at most one pushTap per track per block.
//   The recorder stamps each pushed block with getFramePosition(); gaps (dropouts, disarmed tracks) are written as
//   silence so every file of a take starts on the same frame and all tracks stay sample-aligned.
//   Recording starts on the block boundary following startRecording() (latched in advance()); the file begins
//   preRoll seconds earlier when pre-roll history is available.
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

class RecorderEngine {
public:
    static constexpr int kMaxTracks = 32;
    static constexpr int kDefaultFifoFrames = 1 << 17;              // per channel, per track
    static constexpr juce::int64 kMaxPreRollBytes = 512ll << 20;    // total pre-roll memory cap

    struct TrackConfig {
        int sourceId = 0;        // opaque id chosen by the engine; stored in the session JSON
        int numChannels = 1;     // 1 or 2
        juce::String name;       // {source} token; "Track N" when empty
        bool armed = false;
    };

    struct RecordOptions {
        juce::File folder;
        enum Format { Wav16, Wav24, Wav32f } format = Wav24;
        juce::String namePattern = "{date}_{time}_{scene}_{source}";
        juce::String scene, show, venue;
        int preRollSeconds = 0;          // 0..60: history to include (limited by setPreRollSeconds()/what is buffered)
        int postRollSeconds = 0;         // 0..30: stopRecording() keeps recording this long
        double maxFileSeconds = 0.0;     // automatic split, 0 = off
        juce::int64 maxFileBytes = 0;    // automatic split, 0 = off (approximate, per file)
        juce::var routeSnapshot;         // opaque, copied into the session JSON
        juce::String appVersion;
    };

    struct Status {
        bool recording = false;          // a take is open (including while waiting for post-roll)
        bool postRoll = false;           // stop requested, post-roll still running
        double elapsedSeconds = 0.0;     // length of the current take incl. pre-roll
        juce::int64 framesWritten = 0;   // frames on the take timeline written so far
        int dropouts = 0;                // blocks dropped because a FIFO was full (since prepare)
        int errors = 0;                  // disk/file errors (since prepare)
        double freeDiskGB = -1.0;        // of the last record folder, -1 unknown
        double fifoFill = 0.0;           // 0..1, worst track
        int take = 0;                    // number of the current/last take (auto-increments per startRecording)
        double preRollSeconds = 0.0;     // effective pre-roll buffer length
        bool preRollClipped = false;     // pre-roll reduced to fit kMaxPreRollBytes
        juce::String lastError;
    };

    using OutputStreamFactory = std::function<std::unique_ptr<juce::OutputStream>(const juce::File&)>;

    RecorderEngine();
    ~RecorderEngine();

    //==== lifecycle (message thread, audio stopped) ================================================================
    /** Allocates the FIFOs (kMaxTracks x 2 ch x fifoFramesPerTrack, rounded up to a power of two) and (re)starts the
        writer thread. Finalises any open take. Resets the frame clock to 0. startWriterThread=false is for tests
        (drive with drainForTesting()). */
    void prepare(double sampleRate, int maxBlock, int fifoFramesPerTrack = kDefaultFifoFrames,
                 bool startWriterThread = true);
    /** Finalises any take (stopRecordingNow), stops the writer thread, frees buffers. */
    void shutdown();

    //==== audio thread ===============================================================================================
    /** armed AND (recording OR post-roll running OR pre-roll enabled). */
    bool isTrackActive(int track) const noexcept;
    /** Copies one block into the track FIFO. Missing channels duplicate channel 0, extra channels are ignored,
        nullptr channels are silence. If the FIFO lacks space the block is dropped, counted and logged. Never blocks. */
    void pushTap(int track, const float* const* channels, int numChannels, int numSamples) noexcept;
    /** Advances the frame clock by one block (call after all pushTap()s of the block). */
    void advance(int numSamples) noexcept;
    std::int64_t getFramePosition() const noexcept { return framePosition.load(std::memory_order_acquire); }

    //==== control (non-audio threads) ================================================================================
    /** numChannels clamped to 1..2. Allowed while recording: tracks armed mid-take join at the next take; a track
        disarmed mid-take records silence until the take ends. */
    bool setTrack(int track, const TrackConfig&);
    TrackConfig getTrack(int track) const;
    /** Size of the rolling pre-roll buffer kept for armed tracks while not recording (0 = off, max 60 s). The buffer
        is (re)allocated by the writer thread when no take is open. */
    void setPreRollSeconds(int seconds);
    /** Total pre-roll memory cap (default kMaxPreRollBytes); the pre-roll length is reduced to fit and
        Status::preRollClipped is set. */
    void setPreRollMemoryLimit(juce::int64 bytes);

    /** Opens one WAV per armed track (creating the folder if needed). Returns false (see getStatus().lastError) if
        not prepared, already recording, nothing armed or a file cannot be created; nothing is left behind then. */
    bool startRecording(const RecordOptions&);
    /** Keeps recording RecordOptions::postRollSeconds, then finalises (asynchronously, on the writer thread). */
    void stopRecording();
    /** Finalises immediately (synchronously): files and session JSON are complete when this returns. */
    void stopRecordingNow();
    /** Closes the current files and continues seamlessly in new "<base>_partNNN.wav" files (no lost frames). */
    void splitFile();

    void addMarker(const juce::String& label, MarkerType type);   // any non-audio thread; frame = getFramePosition()
    /** Like addMarker() but at an explicit absolute engine frame (clamped to [0, getFramePosition()]); used by the Show
        Report to place a "Crowd peak" marker on the moment of the peak instead of the moment it was detected. */
    void addMarkerAt(juce::int64 frame, const juce::String& label, MarkerType type);
    std::vector<Marker> getMarkers() const;                        // absolute engine frames
    void clearMarkers();

    Status getStatus() const;
    float getTrackPeak(int track);                // peak of written data since last call (resets)
    bool hasTrackError(int track) const;
    int getTrackDropouts(int track) const;
    /** Session JSON of the current/last take (empty File before the first take). */
    juce::File getLastSessionFile() const;

    static bool readSession(const juce::File& json, SessionInfo& out) { return SessionMetadata::read(json, out); }

    //==== testing hooks ==============================================================================================
    /** Runs one writer iteration on the calling thread (safe also while the writer thread runs). */
    void drainForTesting();
    /** Replace file streams (e.g. to simulate a full disk). nullptr restores FileOutputStream. */
    void setOutputStreamFactoryForTesting(OutputStreamFactory);
    /** FIFO + pre-roll bytes currently allocated (memory-hygiene tests). */
    std::size_t getAllocatedBytes() const;

private:
    struct Fifo;
    struct PreRoll;
    struct SessionTrack;
    struct Session;
    class WriterThread;

    void processOnceLocked();
    void drainTrack(int t, std::int64_t posSnapshot);
    void handleChunk(int t, std::int64_t frame, const float* const* data, int n);
    void writeTimeline(SessionTrack&, std::int64_t frame, const float* const* data, int n);
    void writeSpan(SessionTrack&, const float* const* data, int n);
    std::int64_t currentPartEnd(const SessionTrack&) const;
    bool openPartFile(SessionTrack&, juce::String& error);
    void closePart(SessionTrack&);
    void rotate(SessionTrack&);
    void beginTakeLocked();
    void finaliseTakeLocked();
    void writeSessionJsonLocked();
    void reallocatePreRollLocked();
    void setError(const juce::String&);
    void failTrack(SessionTrack&, const juce::String&);
    void stopWriterThread();

    // ---- configuration
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    std::atomic<bool> prepared { false };

    // ---- audio-thread visible state
    std::atomic<std::int64_t> framePosition { 0 };
    std::atomic<bool> startRequested { false }, startLatched { false }, recActive { false };
    std::atomic<std::int64_t> startFrame { 0 };
    std::atomic<std::int64_t> stopFrame { std::numeric_limits<std::int64_t>::max() };
    std::atomic<int> preRollWanted { 0 };
    std::array<std::atomic<bool>, kMaxTracks> armed {};
    std::array<std::unique_ptr<Fifo>, kMaxTracks> fifos;
    SpscQueue<DropoutEvent, 256> dropoutEvents;
    std::atomic<int> totalDropouts { 0 }, totalErrors { 0 };

    // ---- control / writer state (guarded by writerLock unless atomic)
    mutable juce::CriticalSection writerLock;
    std::array<TrackConfig, kMaxTracks> configs;   // guarded by configLock
    mutable juce::CriticalSection configLock;
    std::array<std::unique_ptr<PreRoll>, kMaxTracks> preRolls;
    std::atomic<bool> preRollDirty { false };
    std::atomic<double> preRollEffective { 0.0 };
    std::atomic<bool> preRollClipped { false };
    std::atomic<juce::int64> preRollLimitBytes { kMaxPreRollBytes };
    std::unique_ptr<Session> session;
    std::vector<float> zeros;
    int takeCounter = 0;
    std::atomic<int> currentTake { 0 };
    std::atomic<bool> splitRequested { false };
    std::atomic<bool> sessionOpen { false }, postRollRunning { false };
    std::atomic<std::int64_t> statusFrames { 0 };
    std::atomic<double> freeDiskGB { -1.0 };
    std::array<std::atomic<float>, kMaxTracks> peaks {};
    std::array<std::atomic<bool>, kMaxTracks> trackErrors {};
    juce::File lastSessionFile;
    OutputStreamFactory streamFactory;
    juce::uint32 lastFlushMs = 0, lastDiskCheckMs = 0;

    mutable juce::CriticalSection errorLock;
    juce::String lastError;

    mutable juce::CriticalSection markerLock;
    std::vector<Marker> markers;
    juce::String markerScene;   // scene of the current/last take

    std::unique_ptr<WriterThread> writerThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecorderEngine)
};

} // namespace crowdmike::rec
