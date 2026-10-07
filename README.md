# CrowdMike

CrowdMike is a JUCE-based live crowd reinforcement, transformation, recording, and playback system for performers, DJs, venues, broadcast, and live production.

The product accepts multiple audience microphones and other audio inputs, delays and processes them, mixes them into one or more crowd buses, and routes those buses to multiple outputs. The goal is to make audience ambience controllable, larger, wider, more energetic, and recordable without forcing an operator to build a complicated DAW session.

## Planned formats

- Standalone application
- VST3 audio effect
- CLAP audio effect
- Windows, macOS, and Linux where supported by JUCE 9 and the selected audio backends

The project uses JUCE 9 with CMake. JUCE 9 is preferred because it provides native CLAP authoring alongside the other plug-in formats.

## Core product goals

1. Multi-input crowd capture with per-input gain, polarity, delay, mute, solo, filtering, dynamics, and metering.
2. Flexible internal routing to crowd groups, effect returns, recorder buses, and multiple physical or host outputs.
3. Feedback-aware operation. Delay is available, but the design does not assume delay alone prevents feedback.
4. Crowd enhancement effects including digital delay, echo, tape delay, filtered noise, width/space processing, reverse-chunk playback, compression, limiting, de-essing, filtering, and notch filtering.
5. Record and playback of raw inputs, processed groups, and master outputs.
6. A live-performance GUI designed for fast operation under stage conditions.
7. Identical DSP behavior across Standalone, VST3, and CLAP wherever host I/O capabilities allow it.
8. Real-time safe DSP: no allocation, file I/O, locks, or blocking work on the audio callback.
9. Presets/scenes that can snapshot a complete show configuration and recall it safely.
10. Fail-safe bypass, clipping protection, routing validation, and clear visual indication of risky gain/feedback conditions.

## Important acoustic behavior

CrowdMike deliberately separates **creative delay** from **feedback management**. Delaying a microphone signal changes the acoustic loop but does not guarantee that feedback cannot occur. Feedback management therefore includes input/output gain discipline, high-pass/low-pass filtering, parametric notches, optional feedback detection, limiter protection, route validation, and operator-visible warnings.

The polarity/phase controls are useful for correcting wiring polarity, aligning microphone groups, creative comb filtering, and reducing some correlated energy in controlled signal paths. They are not specified as a way to silence a diffuse crowd throughout a room, because acoustic cancellation varies with position, path length, frequency, and time.

## Specification index

- [Product and UX specification](docs/PRODUCT_SPEC.md)
- [GUI specification](docs/GUI_SPEC.md)
- [DSP engine specification](docs/ENGINE_SPEC.md)
- [Routing and wiring specification](docs/ROUTING_SPEC.md)
- [Input/output specification](docs/IO_SPEC.md)
- [Effects specification](docs/EFFECTS_SPEC.md)
- [Recording and playback specification](docs/RECORDING_SPEC.md)
- [State, presets, automation, and MIDI/OSC specification](docs/CONTROL_SPEC.md)
- [Testing and performance specification](docs/TESTING_SPEC.md)
- [Implementation roadmap](docs/IMPLEMENTATION_PLAN.md)

## High-level signal flow

```text
Physical/Host Inputs
        |
        v
+--------------------+
| Input Safety       |
| trim / polarity    |
| HPF / LPF / notch  |
+--------------------+
        |
        v
+--------------------+
| Input Delay/Align  |
+--------------------+
        |
        +---------------------> Raw Recorder
        |
        v
+--------------------+
| Input Dynamics     |
| de-ess / comp      |
+--------------------+
        |
        v
+--------------------+
| Crowd Group Mixer  |-----> Group Recorder
+--------------------+
        |
        +----> Delay / Echo
        +----> Tape Delay
        +----> Reverse Chunks
        +----> Noise Enhancer
        +----> Space / Width
        |
        v
+--------------------+
| Master Crowd Bus   |
| EQ / comp / limit  |
+--------------------+
        |
        +---------------------> Master Recorder
        |
        v
+--------------------+
| Output Matrix      |
| trim / delay       |
| polarity / limit   |
+--------------------+
        |
        v
Physical/Host Outputs
```

## Initial implementation strategy

Build the DSP core first as format-independent C++ classes. Wrap that engine in one JUCE `AudioProcessor` for VST3/CLAP and one standalone shell that owns the audio-device configuration. The standalone version exposes physical interface routing; plug-in versions expose the buses and channels the host provides.

The implementation order is defined in [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md).
