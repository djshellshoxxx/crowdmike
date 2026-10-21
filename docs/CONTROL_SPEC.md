# CrowdMike State, Preset, Automation, MIDI, and OSC Specification

## 1. Parameter system

Use JUCE AudioProcessorValueTreeState or an equivalent stable parameter model for automatable plug-in parameters.

Parameter IDs are permanent once publicly released.

Categories:

- input parameters
- group parameters
- effect parameters
- master parameters
- output parameters
- macro parameters

Large routing matrices and device bindings should live in structured state rather than exposing every cell as a DAW automation parameter.

## 2. Presets

Preset scopes:

- entire show
- input channel
- group
- effect
- master chain
- routing template

Presets must be versioned.

Migration code must support loading older state.

## 3. Scenes

Scenes are live snapshots designed for a show.

Features:

- next/previous
- direct select
- safe transition ramp
- lock parameters
- partial recall
- optional notes
- hotkey/MIDI/OSC assignment

Recall scope: every recall path (GUI, hotkeys, MIDI, OSC, next/previous) uses `SceneBank::recall(slot, scope, ramp)` with the
show's current recall scope (`ui.recallScope`, bit mask: inputs, groups, fx, master, outputs, routing, names, macros). The
scope can be changed remotely via OSC `/crowdmike/scene/scope <mask 1..255>` or `/crowdmike/scene/scope/<name> [0|1]`
(no argument toggles; `all` selects every scope) and via the MIDI binding target `SceneScopeToggle`. An empty scope is
never accepted. See BEAT_SYNC_SPEC.md section 12.

## 4. Macros

At least 8 global macros.

A macro may control multiple destinations with:

- min/max
- curve
- inversion
- bipolar mapping

Examples:

- Crowd Size
- Excitement
- Space
- Echo
- Reverse
- Broadcast Density
- Dark/Bright
- Safety/Feedback Margin

## 5. MIDI

Support MIDI learn for:

- continuous parameters
- toggles
- momentary triggers
- scenes
- recorder controls
- playback clips
- panic
- tap tempo, resync downbeat and recall-scope toggles (binding targets `TempoTap`, `TempoResync`, `SceneScopeToggle`)

MIDI realtime clock (0xF8/0xFA/0xFB/0xFC) is consumed by the tempo hub when `tempo.source` is MIDI Clock
(BEAT_SYNC_SPEC.md section 5).

Support:

- CC
- note on/off
- pitch bend optionally for macros

MIDI callback must enqueue lightweight commands rather than mutate complex state unsafely.

## 6. OSC

Recommended for standalone.

Address examples:

- `/crowdmike/master/level`
- `/crowdmike/input/1/mute`
- `/crowdmike/fx/reverse/trigger`
- `/crowdmike/scene/3/select`
- `/crowdmike/record/start`
- `/crowdmike/panic`
- `/crowdmike/scene/scope` and `/crowdmike/scene/scope/<inputs|groups|fx|master|outputs|routing|names|macros|all>`
- `/crowdmike/tempo/tap`, `/crowdmike/tempo/resync`, `/crowdmike/tempo/<bpm|source|quantise|nudge|autoSource|barBeats>`

OSC write control should be optional and bindable to localhost or selected interface.

## 7. Host automation

VST3/CLAP:

- expose stable parameters
- support host gestures
- report normalized values correctly
- avoid changing parameter IDs across versions
- save full non-parameter state in plug-in state chunk

## 8. Undo/redo

GUI edits use a central undo manager where practical.

Do not make automated meter data part of undo state.

## 9. Autosave

Standalone:

- periodic crash-recovery snapshot
- save on clean exit
- restore option after crash

Autosave work occurs off audio thread.

## 10. State versioning

Top-level state includes:

- schema version
- app version
- platform
- engine config
- parameter tree
- routing state
- scene collection
- recorder/playback state references

Migrations are explicit and tested.
