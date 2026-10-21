# Recording and Playback

> Record raw inputs, processed inputs, groups, effect returns, master, and output pairs as multitrack files, with markers, pre-roll, and a session file for each take.
> Play back clips and a crowd bed from disk, or replace live inputs with a recorded multitrack for virtual soundcheck.

## Recording

The recorder runs on its own thread. The audio thread only copies samples into lock-free queues, so a slow disk cannot stop the live sound. If the writer falls behind, the recorder marks a dropout, the status shows an error, and the event is logged with an approximate position. Recording carries on if the queue recovers.

### Record tracks

Each track picks a source (a tap) and has its own arm, mute, and monitor switches. You can record up to 32 tracks at once.

| Tap | What it records |
|---|---|
| Raw input | The mic before any processing |
| Processed input | After the input chain |
| Group | Each crowd group |
| FX return | Each effect return |
| Master | The master crowd bus |
| Output pair | What a given output receives |
| Playback | Clips and beds |

### Formats

| Format | Notes |
|---|---|
| WAV 16-bit, 24-bit, 32-bit float | The main formats. The recommended default is 24-bit WAV at the engine sample rate, 48 kHz in most setups. |
| FLAC 24-bit | Lossless, written by the same writer thread. Marker, split, and pre-roll behave as for WAV. |

WAV parts are capped at about 4 GB (the RIFF limit), and the writer starts a new part at that size. FLAC has no such cap.

### Starting and stopping

- **Record** (Ctrl+R, or the Record page) starts and stops a take.
- **Record All** arms every track.
- **Auto-record on transport** starts a take when the host transport starts and stops it when the transport stops. It only stops takes that it started itself.
- **Pre-roll** keeps a rolling memory of the last few seconds (5 to 60 seconds in the spec), so pressing record can include what just happened. The pre-roll is limited by the memory cap. If the cap shortens it, the take reports the shorter pre-roll.
- **Post-roll** keeps recording for a while after you stop.
- **Split** starts a new file at the current frame. The split can also run on size: set a maximum file size in megabytes. All tracks split at the same frame.

### Track names

The default file name pattern is `YYYY-MM-DD_HH-MM-SS_Scene_Source.wav`. Tokens for date, time, scene, source name, show, venue, and take number can be changed. Names are sanitised for the platform.

### Session file

Each take writes a session JSON file next to the audio. It holds the start time, sample rate, channel names, scene name, route snapshot, marker list, file list, app version, and host tempo when it is known. It is written off the audio thread.

### Disk guard

| Free space in the record folder | What happens |
|---|---|
| Below 500 MB (default) | Status shows LOW DISK |
| Below 100 MB (default) | The take stops cleanly at the current frame. Files are closed and the session JSON is written. The status shows DISK FULL: STOPPED. |

The app also refuses to start a take when free space is already below the stop threshold. An estimate of time left is shown.

## Markers

A marker is a labelled point in time. Markers are added with Ctrl+M, from the Record page, or from MIDI and OSC.

| Type | Use it for |
|---|---|
| Big Cheer | A peak moment |
| Chant | A chant or call-and-response |
| Applause | Applause |
| Performer Moment | Something the performer did |
| Problem/Feedback | Feedback, a dropout, or a fault |
| Custom | Anything else |

With **Export markers** on, each take writes two files next to the session: a CSV with frame, time, timecode, type, label, and scene, and an Audacity label file. The code can also produce a REAPER marker CSV. The Record page has an **Export** button for the last take.

Auto markers from the [[Show Report|Show-Report]] (crowd peaks) use the same marker store.

## Monitoring (cue output)

Each track has a **MON** switch. A monitored track sends its tap to a stereo cue pair on the outputs you choose. Use it to listen to a track in the room or on a foldback mix without changing the live outputs.

- Choose the cue output pair in the recorder settings.
- The cue level runs from -60 to +12 dB (default 0 dB).
- Monitoring works without arming the track.
- A muted track is not monitored. A track that feeds the cue pair itself is skipped, so you do not get a loop.

## Playback

The Playback page holds up to 32 clip slots.

| Setting | What it does |
|---|---|
| Loop or one-shot | Repeats or plays once |
| Gain and fades | Level and fade in and out |
| Trim | Start and end points |
| Reverse | Plays the clip backwards from the file. This is separate from the live reverse chunk effect. |
| Route | Sends the clip to a group, the master, or an output |
| Hotkey and MIDI | Triggers by keyboard, MIDI, or OSC (`/crowdmike/clip/<n>/trigger`, `/stop`, `/crowdmike/clips/stop`) |
| Sync | Follows the tempo grid and can loop in whole beats. See [[Beat Sync and Tempo|Beat-Sync-and-Tempo]]. |

Clip files can be WAV, AIFF, FLAC, or any other format JUCE can read. Each clip shows its waveform, duration, and state.

### Long clips and streaming

Short clips are decoded into memory. Clips longer than the resident limit (120 seconds by default), or any clip that would push resident clips over a 1 GB budget, stream from disk. The app keeps the first second in memory and reads the rest from disk in a background thread. A clip can be up to 24 hours long.

If the disk cannot keep up, the voice goes silent for the missing frames, the Playback page shows UNDERRUN for six seconds, and the counters record how often it happened. Loop wraps, trim, and reverse still play without audible seams for streamed clips.

"Streaming" in this page means reading files from disk. The docs do not describe a built-in network stream. For a livestream, route an output pair to your streaming software.

## Crowd bed

A long recording can loop as a quiet crowd bed under the live sound.

- The loop region is randomised, with long crossfades and some level variation.
- Filtering is optional.
- It is seeded, so the same setup gives the same bed.
- Its memory use does not grow with its length.

## Virtual soundcheck

Virtual soundcheck plays a recorded multitrack in place of the live inputs.

- Each recorded track keeps its channel mapping, so it lines up with the original inputs.
- One control switches between live and recorded sources.
- A safety lock and a fade-in stop an accidental blast into the PA when you switch. Check the outputs before you release the lock.
- A loop range and seek let you test one section again and again.
- Playback runs at 1x speed.

Virtual soundcheck works with the [[Performer Ducker|Performer-Ducker]] and [[Line Check|Quick-Start-Line-Check]], so you can set levels and ducking with the recorded show.

## Tips

- Record the raw inputs when you want to fix things later, and the master when you want the mix as the PA heard it.
- Arm only what you need. Each track costs disk space and a little CPU.
- Use markers during the show, not after. A marker placed a few seconds late is still useful, but an exact time is better.
- Check free space before a long take. The disk guard will stop the take if it runs low.
- Give each take a unique name, or let the session file keep takes separate.
