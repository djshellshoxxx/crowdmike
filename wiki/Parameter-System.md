# Parameter System

> Every automatable setting is one entry in an X-macro table in `Source/Core/ParamTables.h`. Each entry generates both an enum index and a definition, and the IDs are frozen once released.
> Rule one: add new things to the end of a list. Never rename, reorder, or reuse an ID.

## Why it is built this way

- The table is the contract between the UI, host automation, saved state, OSC and MIDI, and the DSP. One list feeds all of them.
- The enum and the definition table come from the same list, so they cannot drift apart. A `static_assert` checks the counts.
- DSP modules receive a plain `const float*` to their own parameters, indexed by the enum, in natural units (dB, Hz, ms, 0 or 1 for a switch, the choice index for a menu).
- Saved state and host automation use IDs, not enum positions. So a reordered enum does not break a saved show, but a renamed ID does.

## The table format

Each module is a macro list with entries of three kinds:

| Constructor | Kind | Arguments |
|---|---|---|
| `F(suffix, name, min, max, default, unit, step, mid)` | Float | `mid` sets a skew, so a value sits at the centre of the control |
| `B(suffix, name, default)` | Bool | Default off unless you say otherwise |
| `C(suffix, name, "A\|B\|C", default)` | Choice | Choices are a `|`-separated list. The stored value is the index. |

A small example from the input compressor:

```cpp
// One entry per parameter. The suffix is part of the frozen ID.
X(P##CompThr, F("compThr", "Comp Threshold", -60.f, 0.f, -18.f, "dB", 0.1f))
X(P##CompOn,  B("compOn", "Comp On"))
```

Blocks used in more than one place (the compressor, the limiter, the EQ band) use one macro that takes a prefix argument, so every copy has the same layout.

## Modules

`CM_DEFINE_MODULE(Name, LIST)` expands one list into a namespace with an enum and a `defs[]` table. The `Mod` enum and the `kInfos` table in `ParamBank.cpp` list the modules in this order. The order is what sets each module's global index range.

| Module (`Mod`) | Definition | Instances | ID prefix |
|---|---|---|---|
| Input | `InputP` | 16 | `input<n>.` |
| Group | `GroupP` | 8 | `group<n>.` |
| Output | `OutputP` | 16 | `out<n>.` |
| Master | `MasterP` | 1 | `master.` |
| Global | `GlobalP` | 1 | none (IDs have no prefix) |
| FxDelay | `DelayP` | 1 | `fx.delay.` |
| FxEcho | `EchoP` | 1 | `fx.echo.` |
| FxTape | `TapeP` | 1 | `fx.tape.` |
| FxReverse | `ReverseP` | 1 | `fx.reverse.` |
| FxNoise | `NoiseP` | 1 | `fx.noise.` |
| Macro | none (special case, one value per macro) | 8 | `macro<n>` |
| FxReverb | `ReverbP` | 1 | `fx.reverb.` |
| FxMulti | `MultiP` | 1 | `fx.multi.` |
| Tempo | `TempoP` | 1 | `tempo.` |

The Ducker parameters are appended to the Global block, and the Tempo block is last. The Macro row has no definition table, so it does not use `CM_DEFINE_MODULE`.

Sub-blocks are contiguous inside larger tables, and helpers give their base index, for example `inputEqBase()` and `groupCompBase()`. Tests check these offsets.

Per-FX send parameters for inputs and groups are not computed as `Fx1 + slot`. Use `inputSendParam(fx)` and `groupSendParam(fx)`, because the later sends sit at the end of the table.

## The parameter bank

`Source/Core/ParamBank.h` declares the `Mod` enum, one entry per module, with `Mod::Count` at the end. `ParamBank.cpp` has a `kInfos` table with the same order, and each row gives the defs, the count, the number of instances, the ID prefix, the name prefix, and whether the instance number is part of the ID.

`ParamBank` turns that into a flat global index:

- `ParamBank::index(mod, instance, enumValue)` gives the global index for one parameter.
- `ParamBank::baseIndex(mod, instance)` gives the first index of a module instance.
- `ParamBank::indexForId("input3.trimDb")` and `idForIndex(i)` convert between the ID and the index.
- `ParamBank::rawAt`, `effectiveAt`, and `parameterAt` read values. `effectiveAt` includes macro and scene effects.

Global indices are used inside the code, but they are not stored. Saved state and OSC use IDs.

## Frozen IDs

- A released ID is permanent. Never rename a suffix.
- Add new parameters at the end of the list for their module. Add new modules just before `Count`, and keep `kInfos` in the same order.
- A choice value is an index. Add new choices at the end of the string.
- Legacy IDs are marked in the code (for example, `trimDb`, `limiterCeilingDb`, and the first-release input IDs). Leave them as they are.

If you need to change behaviour that a saved show depends on, add a new parameter and migrate. See [[State, Scenes and Migration|State-Scenes-and-Migration]].

## Adding a parameter, step by step

1. Find the module's list in `Source/Core/ParamTables.h`. Add the new entry at the end of that list.
2. Pick a suffix that is not already used in the module, and give it a sensible range, default, unit, and skew (`mid`).
3. Build. The `static_assert` on the enum and table counts must pass.
4. Read the value in the DSP. Use the module's enum name, for example `p[params::DelayP::Feedback]`.
5. If the parameter should not be captured by scenes, add it to the transient list in `Source/Core/SceneBank.cpp`. Tap and resync are examples.
6. If it needs a short OSC name, add it to `oscAliasSuffix()` in `Source/Control/OscAddress.cpp`. The full `/crowdmike/param/<id>` address works without an alias.
7. Add the UI control on the page that owns the feature, using the parameter slider, toggle, or choice components, so right-click learn, lock, and undo work.
8. Add a test. `tests/dsp/ParamTablesTest.cpp` checks the table, and a state test should check that an older state without the parameter loads with its default.
9. Update the spec in `docs/` for the feature and `docs/MODULES.md` if the module contract changes.

## Adding a module

Adding a module is the same, plus:

1. Declare the `CM_DEFINE_MODULE` line in `ParamTables.h`.
2. Add the `Mod` enum entry before `Count` in `ParamBank.h`, and the matching `kInfos` row in `ParamBank.cpp`, in the same order.
3. Give it an ID prefix, and decide whether it has numbered instances.
4. Wire it into the engine, and add tests that check the global index layout does not shift for anything that already existed.

## Automation and host notes

Parameters are exposed to hosts with their IDs. The total is about 3000, which is why routing is not automated cell by cell. Large structures, such as the routing matrix and bindings, live in structured state.

## Related

[[Adding a DSP Effect|Adding-a-DSP-Effect]] for a worked checklist. [[Contributing|Contributing]] for the rules on ID changes.
