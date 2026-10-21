# State, Scenes and Migration

> A show is one XML state tree, schema version 2, saved as a `.crowdshow` file or inside the plug-in state chunk. Parameters are saved by their frozen IDs, and everything else is saved as explicit properties with defaults for anything missing.
> Older states load because every change is additive and every migration is explicit and tested.

## What a state contains

The root element is `CrowdMikeState`, with a `schemaVersion` attribute. The state holds:

- the schema version and the app version,
- the platform,
- the engine configuration,
- the parameter tree (every parameter, by ID),
- routing state (matrices, bus rows, templates applied),
- the show model (names, macros, MIDI and OSC bindings, recorder settings, playback settings, report settings, OSC and tempo settings),
- the scene collection,
- recorder and playback references (clip files, bed and soundcheck references).

A `.crowdshow` file is the same processor state written to disk. Use it to move a whole show between computers or to keep a backup. Device bindings are part of the state, but they resolve against the machine that loads the show, so a show from another computer may show unbound devices until you rebind them.

## Schema version

`kSchemaVersion` is 2 in `Source/Core/StateIO.h`. Loading works like this:

1. If the root tag is not `CrowdMikeState`, the load fails.
2. If the saved `schemaVersion` is newer than this build supports, the load is refused with `NewerThanSupported`. The state is not changed.
3. Otherwise the state is migrated step by step to the current version, and the version attribute is updated.

The current migration is `migrateV1ToV2`, which runs on version 1 states and then sets the version to 2.

## Additive rules

These rules keep old shows working.

- Parameter IDs are permanent. A removed ID is still reserved.
- New parameters are appended, and a missing parameter loads at its default. Every other value stays as it was.
- New show data lives in new child elements or properties, and a missing child means its defaults. Examples:
  - The show report settings: a missing `Report` child loads the defaults.
  - Tempo parameters: a state saved before Beat Sync loads with the defaults, and every earlier value stays the same.
  - Ducker parameters: a state saved before the ducker loads with the ducker off.
  - Clip sync: a clip without `syncQuant` or `syncBeats` loads with 4 (1 bar) and 0 (natural length).
- Older builds ignore properties they do not know. For example, the second FX-return bus rows are in `busFx2`. A missing `busFx2` loads as silent rows, so a 5-FX show loads the same way.
- Bus rows are never renumbered. New rows are appended.
- Appended FX send parameters are placed after every existing parameter, so their index does not move.

When a change cannot be additive, it needs a new schema version, a migration function, and a regression test. Do not do this lightly.

## Golden fixtures

The fixture `tests/golden/state_5fx_v2.xml` is a real schema 2 state saved by the five-effect build. The test `golden_state_5fx_loads_identically` in `tests/engine/GoldenStateTest.cpp` loads it into the current build and checks three things:

- every scenario value, by its frozen ID,
- the new FX slots and their sends start at their defaults (off),
- the routing rows keep their meaning, and the new FX rows are silent.

The fixture was generated once, on the pre-change build, and must never be regenerated from a newer build. The generator test only runs when `CM_WRITE_GOLDEN=1` is set.

Files under `tests/golden/` and `tests/engine/golden/` are marked `-text` in `.gitattributes`, so git never rewrites their line endings. Keep it that way, or the byte comparison breaks on Windows.

If a fixture must change because the format really changed, that is a schema change. Update it in the same change as the migration, and explain it in the pull request.

## Scenes

A scene stores the show's parameters and routing for a set of scopes.

- The collection has 32 slots (`kNumScenes`).
- Recall goes through `SceneBank::recall(slot, scope, ramp)`. Every recall path uses the show's current recall scope: GUI, hotkeys, MIDI, OSC, and next and previous.
- The recall scope has eight bits: inputs, groups, FX, master, outputs, routing, names, and macros. An empty scope is refused.
- Some parameters are transient and never captured: tempo tap, tempo resync, reverb freeze, and reverb tail kill. The list is in `Source/Core/SceneBank.cpp`.
- Locked parameters are skipped on recall.
- Recall uses a ramp, so gains and routes glide.

Each parameter belongs to a scope bit through `SceneBank::scopeForParam`. If you add a module, check that mapping.

## Presets

Presets save part of the chain. Each has a scope (show, input, group, effect, master chain, or routing template) and a version. Factory presets are listed in `Source/Core/PresetManager.cpp`. Add new ones at the end of the list.

## Autosave and crash recovery (standalone)

- The standalone app writes a periodic crash-recovery snapshot, and saves on a clean exit.
- After a crash, it can offer to restore the last snapshot.
- Autosave work runs off the audio thread.

Autosave and crash recovery are standalone features. In a plug-in, the host saves the state with the project.

## Checklist for a state change

1. Is it a new parameter? Add it to the end of its table. Done, unless it needs a non-default value.
2. Is it new show data? Add a child or property with a default for when it is missing.
3. Does it change an existing meaning, a row index, or an ID? Stop. This needs a schema version bump, a migration, and tests.
4. Add a test: an old state loads and the new value is the default.
5. Check scenes. Should the new value be captured? Is it transient?
6. Check the golden fixture still passes, or update it with the migration.

## Related

[[Parameter System|Parameter-System]] for IDs and the table format. [[Testing|Testing]] for the state and golden tests. [[Scenes, Presets and Macros|Scenes-Presets-and-Macros]] for the user view.
