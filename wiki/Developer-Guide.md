# Developer Guide

> CrowdMike is a C++20 JUCE 9 project built with CMake. One engine in `Source/Engine` runs inside the Standalone, VST3, and CLAP wrappers, and the DSP in `Source/DSP` is kept free of GUI, file, and OS code.
> Read the module layers and the repo rules before you change anything.

## Repository layout

| Path | Contents |
|---|---|
| `Source/Core` | Parameter tables, parameter bank, limits, state I/O (schema version 2), scenes, presets, macros, the show model |
| `Source/DSP` | Real-time-safe processors: filters, dynamics, delay and FX, reverb, beat tracking, loudness, ducker |
| `Source/Routing` | Routing state, bus matrix rules, route validation, wiring templates, feedback-risk scoring |
| `Source/Engine` | `CrowdEngine` and the per-block pipeline, input and group channels, FX rack, master bus, line check, analysis |
| `Source/Recording` | Recorder, playback, streaming, soundcheck, monitor bus, marker export, session metadata |
| `Source/Report` | Crowd Energy Tracker, peak detector, report writers, the Show Report service |
| `Source/Control` | Control hub, MIDI and OSC handling, OSC address resolver, tempo hub |
| `Source/UI` | Components and pages. Read-only views of the engine. They never own audio state. |
| `Source/PluginProcessor.*`, `Source/PluginEditor.*` | JUCE plug-in wrapper and editor |
| `Source/Standalone` | Standalone application, device controller |
| `tests/dsp`, `tests/engine`, `tests/recording` | Unit and integration tests (see [[Testing|Testing]]) |
| `tests/soak` | Long-running soak tests, excluded from normal builds |
| `tests/golden`, `tests/engine/golden` | Byte-exact fixtures |
| `docs/` | Specifications, the module index, the release process, and the feature audit |
| `installer/` | Inno Setup script, per-platform READMEs, Linux `install.sh` |
| `shared/scripts/` | Configure, packaging, and release scripts |
| `.github/` | CI, nightly soak, and release workflows and the setup action |

## Layers

The module index in `docs/MODULES.md` sets which layer may depend on which.

| Layer | Path | May depend on | Notes |
|---|---|---|---|
| Core | `Source/Core` | JUCE core and data structures | Parameter tables, limits, state, scenes, presets, macros. No audio processing. |
| DSP | `Source/DSP` | `juce_core`, `juce_audio_basics`, `juce_dsp` only | Real-time-safe processors. Built into `crowdmike_dsp_tests`. No GUI, file, or OS code. |
| Routing | `Source/Routing` | DSP, Core | Route matrices, validation, wiring templates, feedback-risk scoring |
| Engine | `Source/Engine` | DSP, Routing, Core | `CrowdEngine`: the per-block pipeline. Format neutral. |
| Recording | `Source/Recording` | JUCE audio formats, threads | Recorder FIFOs and worker, playback, soundcheck |
| Report | `Source/Report` | Core, Engine meters, Recording marker API, JUCE core | Model and writers are pure. `ShowReportService` is the message-thread adapter. |
| Control | `Source/Control` | Core, JUCE OSC and MIDI | Scenes, macros, MIDI learn, OSC, command queue |
| UI | `Source/UI` | Everything above, read-only | Components. Never owns audio state. |
| Plugin and Standalone | `Source/Plugin*`, `Source/Standalone` | Everything | Thin wrappers |

## Targets

| Target | Built from | Purpose |
|---|---|---|
| `CrowdMike_Standalone` | `juce_add_plugin` (Standalone format) | The standalone application |
| `CrowdMike_VST3` | `juce_add_plugin` (VST3 format) | VST3 plug-in |
| `CrowdMike_CLAP` | `clap_juce_extensions_plugin` | CLAP plug-in |
| `crowdmike_dsp_tests` | `tests/dsp` plus DSP sources | Light DSP unit tests |
| `crowdmike_recording_tests` | `tests/recording` plus recording code | Recorder and playback tests |
| `crowdmike_tests` | `tests/engine` plus the app sources it links | Engine, processor, state, and UI snapshot tests |
| `crowdmike_soak_tests` | `tests/soak` | Long-run tests. Not built by default. |

## Where to start reading

1. `docs/MODULES.md`, then the specification for the area you are changing (see [[Spec Index|Spec-Index]]).
2. `Source/Core/ParamTables.h` for every parameter and its range.
3. `Source/Engine/CrowdEngine.*` for the per-block pipeline.
4. `AGENTS.md` for the rules that every change follows (see [[Contributing|Contributing]]).

## Next steps

- [[Building from Source|Building-from-Source]]
- [[Architecture|Architecture]]
- [[Parameter System|Parameter-System]]
- [[Adding a DSP Effect|Adding-a-DSP-Effect]]
- [[State, Scenes and Migration|State-Scenes-and-Migration]]
- [[Testing|Testing]]
- [[CI and Release Process|CI-and-Release-Process]]
