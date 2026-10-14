# CrowdMike feature audit (post-build) - 2026-10-14

Base: branch `claude/brave-ride-ui8c92` at `0b24112` (PR #7), starting from `main` `46d95a2`. The earlier audit scope (first-release state:
trim/polarity/mute/HPF/LPF/limiter/direct routing matrix, simple editor) is superseded by this document.

Status legend: **Complete** = implemented, wired end to end and covered by automated tests; **Impl. (needs host/hardware)** = implemented and
unit-tested, but real device / DAW validation is outstanding; **Partial** = works with the stated limitation; **Missing** = not built.

## Verification evidence

| Command | Result (Linux, g++ 13, Release) |
|---|---|
| `crowdmike_dsp_tests` | 109 tests pass (filters, dynamics, limiter, delay/FX, reverse, noise/width, feedback, analysis, parameter tables) |
| `crowdmike_recording_tests` | 25 tests pass (recorder, playback, bed, soundcheck; also clean under ASan/UBSan/TSan per the module author) |
| `xvfb-run crowdmike_tests` | 49 tests pass (engine, state/migration, scenes, presets, MIDI/OSC, macros, routing, recorder tap integration, all pages rendered at 1100x650/1600x900/2560x1440) |
| Plug-in targets | Standalone, VST3, CLAP build on Linux; the Standalone starts headless without crashing |
| CI | Linux/macOS/Windows build+test matrix; Windows installer + portable zip produced by the workflow (see PR #7) |

Not verified (no hardware / hosts in the build environment): real multichannel audio interfaces, ASIO, any DAW (FL Studio, REAPER, Ableton, Bitwig),
pluginval / Steinberg validator / clap-validator, macOS and Linux desktop runs, hours-long soak tests, acoustic tests.

## Feature matrix

| Area (spec) | Status | Evidence / notes |
|---|---|---|
| Parameters, stable IDs (CONTROL 1) | Complete | `Source/Core/ParamTables.h` X-macro tables; first-release IDs frozen and tested; ~3000 host parameters |
| Input chain: trim, polarity, HPF/LPF 12-48 dB, notch + 4 EQ, phase all-pass, align delay, gate, de-esser, compressor, transient, riding (ENGINE 6, EFFECTS 2-6,14,16,17) | Complete | `Engine/InputChannel`, DSP tests per module |
| Mono/stereo input mode | Complete | linked pairs with linked detectors |
| Groups (8): EQ, comp, width, limiter, delay, fader, sends | Complete | `Engine/GroupChannel` |
| FX: digital delay (single/ping-pong/multitap/sync), echo throw, tape, reverse chunk (triggered/continuous/repeat, 4 s default), noise enhancer | Complete | DSP tests incl. exact reverse order, bounded feedback, deterministic noise cap |
| Width / decorrelation, LF mono, Haas | Complete | `WidthProcessor` (group + master) |
| Master chain EQ(8) -> comp -> width -> limiter | Complete | `Engine/MasterBus` |
| Output stage: trim, mute, polarity, delay, HPF/LPF, lookahead limiter, clamp | Complete | limiter ceiling test at the processor level; constant latency reported to host |
| Output matrix + bus routing + route validator + templates (ROUTING) | Complete | `RoutingState`, `BusMatrix`, `RouteValidator`, 9 wiring templates, tests |
| Route transitions ramped; scene recall ramps | Complete | smoothers with recall-ramp override |
| Feedback-risk scoring (ROUTING 6) | Complete | `DSP/RouteRisk`, `Engine/InputRisk`; advisory |
| Feedback detector + UI + operator-applied notch (ENGINE 8, GUI 11) | Complete (advisory) | tuned on synthetic signals; analysis thread runs only while Diagnostics/Inputs-advanced is open |
| Panic, bypass, rehearsal, global limiter lock | Complete | processor tests (panic <11 ms, bypass is latency-aligned) |
| Latency reporting + estimator | Complete | host latency + status-bar estimate incl. device latency in standalone |
| Recorder: multitrack WAV 16/24/32f, FIFOs, markers, pre/post-roll, split, sidecar JSON, error handling (RECORDING 1-8,13) | Complete | 25 tests incl. stalled writer, disk errors, long run |
| Playback: clips, loop, one-shot, reverse, fades, trim, route, waveform | Partial | memory-resident decode (15 min/clip, 3 GB total); no streaming read-ahead from disk |
| Crowd bed mode | Complete | seeded, seam-tested |
| Virtual soundcheck with safety lock/fade-in | Complete | player + processor/engine switch (global `soundcheck` parameter) |
| Scenes: capture/recall, partial scopes, locks, ramps, next/prev | Complete | tests; MIDI/OSC recalls ignore the GUI scope selection (recall everything) |
| Macros (8), curves, invert, bipolar | Complete | tests |
| MIDI learn (CC/note/pitch bend, toggle/momentary/trigger, scenes, recorder, clips, panic) | Complete (standalone) | plug-ins do not request MIDI (use host automation) |
| OSC receiver, bindable to localhost/interface | Complete (receive-only) | address resolver unit-tested; sockets not exercised in CI |
| Presets with scopes + factory presets (CONTROL 2) | Complete | versioned files, tests |
| State versioning + v1 migration, autosave/crash recovery (CONTROL 9-10) | Complete | migration test; crash recovery logic present in standalone, not automatable in CI |
| Undo/redo (parameters, routing) | Complete | parameter undo is recorded via APVTS timer (slightly deferred) |
| Standalone device management + local bindings + reconnect (IO 1, 8) | Impl. (needs hardware) | `DeviceController`; no ASIO build (needs Steinberg SDK), Windows uses WASAPI/DirectSound |
| Plug-in buses 1-16 channels, bus-aware clears | Partial | one main bus, discrete layouts up to 16 ch; no auxiliary buses; no AU |
| GUI: status bar, nav rail, inspector, performance strip, 10 pages, keyboard shortcuts, high contrast, compact, scale (GUI) | Complete | snapshot tests of every page; fonts do not auto-scale with window size (use UI-scale setting) |
| Accessibility (labels, keyboard, double-click entry, shift fine-adjust, ctrl-click reset) | Complete / screen readers unverified | JUCE accessibility handlers with titles on controls |
| Windows build | Complete | CI job produces installer + portable zip |

## Known gaps and enhancement backlog

| Pri | Item | Dependency / acceptance test |
|---|---|---|
| P1 | DAW + validator runs (pluginval level 5, REAPER/Bitwig/FL, clap-validator) | add pluginval step to CI; no crashes/param-order issues |
| P1 | ASIO on Windows | Steinberg ASIO SDK licence; open the device in the selector and map 8x8 |
| P1 | Real-device tests: 8-in/8-out interface, device removal/reconnect | manual test plan |
| P2 | Streaming read-ahead playback for very long beds | worker-thread ring buffers; 2 h bed stays bounded in memory |
| P2 | Clip tempo sync and track "monitor" are stored but inert | quantised trigger from host tempo; monitor bus |
| P2 | MIDI/OSC scene recall should honour the recall scope | route through `SceneBank::recall(scope)` in `ControlHub` |
| P2 | Group limiter GR and per-send input meters not exposed | extend `EngineMeters` |
| P2 | Soak / memory-growth tests (hours) and max-channel stress on CI hardware | nightly job |
| P3 | AU format, auxiliary plug-in buses | macOS host tests |
| P3 | True-peak limiter, FLAC recording, automatic dynamic notches (all spec "future") | - |
| P3 | Code signing of the Windows installer | certificate |
