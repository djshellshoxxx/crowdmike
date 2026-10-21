# CrowdMike Recording and Playback Specification

## 1. Recorder goals

CrowdMike must be able to capture both archival material and reusable crowd content without compromising the live audio thread.

Record tap types:

- raw input
- processed input
- group
- FX return
- master bus
- individual output bus

## 2. File formats

Initial:

- WAV
- 16-bit PCM
- 24-bit PCM
- 32-bit float

Recommended default:
48 kHz / 24-bit WAV using the current engine sample rate.

Also supported:

- FLAC 24-bit, lossless, written by the JUCE FLAC writer on the writer thread. Track sidecar, markers, splitting and pre-roll behave as for WAV; part files use the `.flac` extension and the session `fileFormat` is `flac24`. FLAC has no float mode, so samples are quantised like WAV 24 (round to nearest, clamp). The disk-rate estimate for FLAC uses the uncompressed 24-bit rate (worst case).
- Playback loads WAV, AIFF, FLAC and any other format JUCE registers (see 9.1).

WAV part files are capped at 4,000,000,000 bytes (RIFF limit) regardless of the user split size; the writer rotates to a new part at that point. FLAC has no such cap.

## 3. Recorder architecture

Audio thread:
- writes interleaved or planar blocks into bounded lock-free FIFOs

Recorder worker:
- drains FIFOs
- writes files
- handles metadata
- rotates/splits files
- reports disk errors asynchronously

No file I/O on audio thread.

## 4. Overrun behavior

If recorder writer cannot keep up:

- live audio must continue
- recorder marks a dropout
- UI shows a clear error
- event/marker log records approximate location
- recording may continue if FIFO recovers

Never block live processing to save a recording.

## 5. Track naming

Default pattern:

`YYYY-MM-DD_HH-MM-SS_Scene_Source.wav`

User-configurable tokens:

- date
- time
- scene
- input/group/output name
- show
- venue
- take

Sanitize filenames per platform.

## 6. Recording controls

- global record
- per-track arm
- pre-roll
- post-roll
- record marker
- split file
- take number
- auto-record on transport optional
- maximum file duration/size split

## 7. Pre-roll

Maintain an optional rolling memory buffer so pressing Record can include the previous 5-60 seconds.

This buffer is preallocated and separate from disk writer logic.

## 8. Markers

Markers contain:

- timestamp/sample position
- user label
- scene
- optional event type

Quick buttons:

- Big Cheer
- Chant
- Applause
- Performer Moment
- Problem/Feedback
- Custom

## 9. Playback engine

Features:

- multiple clips
- waveform cache
- sample-accurate start within engine block constraints
- one-shot
- loop
- gain
- fade in/out
- trim
- reverse clip
- route to group/master/output
- optional tempo sync
- optional pitch/time processing later

Playback file reading uses read-ahead buffers and worker threads.

## 10. Crowd bed mode

A long recording can be looped as a subtle crowd bed.

Features:

- randomized loop region
- long crossfades
- level variation
- optional filtering
- no obvious seam

## 11. Virtual soundcheck

Raw multitrack recordings can substitute for live inputs.

Requirements:

- preserve original channel mapping metadata
- one control to switch live/recorded source
- safety lock preventing accidental output blast
- loop range
- seek
- playback speed remains 1x in v1

## 12. Reverse playback integration

Playback clips may be reversed offline/index-wise without invoking the 4-second Reverse Chunk live effect.

The live effect and clip-level reverse are separate features.

## 13. Session metadata

Store a sidecar project/session JSON containing:

- recording start time
- sample rate
- channel names
- scene name
- route snapshot
- marker list
- file list
- app version

Do not write/serialize this metadata on the audio callback.

## 9.1 Streaming playback (read-ahead)

Clips whose duration (at engine rate) is at most the resident limit (default 120 s) and that fit the resident budget (1 GiB for all resident clips together) are decoded into memory. Longer clips are streamed from disk. The hard limit per clip is 24 hours of source audio; a longer file fails to load with a "too long" error.

A streamed clip keeps in memory only a 1 s pre-buffered head (the first second of the playback region at engine rate), a 2048-point waveform sketch and the file path. Playing voices (and the bed) each own a ring buffer from a pool of 16 (about 1.3 s each, power-of-two length). A worker thread ("CrowdMike clip streamer", polling about every 2 ms) refills rings from per-ring random-access readers. The audio thread never reads files.

