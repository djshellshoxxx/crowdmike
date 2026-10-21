# Testing

> Three self-registering test executables cover the DSP, the engine and UI, and recording. A small harness runs them under CTest, and soak tests run the long cases on a separate nightly job.
> A test that fails is a real failure. Fix the code or the spec, not the assertion.

## Layers

| Layer | Where | What it covers |
|---|---|---|
| Unit (DSP) | `tests/dsp` | Algorithms and state behaviour, one module at a time |
| Engine | `tests/engine` | Modules connected the way the production engine connects them, state, scenes, MIDI and OSC, and UI snapshots |
| Recording | `tests/recording` | Recorder, playback, streaming, soundcheck, monitor, and the disk guard |
| Soak | `tests/soak` | Long runs, device-cycle simulation, allocation counts, memory growth |
| Plug-in validation | CI release workflow | pluginval and clap-validator on Linux, optional and informational |
| Manual | Rehearsal rooms and host machines | Real microphones and speakers, real interfaces, DAW hosts |

## The executables

| Target | Source | Contents at the 2026-10-14 audit |
|---|---|---|
| `crowdmike_dsp_tests` | `tests/dsp` | 109 tests |
| `crowdmike_recording_tests` | `tests/recording` | 25 tests |
| `crowdmike_tests` | `tests/engine` | 49 tests, run under xvfb on Linux (UI pages render at 1100x650, 1600x900, and 2560x1440) |
| `crowdmike_soak_tests` | `tests/soak` | Soak cases, not built by default |

Counts change as tests are added. Check the current tests in the repository.

## The harness

`tests/TestHarness.h` is a small self-registering harness with no external dependency.

| Macro | Use |
|---|---|
| `CM_TEST(name)` | Defines and registers a test function |
| `CM_EXPECT(cond)` | Records a failure if `cond` is false |
| `CM_EXPECT_NEAR(a, b, tol)` | Records a failure if `|a - b| > tol` |

A failure prints the test name, the file, the line, and the expression. The executable returns non-zero if any check failed.

Run one test, or a group, by passing a name fragment:

```bash
# Run every test whose name contains "ducker"
./build/crowdmike_dsp_tests ducker
```

The filter is a substring match on the test name, so name tests after the behaviour they check.

## What the DSP tests cover

The module contract in `docs/MODULES.md` sets the baseline. Each DSP module's tests check:

- **Sample rates**: 44.1, 48, 88.2, and 96 kHz, and 192 kHz where it matters.
- **Block sizes**: 1, 16, 64, 128, 512, and 2048 samples, and a result that does not depend on them.
- **Reset**: the same input after `reset()` gives identical output.
- **Bypass**: a settled bypass is an exact pass-through.
- **Numeric safety**: non-finite input is treated as zero, and output stays finite.
- **Parameter extremes**: the output stays bounded at the top and bottom of every range.

Specific checks include exact polarity inversion, filter attenuation and slope, notch centre and depth, compressor threshold and ratio behaviour, limiter ceilings, delay time exactness, feedback that stays bounded, exact reverse order for reverse chunks, and de-esser band selection. The spec for each effect lists its own acceptance tests.

## Engine and UI tests

- The engine tests run the real `CrowdMikeAudioProcessor` and `CrowdEngine`, so they catch wiring mistakes that DSP tests cannot.
- The UI tests build the pages with xvfb, and check that controls exist, that edits reach the model, and that the rendering does not fail.
- The golden test loads a state saved by an older build and checks it loads unchanged. See [[State, Scenes and Migration|State-Scenes-and-Migration]].

## Golden files

Some tests compare output with a stored file, byte for byte:

- `tests/golden/` and `tests/engine/golden/` hold fixtures.
- `.gitattributes` marks them `-text`, so git never changes their line endings. This matters on Windows.
- A fixture is only written by its generator test, and only when that test's environment variable is set. The state fixture is generated once, on the build before the change it guards. Never regenerate a fixture from a newer build to make a test pass.

## Soak tests

Soak tests live in `tests/soak`. They are a separate CMake target, `EXCLUDE_FROM_ALL`, with a CTest label `soak`. The fast gate excludes them with `-LE soak`, and the nightly workflow runs them.

```bash
# Build and run the soak tests locally
cmake --build build --target crowdmike_soak_tests
ctest --test-dir build -L soak --output-on-failure
```

Durations are in audio seconds, and each one has an environment variable for a short local run:

| Variable | Default | Controls |
|---|---|---|
| `CROWDMIKE_SOAK_STRESS_SECONDS` | 120 | Length of the stress runs |
| `CROWDMIKE_SOAK_LONG_SECONDS` | 600 | Length of the memory-growth run |
| `CROWDMIKE_SOAK_DEVICE_CYCLES` | 300 | Stop and re-prepare cycles in the device test |
| `CROWDMIKE_SOAK_REC_SECONDS` | 300 | Length of the recorder disk-full run |

The soak cases and what they assert:

| CTest name | What it runs | What it asserts |
|---|---|---|
| `Soak_Stress` | 16 in, 16 out, all FX on, block sizes 1 to 2048, automation twice per audio second, FX triggers, at 48 and 96 kHz | Output finite, peak below 4.0, no audio-thread heap allocation after a 2 s warm-up, faster than real time, all 16 channels active |
| `Soak_LongRun` | At least 10 minutes at 48 kHz, 16 by 16, all FX, a scene recall every 30 s | Output finite, no audio-thread allocation, outstanding allocations grow by no more than 512, resident memory grows by no more than 48 MiB (Linux and macOS) |
| `Soak_DeviceDisconnect` | 300 stop and re-prepare cycles through mixed rates, block sizes, and layouts from 1 by 2 to 16 by 16, with state save and restore each time | Output finite, no audio-thread allocation, no engine objects left behind across cycles |
| `Soak_Recorder` | 8 tracks for 5 minutes on a destination that repeatedly fills up, with split recovery | Errors reported, no audio-side dropouts, recorder memory unchanged, normal recording afterwards |

Device removal is simulated at the engine boundary. The soak tests do not use OS audio drivers. Keep them out of the fast gate.

## Real-time safety

The soak tests count allocations on the audio thread, which is how an allocation inside `process()` gets caught. The testing spec also asks for debug instrumentation that flags long mutex waits, unexpected file calls, and slow callbacks. Check the code before you assume that instrumentation is in place. See [[Roadmap and Known Gaps|Roadmap-and-Known-Gaps]].

## Manual and host testing

Some checks cannot run in CI:

- Real microphones and speakers in a rehearsal room (one mic near the PA, a stereo pair, four distributed mics, high reinforcement gain, narrow tonal feedback, applause transients, whistles, quiet rooms, dense music spill, PA and broadcast split).
- Real audio interfaces, including 8 in and 8 out, and device removal and reconnect.
- DAW hosts (see [[Plugin Hosts|Plugin-Hosts]]).
- macOS and Windows checks of the plug-ins, and the Windows installer.

Record the results in the pull request or the release notes, and call out anything you did not check.

## Rules

- Every fixed DSP, routing, or state bug gets a regression test, where that is feasible.
- Tests check behaviour, not only that the code compiles.
- Do not lower an assertion, skip a failing test, or hide a failing build to get a green result. If you think a test is wrong, fix the test with a reason in the change.
- Do not add soak tests to the fast gate.

## Related

[[CI and Release Process|CI-and-Release-Process]] for where each test runs. [[Contributing|Contributing]] for the rules that apply to every change.
