# CrowdMike Product Specification

## 1. Product definition

CrowdMike is a live crowd ambience processor and router. It is intended to let a performer or engineer capture an audience with multiple microphones, transform the captured sound, delay and align it, record it, replay it, and distribute it to one or more destinations.

Primary use cases:

- DJ or live act wants the room to sound larger and more energetic in reinforcement, stream, broadcast, or recording.
- Venue wants independent crowd feeds for PA, monitors, broadcast, recording, foyer, livestream, or effects.
- Engineer wants multiple crowd microphones aligned, filtered, dynamically controlled, and grouped without building a large DAW session.
- Performer wants momentary crowd effects such as reverse bursts, echo throws, exaggerated tails, width, or noise-assisted excitement.
- Production wants record/playback of crowd beds, reactions, chants, and ambience.

## 2. Product principles

- Live-first: all important controls reachable in one or two actions.
- Safe by default: conservative gains, output limiting, route validation, no accidental feedback loops.
- Scalable: useful with one microphone but designed for 2-16+ inputs and multiple output buses.
- Format-neutral DSP: core engine shared between standalone, VST3, and CLAP.
- Deterministic: presets and scenes restore exactly, including routing.
- Real-time safe: audio callback performs no blocking work.
- Observable: signal, gain reduction, latency, clipping, feedback-risk, recording, and routing state are visible.
- Recoverable: if a device disappears or a file write fails, audio processing continues where possible.

## 3. Editions/formats

### Standalone
Full device management, hardware input/output assignment, recorder, playback deck, scenes, MIDI/OSC control, and routing matrix.

### VST3
Host-controlled I/O. Multi-bus support where the host allows it. No direct hardware ownership. Recording remains available, but host transport and permissions must be respected.

### CLAP
Same engine and feature set as VST3 where the host exposes equivalent buses and parameter automation.

## 4. Main operating modes

### Reinforce
Live microphones processed and sent to outputs.

### Broadcast
Crowd feed optimized for stream/recording, potentially separate from PA reinforcement.

### Capture
Record raw and processed crowd signals with optional monitoring.

### Playback
Play recorded crowd clips or beds through the processing and output matrix.

### Performance FX
Live macros for throws, reverse chunks, echo, tape slowdown coloration, width, and density.

## 5. Scene model

A Scene stores:

- input names and enabled states
- input gains, delays, polarity, filters, dynamics
- group membership
- effect parameters and sends
- output routing and trims
- recorder routing
- playback deck settings
- macro assignments
- MIDI/OSC assignments
- GUI layout state where useful

Scene recall must support:
- immediate recall
- safe recall with short parameter ramps
- partial recall scopes
- lockable parameters that are excluded from recall

## 6. Non-goals for first release

- automatic acoustic calibration of an entire venue
- guaranteed room-wide active noise cancellation
- replacing a dedicated system DSP/loudspeaker processor
- hosting arbitrary third-party plug-ins inside CrowdMike
- networked audio transport such as Dante/AES67 directly in the DSP core

These can be future integrations.

## 7. Suggested first-release limits

Configurable compile/runtime limits:

- 16 input channels
- 8 crowd groups
- 8 output buses
- 8 effect returns
- 32 playback clips
- 32 simultaneously armed recorder tracks
- up to 192 kHz where hardware/host permits
- internal processing in 32-bit float, optional 64-bit host processing later

## 8. Additional features worth including

- per-input automatic gain riding option with conservative range
- feedback detector display and manual notch suggestion
- 8-band parametric master EQ
- stereo width and decorrelation effect for broadcast/recording buses
- transient emphasis for applause
- density/envelope follower that can drive effect sends
- crowd gate/expander for quiet-stage suppression
- momentary FX buttons suitable for MIDI foot/controller mapping
- panic button that mutes all crowd reinforcement while leaving recording active
- rehearsal mode with no physical output
- virtual soundcheck from previous recordings
- latency estimator and route latency display
- file markers for notable crowd moments
- autosave and crash-recovery state
