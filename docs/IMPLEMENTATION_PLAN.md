# CrowdMike Implementation Plan

## Architecture decision

Use JUCE 9 + modern CMake + C++20 where supported by target toolchains.

One shared engine produces:

- Standalone
- VST3
- CLAP

Do not fork separate DSP implementations per format.

## Status (updated for the beta; evidence in docs/feature-audit/)

Legend: **Built** = implemented and covered by automated tests. **Built, needs validation** = implemented and tested, but the
exit criterion needs real hardware or a real DAW (listed as manual validation, not automated). **Out of scope** = moved to
[POST_BETA_SPEC.md](POST_BETA_SPEC.md).

| Phase | Status | Notes |
|---|---|---|
| 0 Repository foundation | Built | CMake + JUCE 9, `ci.yml` on Linux/macOS/Windows, version in three agreed places, docs linked from the README. Formatter / clang-tidy configuration: out of scope for beta (mixed legacy style, no drive-by reformatting per AGENTS.md); the strict JUCE warning flags are the static check. |
| 1 Core audio engine | Built | `Source/Engine`, `Source/DSP`, routing matrix, parameter tables with frozen IDs. |
| 2 Standalone multichannel I/O | Built, needs validation | `DeviceController` (bindings, reconnect, autosave); device-independent parts are unit-tested, the 8-in/8-out interface exit test is manual. ASIO: out of scope (POST_BETA_SPEC.md section 5). |
| 3 Plugin wrappers | Built, needs validation | VST3 + CLAP + Standalone, bus layouts 1 to 16 channels, state, host automation, constant reported latency (tests in `WrapperAuditTest`). Validation in AudioPluginHost / a major VST3 host / a CLAP host is manual; pluginval and clap-validator run optionally in `release-beta`. AU and aux buses: out of scope. |
| 4 Dynamics and cleanup | Built | compressor, de-esser, gate, notch/EQ, master chain, meters, ducker (DUCKER_SPEC). |
| 5 Creative crowd effects | Built | delay, echo, reverse, noise, tape, width, transient, plus Stadium Reverb and Crowd Multiplier (EFFECTS_SPEC sections 19 and 20) and Beat Sync (BEAT_SYNC_SPEC). |
| 6 Recording | Built | WAV 16/24/32f and FLAC 24, markers, pre-roll, split, disk guard, Show Report (SHOW_REPORT_SPEC). |
| 7 Playback and virtual soundcheck | Built | clips, loop, reverse, fades, trim, routing, tempo sync, streaming read-ahead for long clips, crowd bed, soundcheck. |
| 8 Full GUI | Built | all ten pages plus shell; every visible control audited by `UiAuditTest`. |
| 9 Feedback analysis | Built (advisory) | detector, persistence tracking, suggestions, operator-applied notch. Automatic dynamic notches: out of scope (POST_BETA_SPEC.md section 4). |
| 10 Scenes and controllers | Built | scenes with scopes and ramps, macros, MIDI learn, OSC (receive), shortcuts (GUI_SPEC section 15), undo/redo. |
| 11 Hardening | Built, needs validation | soak tests (nightly), stress, device disconnect simulation, disk-full, state migration, crash recovery, accessibility titles, installers, documentation. Real-device and DAW matrix runs, signing and notarization: manual / out of scope. |

Milestones M0 to M7 are met; M8 (Beta) is the release process in [RELEASE_PROCESS.md](RELEASE_PROCESS.md) plus the manual validation list.

## Phase 0 - Repository foundation

Deliverables:

- CMake project
- JUCE dependency strategy
- formatting/static-analysis configuration
- test target
- CI for Windows/macOS/Linux
- basic app/plugin targets
- version header
- docs linked from README

Exit criteria:
all empty-shell targets configure and build.

## Phase 1 - Core audio engine

Implement:

- EngineConfig
- channel/bus abstractions
- InputChannel
- CrowdGroup
- OutputBus
- parameter state
- meter FIFO
- basic routing matrix

Initial DSP:

- trim
- mute/solo
- polarity
- HPF
- LPF
- simple notch
- alignment delay
- output limiter

