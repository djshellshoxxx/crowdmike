# CrowdMike DSP Engine Specification

## 1. Architecture

The DSP engine is independent from GUI and product wrapper code.

Recommended modules:

```text
Source/
  Core/
    CrowdEngine.*
    EngineConfig.*
    ParameterState.*
    SceneState.*
  DSP/
    InputChannel.*
    CrowdGroup.*
    OutputBus.*
    DelayLine.*
    FilterBank.*
    FeedbackDetector.*
    Compressor.*
    Limiter.*
    DeEsser.*
    ReverseChunk.*
    TapeDelay.*
    NoiseEnhancer.*
    WidthProcessor.*
  Routing/
    RoutingMatrix.*
    RouteValidator.*
  Recording/
    RecorderEngine.*
    PlaybackEngine.*
    AudioFileWorker.*
  Plugin/
    PluginProcessor.*
    PluginEditor.*
  Standalone/
    Main.*
    DeviceController.*
  UI/
    ...
```

## 2. Processing pipeline

Per audio callback:

1. Validate active layout and clear unused output channels.
2. Copy/alias host/device inputs into engine input views.
3. Apply input trim with smoothing.
4. Apply polarity.
5. Apply input safety filters.
6. Apply alignment delay.
7. Apply de-esser/compression/gate as configured.
8. Distribute to groups and pre/post effect sends.
9. Process groups.
10. Process effect returns.
11. Sum master crowd buses.
12. Apply master EQ/dynamics/limiter.
13. Apply output matrix, output delay, polarity, and trim.
14. Hard-safety clamp only as final exceptional protection.
15. Push non-blocking meter/analysis data.
16. Copy record taps into lock-free recorder FIFOs.

## 3. Real-time requirements

The audio thread must not:

- allocate/deallocate heap memory
- perform file access
- take unbounded mutexes
- call logging systems that can block
- parse XML/JSON
- resize vectors
- open devices
- perform DNS/network operations

All buffers and DSP state are prepared in `prepareToPlay`.

Parameter changes use atomics, lock-free snapshots, or JUCE parameter state access with smoothing.

## 4. Parameter smoothing

All gain, pan, filter cutoff, delay mix, send level, and other potentially discontinuous controls require smoothing.

Typical values:

- gain: 10-30 ms
- pan: 10-30 ms
- filter cutoff: 20-100 ms
- wet/dry: 10-30 ms
- scene recall: configurable 10-500 ms

True bypass may remain immediate only where click-free behavior is guaranteed.

## 5. Latency

The engine tracks:

- base host/device block latency
- intentional input alignment delay
- reverse-buffer latency where used in continuous mode
- lookahead limiter latency
- optional linear-phase processing if ever added

Plug-in wrappers report algorithmic latency to the host where required.

Standalone displays estimated end-to-end latency but does not pretend to know acoustic propagation latency unless manually configured.

## 6. Input channel object

Each `InputChannel` owns/prepares:

- smoothed trim
- polarity
- HPF
- LPF
- parametric/notch filters
- delay line
- de-esser
- compressor
- gate/expander
- meter taps
- record taps
- sends

## 7. Crowd groups

Group processing supports:

- N-to-1/N-to-stereo summing
- trim
- balance/pan
- delay
- EQ
- dynamics
- width
- FX sends
- post-group recorder tap

Groups are preallocated at the configured maximum and activated/deactivated without reallocating.

## 8. Feedback detector

Initial detector is advisory.

Algorithm:

- windowed FFT on selected monitoring taps
- identify narrow persistent peaks
- compare peak prominence to neighboring bins
- track persistence over time
- rank candidates
- reject transient musical peaks using persistence and bandwidth heuristics

Output is analysis data only in v1. The UI can suggest notch frequency/Q/depth.

Future mode may automatically insert bounded dynamic notches with explicit enable and maximum attenuation.

## 9. Panic path

Panic is a dedicated atomic state checked every block.

When engaged:

- crowd reinforcement outputs ramp rapidly to silence
- recording can continue
- playback to reinforcement outputs stops
- meters continue
- UI remains responsive

Ramp should be short enough to stop a developing feedback event but not introduce a severe discontinuity.

## 10. Denormals and numeric safety

Use JUCE scoped no-denormals handling.
Sanitize NaN/Inf in debug builds and optionally at subsystem boundaries.
All gain stages define expected headroom.
Internal mix buses should retain ample float headroom before final limiting.

## 11. Threading

Threads:

- real-time audio thread
- GUI/message thread
- recorder writer thread
- playback/file prefetch worker
- optional analysis worker for expensive visual FFT/history aggregation

FFT data sent from audio thread through bounded lock-free FIFOs.

## 12. State ownership

Audio processor owns authoritative parameter/state model.
GUI is a view/controller only.
Recorder/playback transport state uses thread-safe commands.
Routing changes are built off-thread and atomically swapped at safe block boundaries.
