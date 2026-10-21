#pragma once
// TempoHub: single source of FxContext tempo (bpm / hasTempo / ppq / playing) for every tempo-synced effect, quantised
// trigger and tempo-synced clip (docs/BEAT_SYNC_SPEC.md). Selectable source: Host playhead, Tap tempo, Manual BPM, MIDI beat
// clock, or Auto (onset-energy BeatTracker).
//
// Threading:
//  * message thread : tap(), resyncDownbeat(), resetTaps(). They only touch message-thread-owned TapTempo state and
//                     publish results through atomics.
//  * audio thread   : midi*() (queue realtime MIDI for the coming block), process() (once per host block, before the engine).
//  * any thread     : snapshot() (UI, tests) -- relaxed atomics, may be a block stale.
// The hub never allocates, locks or logs on the audio thread. Every number it returns is finite and bpm is inside
// [kMinBpm, kMaxBpm] whenever hasTempo is true.
#include "DSP/BeatTracker.h"
#include "DSP/MidiClockTempo.h"
#include "DSP/TapTempo.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace crowdmike {
enum class TempoSource : int { Host = 0, Tap = 1, Manual = 2, MidiClock = 3, Auto = 4 };

struct HostTempoInfo {
    bool hasBpm = false;
    double bpm = 120.0;
    bool hasPpq = false;
    double ppq = 0.0;
    bool playing = false;
};

struct TempoParams {
    int source = 0;           // TempoSource
    double manualBpm = 120.0;
    double nudgeBpm = 0.0;    // added to Tap / Manual / Auto tempos (not to Host or MIDI clock)
    int barBeats = 4;
};

struct TempoState {
    double bpm = 120.0;
    bool hasTempo = false;
    double ppq = 0.0;
    bool playing = false;
    float confidence = 0.0f;       // 0..1; Auto reports the tracker confidence, other sources 1 while a tempo exists
    bool lowConfidence = false;    // Auto below threshold, MIDI clock lost, ...
    int source = 0;                // TempoSource that produced this state
    int barBeats = 4;
};

class TempoHub {
public:
    static constexpr double kMinBpm = 20.0, kMaxBpm = 300.0;
    static constexpr float kLowConfidence = 0.3f;

    void prepare(double sampleRate, int maxBlock);
    void attachTracker(dsp::BeatTracker* tracker) noexcept { beat = tracker; }

    // ---- message thread
    // Registers a tap at `nowSec` (monotonic seconds). Returns true when a tap tempo is available.
    bool tap(double nowSec);
    void resetTaps();
    // Declares "now" to be beat 1 of a bar: the beat position restarts at 0 (free-running sources).
    void resyncDownbeat() noexcept { resyncRequest.store(true, std::memory_order_release); }
    bool hasTapTempo() const noexcept { return tapHas.load(std::memory_order_relaxed); }
    double getTapBpm() const noexcept { return tapBpm.load(std::memory_order_relaxed); }

    // ---- audio thread: realtime MIDI of the block that process() is about to handle (offset in samples)
    void midiClockTick(int offset) noexcept { pushEvent(Ev::Tick, offset); }
    void midiStart(int offset) noexcept { pushEvent(Ev::Start, offset); }
    void midiContinue(int offset) noexcept { pushEvent(Ev::Continue, offset); }
    void midiStop(int offset) noexcept { pushEvent(Ev::Stop, offset); }

    TempoState process(int numSamples, const TempoParams& params, const HostTempoInfo& host) noexcept;

    TempoState snapshot() const noexcept;
    const dsp::MidiClockTempo& getClock() const noexcept { return clock; }

private:
    enum class Ev : int { Tick, Start, Continue, Stop };
    struct Event { Ev type; int offset; };
    static constexpr int kMaxEvents = 128;

    void pushEvent(Ev t, int off) noexcept {
        if (eventCount < kMaxEvents) events[static_cast<size_t>(eventCount++)] = { t, off < 0 ? 0 : off };
    }
    void publish(const TempoState&) noexcept;

    double sr = 48000.0;
    dsp::BeatTracker* beat = nullptr;

    // message-thread tap state + hand-over
    dsp::TapTempo taps;
    std::atomic<bool> tapHas { false };
    std::atomic<double> tapBpm { 120.0 };
    std::atomic<std::uint32_t> tapSeq { 0 };
    std::atomic<bool> resyncRequest { false };

    // audio-thread state
    dsp::MidiClockTempo clock;
    std::array<Event, kMaxEvents> events {};
    int eventCount = 0;
    std::int64_t absSamples = 0;
    std::uint32_t seenTapSeq = 0;
    double ppqFree = 0.0;        // free-running beat position (Tap / Manual / Auto), also kept continuous across sources
    double bpmOut = 120.0;       // smoothed Auto tempo
    bool autoHad = false;
    double lastClockPpq = 0.0;
    std::int64_t lastTrackerNow = -1;

    // published snapshot
    std::atomic<double> sBpm { 120.0 }, sPpq { 0.0 };
    std::atomic<bool> sHas { false }, sPlaying { false }, sLow { false };
    std::atomic<float> sConf { 0.0f };
    std::atomic<int> sSource { 0 }, sBarBeats { 4 };
};
}