Exit criteria:
multi-input signals route predictably through unit/integration tests.

## Phase 2 - Standalone multichannel I/O

Implement:

- audio device management
- physical channel assignment
- local device-binding persistence
- sample-rate/buffer settings
- xrun counter
- output matrix

Exit criteria:
at least 8-in/8-out test interface can be configured and routed without a DAW.

## Phase 3 - Plugin wrappers

Implement:

- JUCE AudioProcessor
- bus-layout validation
- VST3 build
- CLAP build
- state save/restore
- host automation
- latency reporting

Exit criteria:
validated in JUCE AudioPluginHost plus at least one major VST3 host and one CLAP host.

## Phase 4 - Dynamics and cleanup

Implement:

- compressor
- de-esser
- gate/expander
- multi-band/parametric notch EQ
- master chain
- meters

Exit criteria:
DSP unit tests pass at all required sample rates.

## Phase 5 - Creative crowd effects

Implement in this order:

1. digital delay
2. echo throw
3. reverse chunk
4. white-noise crowd enhancer
5. tape delay
6. width/decorrelation
7. transient/applause enhancer

Reason:
the first three establish delay-line infrastructure that tape delay and other time effects can reuse.

Exit criteria:
effects are real-time safe and preset/state compatible.

## Phase 6 - Recording

Implement:

- track arming
- lock-free record FIFOs
- recorder worker
- WAV writing
- markers
- pre-roll
- raw/processed/group/master taps
- failure handling

Exit criteria:
long-duration multitrack recording does not block audio.

## Phase 7 - Playback and virtual soundcheck

Implement:

- clip database
- read-ahead
- waveform generation
- one-shot/loop
- reverse clip playback
- routing
- virtual soundcheck

Exit criteria:
recorded multitrack session can replace live inputs safely.

## Phase 8 - Full GUI

Implement in order:

1. global shell/status
2. Live page
3. Inputs page
4. Groups page
5. FX page
6. Matrix page
7. Record page
8. Playback page
9. Scenes
10. Settings
11. Diagnostics

Exit criteria:
all core live operations accessible without opening modal dialogs.

## Phase 9 - Feedback analysis

Implement:

- spectrum analysis FIFO
- persistent peak tracking
- candidate scoring
- feedback UI
- suggested notch application

Out of scope for beta:
bounded automatic dynamic notches (specified in POST_BETA_SPEC.md section 4).

Exit criteria:
detector is useful without creating false confidence or mutating audio unexpectedly.

## Phase 10 - Scenes and controllers

Implement:

- scenes
- safe recall ramps
- macros
- MIDI learn
- OSC standalone
- keyboard shortcuts
- undo/redo

Exit criteria:
performer can run primary CrowdMike actions without mouse interaction.

## Phase 11 - Hardening

- pluginval/validator work
- soak tests
- maximum-channel stress
- device disconnect/reconnect
- disk-full tests
- state migration
- crash recovery
- accessibility
- installer/package
- documentation

## Suggested milestone list

### M0 - Builds
Standalone/VST3/CLAP shells compile.

### M1 - Pass Audio
Multichannel pass-through, gain, filters, delay, routing.

### M2 - Safe Crowd Mixer
Dynamics, limiter, panic, meters.

### M3 - Crowd FX
Delay, echo, reverse, noise, tape, width.

### M4 - Capture
Multitrack record and markers.

### M5 - Playback
Clip deck and virtual soundcheck.

### M6 - Performance UI
Complete live GUI, macros, scenes.

### M7 - Feedback Assistant
Detection and notch suggestions.

### M8 - Beta
Cross-platform/host validation and performance hardening.

## Recommended first coding slice (historical, completed)

The first implementation PR should be intentionally small:

1. bootstrap JUCE 9/CMake
2. create Standalone, VST3, and CLAP targets
3. implement a 1-to-N testable engine skeleton
4. add input trim, polarity, HPF, LPF, and master limiter
5. add basic meters
6. add unit tests
7. prove state save/restore

Then add the routing matrix and multichannel support in the next slice.

This avoids trying to debug plugin formats, device I/O, complex routing, recording, GUI, and advanced DSP simultaneously.
