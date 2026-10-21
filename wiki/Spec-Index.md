# Spec Index

> Every document in the repository that describes what CrowdMike should do, how it is built, or where it stands, with one line on each.
> Start with the module index for code work, and the feature audit for status.

## Repository documents

| Document | Read it for |
|---|---|
| [README.md](https://github.com/djshellshoxxx/crowdmike/blob/main/README.md) | The product in brief: goals, the download and build quick start, the high-level signal flow, and the acoustic behaviour rules |
| [AGENTS.md](https://github.com/djshellshoxxx/crowdmike/blob/main/AGENTS.md) | The modular code and safe refactoring policy. Every change follows it. |
| [docs/PRODUCT_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/PRODUCT_SPEC.md) | Product definition, editions, operating modes, scene model, first-release limits, and non-goals |
| [docs/GUI_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/GUI_SPEC.md) | Layout and each page of the interface, the reverse and feedback views, accessibility, and usability additions |
| [docs/ENGINE_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/ENGINE_SPEC.md) | The DSP engine architecture, the per-block pipeline, real-time rules, latency, smoothing, the feedback detector, panic, and threading |
| [docs/IO_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/IO_SPEC.md) | Standalone device I/O, plug-in buses, channel safety, input and output controls, sample rates, buffer sizes, and device-binding persistence |
| [docs/ROUTING_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/ROUTING_SPEC.md) | The routing graph, default wiring, feedback-aware rules, route transitions, wiring templates, channel naming, and the frozen bus-source table |
| [docs/EFFECTS_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/EFFECTS_SPEC.md) | Every effect's purpose and parameters, the effect contract, the effect order, and the FX slot table. Stadium Reverb and Crowd Multiplier are sections 19 and 20. |
| [docs/DUCKER_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/DUCKER_SPEC.md) | The performer ducker: parameters, the detector and gain algorithm, engine placement, recorder rules, lookahead, and acceptance tests |
| [docs/LINE_CHECK_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/LINE_CHECK_SPEC.md) | The 10-second Line Check: probe, analysis, verdicts, trim proposals, apply and undo, quick start, and the panel |
| [docs/BEAT_SYNC_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/BEAT_SYNC_SPEC.md) | Tempo sources, tap tempo, MIDI clock, auto tempo, trigger quantise, nudge and resync, and scene recall scope |
| [docs/RECORDING_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/RECORDING_SPEC.md) | Recording formats, the writer architecture, overrun behaviour, track naming, pre-roll, markers, playback, streaming, tempo-synced clips, crowd bed, monitor, and disk guard |
| [docs/CONTROL_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/CONTROL_SPEC.md) | Parameters, presets, scenes, macros, MIDI and OSC, host automation, undo, autosave, and state versioning |
| [docs/SHOW_REPORT_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/SHOW_REPORT_SPEC.md) | The Crowd Energy Tracker and Show Report: sampling, the peak detector, the report files and schema, export triggers, and the UI |
| [docs/TESTING_SPEC.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/TESTING_SPEC.md) | Test layers, DSP and routing tests, real-time checks, performance targets, host matrix, validators, manual acoustic tests, and the CI and soak layers |
| [docs/IMPLEMENTATION_PLAN.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/IMPLEMENTATION_PLAN.md) | The eleven build phases, their exit criteria, and the milestone list |
| [docs/MODULES.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/MODULES.md) | The module index: layers and their dependencies, DSP module contracts and interfaces, invariants, and the Show Report and recording extras |
| [docs/RELEASE_PROCESS.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/RELEASE_PROCESS.md) | The three workflows, the version rule, the steps to cut a beta, the outputs, re-running, and what is not verified |
| [docs/feature-audit/FEATURE_AUDIT_2026-10-14.md](https://github.com/djshellshoxxx/crowdmike/blob/main/docs/feature-audit/FEATURE_AUDIT_2026-10-14.md) | The status of every feature against the specs, the verification evidence, what was not verified, and the gap backlog |

## Which spec for which question

| Question | Document |
|---|---|
| What is a parameter's range? | `Source/Core/ParamTables.h`, with the feature spec |
| How does a new effect fit? | `EFFECTS_SPEC.md`, then `MODULES.md` |
| Why is the output limited this way? | `ENGINE_SPEC.md` (latency and the limiter) and `PRODUCT_SPEC.md` (safety) |
| What is recorded, and in what format? | `RECORDING_SPEC.md` |
| How does a saved show load? | `CONTROL_SPEC.md` section 10, and the state page |
| What does the build need? | `RELEASE_PROCESS.md`, and the build page |
| Is this feature done? | `FEATURE_AUDIT_2026-10-14.md` |

## Date notes

The feature audit is dated 2026-10-14. Several later specs describe features that the audit still lists as partial or missing, including FLAC recording, streaming playback, clip tempo sync, and recall scope on MIDI and OSC. The [[Roadmap and Known Gaps|Roadmap-and-Known-Gaps]] page lists those differences.
