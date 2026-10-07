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
- Logic for non-VST formats only if AU is added later

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
