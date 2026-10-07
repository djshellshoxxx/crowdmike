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

Optional later:
FLAC.

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
