#pragma once
// Auto-record on host transport (RECORDING_SPEC 6): starts a take when the host transport starts playing and ends it
// again when the transport stops, but only for takes that this trigger started itself.
//
// THREADS: setHostPlaying() from the audio thread (one relaxed atomic store); everything else from ONE control thread
// (the processor's 20 Hz timer). Starting a take opens files, so it is never done on the audio thread.
#include <atomic>

namespace crowdmike::rec {

class TransportRecordTrigger {
public:
    enum class Action { None, Start, Stop };

    void setEnabled(bool on) noexcept { enabled = on; }
    bool isEnabled() const noexcept { return enabled; }
    void setHostPlaying(bool playing) noexcept { hostPlaying.store(playing, std::memory_order_relaxed); }

    /** Call periodically. Start: transport started while enabled and not recording (the caller tries to start and
        reports success through noteStarted()). Stop: transport stopped while recording a take that noteStarted()
        claimed. Enabling while the transport already runs does nothing until the next start. */
    Action poll(bool recording) noexcept {
        const bool p = hostPlaying.load(std::memory_order_relaxed);
        const bool rising = p && ! lastPlaying, falling = ! p && lastPlaying;
        lastPlaying = p;
        if (! recording) ownsTake = false;
        if (! enabled) return Action::None;
        if (rising && ! recording) return Action::Start;
        if (falling && recording && ownsTake) { ownsTake = false; return Action::Stop; }
        return Action::None;
    }
    void noteStarted() noexcept { ownsTake = true; }

private:
    std::atomic<bool> hostPlaying { false };
    bool enabled = false, lastPlaying = false, ownsTake = false;
};

} // namespace crowdmike::rec
