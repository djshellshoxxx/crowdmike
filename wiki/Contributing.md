# Contributing

> Every change follows `AGENTS.md`: new code is modular, refactors go in their own pull request, parameter IDs and saved formats are kept, and nothing blocks the audio thread.
> Specs come before code, and tests come before the claim that a change works.

## Before you start

1. Read the spec for the area (see [[Spec Index|Spec-Index]]) and the module layer rules in `docs/MODULES.md`.
2. Look at open branches and pull requests for the same area, so you do not duplicate or fight another change.
3. Check the feature audit for the item's status. See [[Roadmap and Known Gaps|Roadmap-and-Known-Gaps]].
4. If the change touches a shared contract (a parameter ID, a saved format, a bus row, an OSC address, a MIDI binding), read the state and migration page first.

## The rules (AGENTS.md)

1. **Look first.** Before editing, read the active branches and pull requests, the specs, the tests, the build scripts, and the module dependencies. Preserve other people's work, and avoid competing edits.
2. **Modular by default.** Give each module one responsibility, small documented contracts, and clear ownership of its state and resources. Prefer dependency injection and composition. Avoid hidden global state, tight UI and engine coupling, and circular dependencies. Do not make micro-modules just to add files.
3. **Separate the layers.** Keep algorithms, DSP, state, and models apart from GUI, framework, OS, hardware, networking, serialisation, and persistence code. Share domain logic through narrow contracts.
4. **Keep the public contracts.** Do not change public APIs, exports, parameter IDs, preset formats, plug-in identity, automation, CLI behaviour, settings, or persistent data. An incompatible change needs a separate specified, versioned migration and regression tests.
5. **Refactor in a separate pull request.** Characterise the current behaviour with tests first. Then extract the code behind an adapter or facade, and keep the old entry point until callers have moved. Test behaviour, not just that it compiles.
6. **Respect real time.** Real-time audio and MIDI callbacks allow no blocking, no dynamic allocation, no file or network I/O, no logging, no GUI access, and no unsafe locks. Keep execution bounded and check DSP output.
7. **Run the matrix.** Run the focused regression tests, and the full build and test matrix for the platforms you can reach. Do not lower assertions or skip failing builds to claim success. Report anything you could not run (devices, DAW hosts, Windows) as unverified.
8. **Document the module.** Record the contract, dependencies, invariants, error cases, and migration notes in the module index (`docs/MODULES.md`), or add a short index for a new module. Where coverage is weak, write the tests or spec first, and then change the risky code.
9. **Merge only reviewed, passing work.** Use short-lived branches. Do not force-push, and do not silently change an approved method.

The closing line of `AGENTS.md` sums it up: modular design applies to all new work from now on, and converting legacy code happens step by step, with tests, in a way that can be reversed.

## Spec first

For a new feature or a behaviour change:

1. Update or write the spec in `docs/`. Include the parameters, the invariants, the edge cases, and the acceptance tests.
2. Write the tests that the spec describes. They should fail first.
3. Implement the change until the tests pass.
4. Update `docs/MODULES.md` if a module contract changed.
5. Update the feature audit if an item changed status.

## Working with parameters and saved data

- Parameter IDs are permanent. Add new ones at the end of a list. See [[Parameter System|Parameter-System]].
- Saved formats (state, presets, scenes, bus rows, recorder session files) change only with a migration and a test. See [[State, Scenes and Migration|State-Scenes-and-Migration]].
- Golden fixtures are not regenerated to make a test pass.

## Commits and pull requests

- Keep each pull request to one thing. A refactor and a feature go in separate pull requests.
- Say what you tested and where. List any manual or hardware checks you could not do.
- Name the spec section the change follows.

## Code style

- C++20 with JUCE 9, following the existing file layout.
- New DSP modules use the `crowdmike::dsp` namespace (see `docs/MODULES.md`).

## Review checklist

Derived from the rules above. A reviewer checks:

- The change follows the spec and the rules above.
- The real-time rules hold on the audio thread.
- Parameter IDs and saved formats are unchanged, or migrated.
- The tests check behaviour, and nothing was weakened to pass.
- The docs are updated.

## Related

[[Architecture|Architecture]] for the thread and pipeline rules. [[Testing|Testing]] for what to run. [[CI and Release Process|CI-and-Release-Process]] for how a change becomes a beta.
