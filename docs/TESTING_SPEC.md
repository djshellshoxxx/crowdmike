# CrowdMike Testing and Performance Specification

## 1. Test layers

### Unit tests
DSP algorithms and state behavior.

### Engine tests
Multiple modules connected exactly as production engine.

### Plugin validation
VST3 and CLAP host compatibility.

### Standalone integration
Device selection, multichannel routing, recording, playback.

### Manual acoustic tests
Real microphones/speakers in controlled rehearsal environment.

## 2. DSP unit tests

Required tests include:

- polarity inversion is sample-exact
- HPF/LPF expected attenuation
- notch center/depth/Q tolerance
- compressor threshold/ratio behavior
- limiter never exceeds expected ceiling within defined tolerance
- delay time exactness across sample rates
- delay feedback remains bounded
- reverse chunk outputs samples in expected reverse order
- reverse crossfade does not produce invalid samples
- de-esser detector selects expected band
- parameter smoothing contains no discontinuity beyond tolerance
- no NaN/Inf propagation

## 3. Routing tests

- every legal source/destination pair
- illegal cycle rejection
- mute/solo behavior
- pre/post tap behavior
- route transition smoothing
- scene routing recall
- disconnected device mapping
- N-in/M-out cases
- output channels are explicitly cleared when unused

## 4. Real-time safety tests

Debug instrumentation to detect:

- allocation in process block
- long mutex wait
- unexpected file calls
- excessive callback duration

Stress test:

- maximum configured channels
- all effects enabled
- recording maximum tracks
- playback active
- FFT analysis active
- smallest practical audio buffer

## 5. Performance targets

At 48 kHz / 128 samples on a representative modern desktop CPU:

- normal 8-input configuration should use a small fraction of one performance core
- 16-input maximum configuration should retain substantial safety headroom
- no sustained callback deadline misses

Exact CPU thresholds should be established from benchmark hardware rather than invented before profiling.

## 6. Memory tests

- no memory growth during hours-long operation
- recorder FIFO bounded
- waveform caches bounded
- reverse buffers fixed after prepare
- scene switching does not leak
- device reconnect does not leak

## 7. File tests

- disk full
- destination removed
- permission denied
- long recording
- split file boundaries
- invalid/corrupt playback file
- sample-rate mismatch
- many clips
- Unicode filenames

## 8. Host matrix

Windows:
- FL Studio
- REAPER
- Ableton Live
- Bitwig Studio
- Studio One where available

macOS:
- REAPER
- Ableton Live
- Bitwig Studio
- Logic: out of scope for beta (needs the AU format, POST_BETA_SPEC.md section 1)

Linux:
- REAPER
- Bitwig Studio
- other CLAP/VST3 hosts as available

CLAP and VST3 support varies by host; test actual supported bus layouts rather than assuming parity.

## 9. Plugin validators

Use where applicable:

- JUCE AudioPluginHost
- pluginval
- Steinberg VST3 validator
- CLAP validation/testing tools available in the chosen toolchain

CI should run non-GUI validator tests where licensing/platform permits.

## 10. Acoustic/manual tests

Test scenarios:

- one crowd mic near PA
- stereo audience pair
- four distributed microphones
- intentionally high reinforcement gain
- narrow tonal feedback
- applause transients
- whistles/screams
- quiet room
- dense music spill
- PA/broadcast split

Verify:
- panic works
- detector identifies persistent peaks reasonably
- delay behavior is predictable
- limiter protects outputs
- routing changes are click-free
- phase/polarity controls are correctly labeled and understood

## 11. Regression rule

Every fixed DSP/routing/state bug receives a regression test whenever feasible.

## 12. CI layers and soak tests

| Layer | Where | Runs | Contents |
|---|---|---|---|
| Fast gate | `ci.yml` | every PR and push to `main` | `crowdmike_dsp_tests`, `crowdmike_recording_tests`, `crowdmike_tests` via `ctest -LE soak` on Linux, macOS (arm64), Windows |
| Packaging | `release-beta.yml` | manual / `beta-*` tag | compiles and packages plug-ins only; requires a green `ci.yml` for the commit; optional pluginval (strictness 5) and clap-validator on Linux, informational |
| Soak | `nightly.yml` | nightly + manual | `crowdmike_soak_tests`, ctest label `soak` |

Soak tests live in `tests/soak/*.cpp` (CMake target `crowdmike_soak_tests`, `EXCLUDE_FROM_ALL`, same sources as
`crowdmike_tests`; the executable replaces global `operator new/delete` to count allocations). Locally:

```
cmake --build build --target crowdmike_soak_tests
ctest --test-dir build -L soak --output-on-failure
```

Durations are audio-time seconds and can be shortened with environment variables for local runs:
`CROWDMIKE_SOAK_STRESS_SECONDS` (default 120), `CROWDMIKE_SOAK_LONG_SECONDS` (default 600),
`CROWDMIKE_SOAK_DEVICE_CYCLES` (default 300), `CROWDMIKE_SOAK_REC_SECONDS` (default 300).

| ctest name | Test | Assertions |
|---|---|---|
| `Soak_Stress` | `Soak_Stress_16x16_AllFx_48k` / `_96k`: 16 in / 16 out, all five FX sends open on every input and group, input processing and master width on, block sizes cycling 1..2048, parameter automation twice per audio-second, FX triggers | all outputs finite, peak < 4.0, zero audio-thread heap allocations after a 2 s warm-up, faster than real time, 16/16 channels active |
| `Soak_LongRun` | `Soak_LongRun_NoMemoryGrowth`: >= 10 minutes of audio at 48 kHz, 16x16, all FX, scene recalls every 30 s | finite output, zero audio-thread allocations, outstanding heap allocations grow <= 512 and resident set <= 48 MiB between the first and last sample after a 60 s warm-up (RSS on Linux/macOS; Windows uses allocation counters only) |
| `Soak_DeviceDisconnect` | `Soak_DeviceDisconnectReconnectCycles`: 300 stop / re-`prepareToPlay` cycles through mixed rates (44.1-192 kHz), block sizes (64-2048) and layouts (1x2 to 16x16) with state save/restore each cycle | finite output, zero audio-thread allocations, no engine objects accumulating across cycles |
| `Soak_Recorder` | `Soak_Recorder_DiskFullRepeatedly`: 8-track take for 5 minutes of audio on a destination whose files repeatedly hit "disk full", operator `splitFile()` recovery | errors detected and reported, audio side sees zero dropouts, recorder memory (`getAllocatedBytes`) unchanged, recorder usable on a healthy disk afterwards |

Real device removal is simulated at the engine boundary (stream stop, new layout/rate, `prepareToPlay`); it does not
exercise OS audio drivers. These tests must never be added to the `ci.yml` selection.