- Instant trigger: a voice starts from the head while its ring fills, so trigger latency equals that of a resident clip.
- Rings are produced in playback order. Loop wrap, trim and reverse are applied on the producer side, so loops are seamless and reversed clips read backwards from disk.
- Underrun: when the ring cannot supply a block the voice renders silence for the missing frames, increments the underrun counters (events and frames) and keeps its place in time (the producer skips ahead). The Playback page shows "UNDERRUN" for 6 s and a status message. Counters: `getStreamStats()`.
- Pool exhaustion (more than 16 streamed voices): the extra voice plays from the head only, then goes silent; counted as `starvedAcquires`.
- Read errors on the worker are counted (`readErrors`) and treated as an underrun.
- Memory bound: `getMemoryStats()` reports resident clip bytes, head bytes and ring bytes. A 2 hour bed costs only its head plus rings, independent of its length. Tests assert this bound.
- Trim or reverse changes on a streamed clip rebuild its head on the loader thread; the audio thread keeps the old clip until the new one is published.

## 9.2 Clip tempo sync

`ClipSettings.tempoSync` acts. `PlaybackEngine::setTempo({bpm, ppq, hasTempo, playing})` is called before each `process()` with the host or engine tempo; any other tempo source may feed the same inputs.

- With sync on and a tempo present, a trigger waits for the next grid line, located sample-accurately inside the block. The grid is `syncQuantise` (0..5 = 1/16, 1/8, 1 beat, 2 beats, 1 bar, 2 bars; default 4 = 1 bar; a beat is a quarter note, a bar is 4 beats).
- While the transport is stopped, a free-running beat clock at the current bpm drives the grid. Without a tempo the trigger is immediate. `triggerNow` always bypasses quantisation. Stop cancels a pending trigger.
- With Loop on and `syncLoopBeats` > 0 the loop restarts every N beats, locked to the beat clock, with a short declick ramp; 0 plays the natural clip length.
- Persisted per clip in the processor state as `syncQuant` and `syncBeats`; missing keys load as 4 and 0. Parameters are append-only.

## 14. Monitor (cue) output

Each recorder track has a MON switch. Monitor means: the record tap selected for the track (raw, processed, group, FX, master, playback or output pair) is summed into a stereo cue pair of the plugin outputs.

- `RecorderSettings.monitorPair` selects the output pair (-1 = off); `monitorGainDb` sets the cue level (-60..+12 dB, default 0).
- Mono tracks feed both sides. The cue is added after panic, rehearsal and bypass handling and hard-limited to +-1.
- A muted or unassigned track is not monitored. A track whose source is the cue pair itself is skipped (no feedback).
- Monitoring does not depend on arming or recording.
- Audio-thread safe: preallocated, ramped gain, no locks.

## 15. Disk guard, splitting and session extras

- Low disk: every second the writer checks free space in the record folder. Below `lowDiskWarnMB` (default 500) the status flag `lowDisk` is set and the UI shows "LOW DISK". Below `lowDiskStopMB` (default 100) the take ends cleanly at the current frame (files finalised, session JSON written), `diskStopped` is set with an error message and the UI shows "DISK FULL: STOPPED". A start is refused when free space is already below the stop threshold. `diskSecondsLeft` estimates the remaining time at the current rate. A removed destination is reported separately as a track error.
- Split on size: `maxFileMegabytes` (0 = off) and the WAV cap in section 2 give the part length in frames from the widest track; all tracks split at the same frame.
- Pre-roll edge cases: the take starts at the earlier of the requested pre-roll and the earliest audio actually captured (no silence is invented). If the memory cap limits the ring, the effective pre-roll is reduced and reported as an error message.
- Marker export: `MarkerExport` writes `<stem>_markers.csv` (frame, seconds, timecode, type, label, scene) and `<stem>_markers.txt` (Audacity labels); `toReaperCsv` produces a REAPER marker CSV. With `exportMarkers` on the files are written when a take ends; the Record page has an "Export..." button for the last take.
- Auto-record on transport: with `autoRecordOnTransport` a take starts when the host transport starts and stops when it stops (only takes started this way are stopped). Polled at 20 Hz, never on the audio thread.
- Session metadata gains `tempoBpm` (host tempo at start, written only when known).

## 16. Acceptance tests

`crowdmike_recording_tests` covers: streaming bit-exactness (forward, reverse, loop wrap), head instant trigger, underrun with a stalled streamer, 2 hour bed memory bound, tempo-sync alignment at 60/120/174 BPM, monitor routing, FLAC 24-bit round trip with sidecar, markers and split, AIFF load, stalled writer, low-disk and disk-full guard, marker export, transport trigger, long-run recording.
