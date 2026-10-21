# Adding a DSP Effect

> A new send-and-return effect goes in the DSP layer as a real-time-safe class, gets a frozen parameter block, is wired into the FX rack and the routing, gets its UI and presets, and is covered by DSP, wiring, and state tests.
> Stadium Reverb (slot 6) and Crowd Multiplier (slot 7) are the two most recent worked examples.

Read the effect contract in `docs/EFFECTS_SPEC.md` section 1 before you start. It sets the rules every effect must meet.

## Before you write code

- Decide if it is a **send and return** effect (the usual case) or an insert on an input, group, or master. Inserts go in their channel chain. Send-and-return effects go through the FX rack.
- Decide if you need a **new slot**. Slots are frozen. A new one is appended after the last slot, which changes `kNumFx`, the bus rows, and the send parameters. Reusing or moving a slot breaks saved shows.
- Write the spec section first in `docs/EFFECTS_SPEC.md`, with the parameter table, the invariants, and the acceptance tests. The repo rules say spec first.

## Checklist

### 1. DSP class

- Add `Source/DSP/<Name>.h` and `.cpp`, in namespace `crowdmike::dsp`.
- Implement `prepare(sampleRate, maxBlock)` (this may allocate), `reset()` (deterministic), and `process(...) noexcept` (no allocation, locks, or I/O).
- Take the parameter block as a `const float*`, with the same natural units as the table.
- Smooth every parameter that can click. Give bypass a click-free crossfade.
- Sanitise input (non-finite becomes 0), keep the output finite, and bound every feedback path.
- Use `dsp::FxContext` for tempo and transport. Do not read the host yourself.
- Give the effect a bounded cost. Freeze or off should cost almost nothing.
- Make the output block-size independent. Update controls on a fixed sample counter if you need to, so block size does not change the result.

### 2. Parameter block

- Add a `CM_FX_<NAME>(X)` macro in `Source/Core/ParamTables.h`, with an `ID prefix`, such as `fx.<name>.`. Include a `Return` entry (the return level in dB) as the other effects do.
- Add the `CM_DEFINE_MODULE(<Name>P, CM_FX_<NAME>)` line.
- Add a `Mod` value (before `Count`) in `Source/Core/ParamBank.h`, and a matching `kInfos` row in `Source/Core/ParamBank.cpp`.
- Keep every suffix final. Parameters appended later go at the end.

### 3. FX rack

- Add the effect as a member of `FxRack` in `Source/Engine/FxRack.h`, and process it in `FxRack::process` with its input send bus.
- Scale the output by its Return level, as the other slots do.
- Use the tempo context for any sync. If the effect has a trigger, check how beat-quantised triggers are planned (`FxTriggerPlan`).
- Expose any meters or counters the Show Report or the UI needs through the engine meter struct, as relaxed atomics.

### 4. Slot, bus, and send wiring

These are the parts a new slot touches. They are shared with the existing slots, so change them carefully.

- `Source/Core/Limits.h`: `kNumFx` and the appended bus-row constants. New rows are appended after the existing ones.
- `Source/Routing/RoutingState.cpp`: the slot name list (`Delay`, `Echo`, and so on).
- `Source/Core/ParamTables.h`: the input and group send parameters, appended at the end of each table, and read through `inputSendParam(fx)` and `groupSendParam(fx)`.
- Tap map and bus matrix helpers that name FX returns (`busRowForFxReturn`, `busRowFxIndex`).
- Route validation: send and return nodes must still end at the master and the matrix, with no FX-to-FX links.

### 5. UI

- The FX page: a module card, controls, and meters. The rack view reorders on screen only.
- The Matrix page: a new row for the return, and the send columns on inputs and groups.
- The parameter picker and the preset browser: a name, and the factory presets if you ship any.
- Keep every control accessible: labels, keyboard, double-click entry, and right-click learn and lock.

### 6. Presets

- Add factory presets in `Source/Core/PresetManager.cpp`, appended to the list so existing preset names do not change.
- Presets are versioned. Test that an old preset still loads.

### 7. State and scenes

- The new parameters save through `ParamBank` by ID, so no special state code is needed for them.
- Decide if any control is transient (never captured by scenes). Freeze and tail kill are transient for the Stadium Reverb, and the list is in `Source/Core/SceneBank.cpp`.
- Check that `SceneBank::scopeForParam` puts the new module under the FX scope bit.

### 8. Tests

| Test | Where | What it checks |
|---|---|---|
| DSP unit tests | `tests/dsp/<Name>Test.cpp` | Stability, bounded output, reset determinism, block-size independence, rate independence (44.1, 48, 88.2, 96 kHz), and the effect's own acceptance tests |
| Table test | `tests/dsp/ParamTablesTest.cpp` | Table and enum counts, IDs, and ranges |
| Wiring test | `tests/engine/FxNewWiringTest.cpp` (pattern) | The effect is reached from its sends and its return reaches the master and matrix |
| State test | `tests/engine/PersistenceTest.cpp` (pattern) | Round trip, and an older state loads with the new effect at its defaults |
| UI snapshot | `tests/engine/UiRoutingPagesTest.cpp` or `UiSnapshotTest.cpp` (pattern) | The page renders at the test sizes |

Name the tests for the behaviour. Every bug fixed later gets a regression test.

### 9. Docs

- The spec section in `docs/EFFECTS_SPEC.md`, with the table of parameters, the contract, and the presets.
- `docs/MODULES.md` for the interface and invariants.
- The feature audit, if the item moved from missing to implemented.

## Touch points from the Crowd Multiplier change

A search for the Crowd Multiplier names in the source tree finds these files, which is a good map of where a new slot lands:

- `Source/DSP/CrowdMultiplier.*`
- `Source/Core/ParamTables.h`, `ParamBank.h`, `ParamBank.cpp`
- `Source/Engine/FxRack.*`
- `Source/UI/Pages/FxPage.cpp`, `Source/UI/Pages/MatrixPage.cpp`, `Source/UI/ParamPicker.cpp`, `Source/UI/PresetBrowser.cpp`, `Source/UI/Shell.cpp`
- `tests/dsp/CrowdMultiplierTest.cpp`, `tests/dsp/ParamTablesTest.cpp`, `tests/engine/FxNewWiringTest.cpp`

## Common mistakes

- Renaming a suffix, or inserting a parameter in the middle of a list. Both break saved shows.
- Allocating in `process()`, including by growing a vector or building a string.
- Letting feedback reach unity, or making the output depend on the block size.
- Adding an FX-to-FX path, which the validator forbids.
- Forgetting the Return level, so the effect is louder or quieter than the others.
- Writing the UI before the DSP is tested. Test the DSP on its own first.

## Related

[[Parameter System|Parameter-System]] for the table format and the ID rules. [[Architecture|Architecture]] for the pipeline. [[Testing|Testing]] for the test layers.
