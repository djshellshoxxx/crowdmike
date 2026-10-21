# Architecture

> `CrowdEngine` owns all DSP state and runs one pipeline per audio block. The processor and the GUI talk to it only through atomics, lock-free queues, and parameter snapshots.
> The audio thread never allocates, locks, touches files, or logs.

## Threads

| Thread | What runs there | Talks to the others through |
|---|---|---|
| Audio | `CrowdMikeAudioProcessor::processBlock`, the tempo hub, `CrowdEngine::process` | Lock-free FIFOs out, atomic parameter reads and command queues in |
| Message (GUI) | Pages, the processor's 20 Hz timer, the Show Report service (1 Hz sampling), undo and state | Atomics and snapshots from the engine |
| Recorder writer | Drains record FIFOs, writes files, rotates parts, session JSON, disk guard | Bounded FIFOs from the audio thread |
| Playback streamer | "CrowdMike clip streamer" refills rings for streamed clips (about every 2 ms) | Ring buffers, read on the audio thread without I/O |
| Clip loader | Decodes clips, builds waveform sketches, rebuilds heads on trim or reverse | Publishes a new clip for the audio thread to pick up |
| Analysis (feedback) | FFT over the monitoring signal while Diagnostics, advanced Inputs, or the Show Report is active | Lock-free FIFO in, snapshot out |
| Beat analysis | Onset autocorrelation, 25 ms cadence, only while the tempo source is Auto | Lock-free FIFO in, seqlock snapshot out |
| Line Check | Frame analysis on the message thread | Frames from a lock-free ring filled by the probe |

Control input (OSC and MIDI callbacks) enqueues lightweight commands. The audio side drains them at block boundaries, and the message side applies anything that touches a complex state.

## Per-block pipeline

Each audio callback runs these steps, in order. The numbers follow `docs/ENGINE_SPEC.md`, with the later additions.

1. Validate the active layout and clear output channels the engine does not write.
2. Take the host or device input into engine views. Line Check's probe reads raw input here, after soundcheck substitution.
3. Input trim, smoothed.
4. Polarity.
5. Input safety filters: HPF, LPF, notch, four-band EQ, phase all-pass.
6. Alignment delay.
7. Gate, de-esser, compressor, transient, gain riding.
8. Send to groups, and take the pre-group and FX send buses.
9. Process the crowd groups: EQ, compressor, limiter, width, delay, and sends.
10. Process the FX rack: seven parallel slots, each with its own return gain. The rack takes the tempo context and the beat-quantised trigger plan.
11. Performer ducker, if on: copies the group and FX buses, computes the gain curve, and applies it to the copies. Recorder group and FX taps are taken before this step.
12. Sum the master crowd bus: EQ, compressor, width, limiter.
13. Output matrix: each output gets its bus mix, trim, mute, polarity, delay, filters, and the output limiter. Then the panic gain applies.
14. Final hard clamp. This is a last-resort guard only.
15. Post meters and analysis data through lock-free paths.
16. Copy record taps into the recorder FIFOs. Raw and processed taps come before panic and rehearsal, so recording survives both.

The plug-in reports one constant latency, the lookahead of the output limiter. Bypass and panic keep that latency aligned.

## Processor flow

`CrowdMikeAudioProcessor::processBlock` does three things before the engine:

1. Read the tempo parameters and host playhead, then run the tempo hub for this block. The hub returns one tempo state (BPM, position, playing) that every tempo-synced effect reads.
2. Set the playback tempo for clips.
3. Call `CrowdEngine::process` with the buffer and the block info.

Clip triggers and echo or reverse triggers that are beat-quantised are planned for the exact sample on the grid, and the FX rack splits the block at that sample.

## Real-time rules

Required on the audio thread:

- no heap allocation or deallocation, no vector resizing,
- no file or network I/O,
- no unbounded mutexes, no blocking,
- no logging that can block,
- no GUI access,
- deterministic, bounded run time per block,
- finite output for any input, including NaN and infinity.

How the code keeps these:

- All buffers and DSP state are sized in `prepare()`.
- Parameter values come from `ParamBank` snapshots, which are read with relaxed atomics.
- Meters are relaxed atomics, written by the audio thread and read by the GUI.
- Recorder and streamer work runs on their own threads, with FIFOs in between.
- Routing changes are built on the message thread and swapped in at a block boundary.
- Denormals are handled with JUCE's scoped no-denormal guard, and the DSP guards its own feedback paths.

## Engine invariants

- `CrowdEngine` owns all DSP state. The processor and GUI talk to it through atomics and queues.
- Panic is a dedicated atomic, checked every block. Recorder taps are taken before the panic gain.
- Every output channel passes through the same lookahead limiter, so plug-in latency is one constant value.
- Routing and scene recalls ramp. A route never switches hard unless panic is on.

## Where state lives

| State | Owner |
|---|---|
| Parameters | `ParamBank` (the authoritative values, saved by ID) |
| Show data that is not a parameter (routing, bindings, macros, OSC and MIDI settings, report settings) | `ShowModel`, a message-thread object |
| DSP state | `CrowdEngine` and the modules under it |
| Recording and playback state | `RecorderEngine`, `PlaybackEngine` |
| Undo | The processor's `UndoManager`, for parameter and routing edits |

## Design rules that matter in practice

- The GUI is a view and a controller. It never owns audio state.
- Code in `Source/DSP` must not include GUI, file, or OS headers.
- Feature code and refactors go in separate pull requests. See [[Contributing|Contributing]].

## Reading the code

Start with `Source/Engine/CrowdEngine.h`, then `Source/Engine/FxRack.h` (the seven send and return effects), `Source/Engine/BusMatrix.*` (the output matrix), and `Source/Control/ControlHub.*` (MIDI and OSC routing into the engine).
