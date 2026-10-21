# CrowdMike module index and DSP contracts

This is the module index required by the repo policy. Each module lists its contract, dependencies and invariants.
Parameter IDs are frozen (see `Source/Core/ParamTables.h`).

## Layers

| Layer | Path | May depend on | Notes |
|---|---|---|---|
| Core | `Source/Core` | JUCE core/data_structures | Param tables, limits, state, scenes, presets, macros. No audio processing. |
| DSP | `Source/DSP` | `juce_core`, `juce_audio_basics`, `juce_dsp` (FFT/filters) only | Real-time-safe processors. Built into `crowdmike_dsp_tests`. **No GUI/file/OS code.** |
| Routing | `Source/Routing` | DSP, Core | Route matrices, validation, wiring templates, feedback-risk scoring. |
| Engine | `Source/Engine` | DSP, Routing, Core | `CrowdEngine`: the per-block pipeline (ENGINE_SPEC section 2). Format neutral. |
| Recording | `Source/Recording` | JUCE audio_formats, threads | Recorder FIFOs/worker, playback, soundcheck. |
| Control | `Source/Control` | Core, JUCE osc/midi | Scenes, macros, MIDI learn, OSC, command queue. |
| UI | `Source/UI` | everything above (read-only views) | Components. Never owns audio state. |
| Plugin / Standalone | `Source/Plugin*`, `Source/Standalone` | everything | Thin wrappers. |

## DSP module contract (all of `Source/DSP/*`)

* Namespace `crowdmike::dsp` for new modules (legacy `InputStrip`/`SafetyLimiter` stay in `crowdmike`).
* Lifecycle: `prepare(double sampleRate, int maxBlockSize, ...)` may allocate; `reset()` is deterministic (same input after
  `reset()` => bit-identical output); `process(...)` is `noexcept`, allocation-free, lock-free, and supports any block
  length `1..maxBlockSize` and sample rates 44.1 kHz .. 192 kHz.
* Parameters: `process()` receives `const float* p`, one module instance's parameters indexed by the enum in
  `Source/Core/ParamTables.h` (`CompP`, `GateP`, `DelayP`, ...). Values are in natural units (dB, Hz, ms, 0/1 bool, choice
  index). The module reads `p` every call and smooths internally (`dsp::Smoother` in `DspCommon.h`; gain 10-30 ms,
  cutoff 20-100 ms, wet/dry 10-30 ms). The engine may call with `p == nullptr` only where documented.
* Bypass: when a module's `On`/`Bypass` flag turns it off it crossfades (>= 5 ms) and, once settled, `process()` is a bit-exact
  pass-through that costs almost nothing.
* Numeric safety: non-finite input samples are treated as 0; output is always finite; no denormal stalls.
* Metering: getters are `noexcept` and read relaxed atomics, e.g. `float getGainReductionDb() const noexcept`.
* In-place processing on `float* const* channels, int numChannels, int numSamples` unless a module says otherwise.
* Tests: `tests/dsp/<Module>Test.cpp`, using `tests/TestHarness.h` (`CM_TEST`, `CM_EXPECT`, `CM_EXPECT_NEAR`).
  Cover sample-rates 44.1/48/88.2/96 kHz and block sizes 1,16,64,128,512,2048 where relevant.

## DSP modules

| Module | File | Contract summary |
|---|---|---|
| Smoother, helpers | `DspCommon.h` | Linear ramp smoother with settable ramp, dB helpers. |
| InputStrip (legacy) | `InputStrip.*` | Trim, polarity, mute, HPF/LPF (extended with slopes), peak meter. |
| SafetyLimiter (legacy) | `SafetyLimiter.*` | juce limiter, kept for compatibility. |
| (others) | see below | Added by phase; each file documents invariants at the top. |

### Interfaces to implement (names are the public contract)

```cpp
// Biquad.h
struct BiquadCoeffs { float b0,b1,b2,a1,a2; };      // normalised, a0 == 1
BiquadCoeffs designLowpass(double sr, float hz, float q);   designHighpass(...)
BiquadCoeffs designBell(double sr, float hz, float q, float gainDb);  designNotch(double sr, float hz, float q, float depthDb);
BiquadCoeffs designLowShelf(...), designHighShelf(...), designAllpass(double sr, float hz, float q);
class Biquad { void setCoeffs(const BiquadCoeffs&); void reset(); float process(float) noexcept; };  // transposed DF-II

// SlopeFilter.h : cascaded Butterworth HPF/LPF, slope index 0..3 = 12/24/36/48 dB/oct (2/4/6/8 poles)
class SlopeFilter { void prepare(double sr,int maxBlock,int channels,bool highpass);
  void process(float* const* ch,int nch,int n,bool enabled,float hz,int slopeIndex) noexcept; };  // glides cutoff, crossfades enable

// EqBank.h : N-band parametric EQ; band params use the layout of CM_EQBAND (stride 5: On,Type,Hz,Gain,Q).
class EqBank { void prepare(double sr,int maxBlock,int channels,int maxBands);
  void process(float* const* ch,int nch,int n,const float* bandParams,int numBands) noexcept; };
//   Type Notch => attenuation = -|Gain| dB at Hz with the given Q (dedicated notch mode, depth 0..-24 dB).

// AllpassPhase.h : cascaded 2nd-order allpass (0..4 stages) for phase rotation. process(ch,nch,n,stages,hz).
// Compressor.h : class Compressor { prepare(sr,maxBlock,channels /*1 or 2, linked detector*/);
//   process(float* const* ch,int nch,int n,const float* p /*CompP*/) noexcept; float getGainReductionDb() const noexcept; };
// Gate.h       : class Gate        (GateP)  expander/gate with hysteresis, hold, sidechain HPF/LPF; getGainReductionDb()
// DeEsser.h    : class DeEsser     (DeEssP) split-band (default) or wideband; Listen outputs the detector band; getGainReductionDb()
// TransientEnhancer.h : class TransientEnhancer (TransP)
// Limiter.h    : class Limiter { prepare(sr,maxBlock,channels,float lookaheadMs); int getLatencySamples() const;
//   process(float* const* ch,int nch,int n,const float* p /*LimP*/, bool linkChannels) noexcept; getGainReductionDb(); }
//   lookaheadMs == 0 => zero latency "bus limiter" (instant attack + hard clamp at ceiling). Output never exceeds the ceiling
//   (+0.01 dB) for any input once the engage window has passed; with lookahead the delay is constant even when On==0.
// GainRider.h  : class GainRider { prepare; process(ch,nch,n,bool enabled) } -- slow (seconds) level riding, +/-6 dB max,
//   never boosts below -60 dBFS (freezes) so noise is not pumped.
// ChannelDelay.h : class ChannelDelay { prepare(sr,maxBlock,channels,float maxDelayMs); process(ch,nch,n,float delayMs) }
//   exact integer-sample delay when static; delay changes slew (no clicks); reports nothing, costs nothing at 0 ms.
// Envelope.h   : EnvelopeFollower (peak/RMS, attack/release), DensityFollower (slow crowd-intensity 0..1 estimate).

// ---- time-based effects: send/return. Inputs are the stereo send bus; outputs are the stereo WET return.
struct FxContext { double bpm = 120.0; bool hasTempo = false; double ppqPosition = 0.0; bool playing = false;
                   bool freeze = false; float densityMod = 0.0f; };
// DigitalDelay.h : class DigitalDelay (DelayP)  single / ping-pong / multi-tap, tempo sync (DelayP::Division table below),
//   feedback clamped < 0.95 and bounded for every parameter combination, HPF/LPF in feedback path, stereo offset, diffusion.
//   process(const float* inL,const float* inR,float* outL,float* outR,int n,const float* p,const FxContext&) noexcept
// EchoThrow.h    : class EchoThrow (EchoP) momentary Trigger opens the send for CaptureMs then repeats decay; TailKill fades the loop
// TapeDelay.h    : class TapeDelay (TapeP) wow/flutter/age modulated read, tanh drive, head tone, hiss, 2 heads offset
// ReverseChunk.h : class ReverseChunk (ReverseP)  Triggered / Continuous / Repeat; process(sendL,sendR,preL,preR,outL,outR,n,p,ctx);
//   exposes ReverseChunk::Viz getViz() const noexcept for the GUI (write head, play head, stored region, state, pending, crossfade).
// NoiseEnhancer.h: class NoiseEnhancer (NoiseP)  shaped noise under the crowd; process(sendL,sendR,outL,outR,n,p,ctx); hard output cap -24 dBFS peak
// WidthProcessor.h: class WidthProcessor (WidthP) M/S width + decorrelation + safe Haas + LF mono + mono-compat; stereo in-place
// FeedbackDetector.h : see below
```

`DelayP::Division` index -> beats: `{0.125, 1/6, 0.25, 1/3, 0.375, 0.5, 2/3, 0.75, 1, 4/3, 1.5, 2, 4}`.
`ReverseP::BeatDiv` index -> beats: `{1, 2, 4, 8, 16}`.

### Beat Sync (docs/BEAT_SYNC_SPEC.md)

```cpp
// DSP/BeatTracker.h   : dsp::BeatTracker  prepare(sr) allocates; pushSamples(mono,n) noexcept (audio thread, lock-free FIFO);
//   analyse() (worker thread); BeatEstimate getEstimate() const noexcept (seqlock, any thread); reset()
// DSP/TapTempo.h      : dsp::TapTempo  tap(timeSec) -> accepted; hasBpm(); bpm()  (pure logic, 2.5 s reset, outlier reject)
// DSP/MidiClockTempo.h: dsp::MidiClockTempo  onTick(samplePos)/onStart/onContinue/onStop; bpm(); ppqAt(pos)  (24 ppqn)
// DSP/TriggerQuantiser.h: findGridOffset(...), TriggerQuantiser::process(edge, QuantiseTiming, n) -> Passthrough|None|Fire(offset)
// Control/TempoHub.h  : crowdmike::TempoHub  process(n, TempoParams, HostTempoInfo) noexcept -> TempoState (audio thread, once per block);
//   tap()/resyncDownbeat() message thread; midiClockTick/Start/Continue/Stop audio thread; snapshot() any thread
```

Parameter module `Mod::Tempo` (`tempo.*`, `params::TempoP`) is appended after the macros; `kBarBeatsTable` maps the bar-length choice.

### FeedbackDetector (advisory only)

```cpp
struct FeedbackCandidate { float frequencyHz, prominenceDb, persistenceSec, severity /*0..1*/, suggestedQ, suggestedDepthDb; };
class FeedbackDetector { void prepare(double sr);   // allocates
  void pushSamples(const float* mono,int n) noexcept;          // audio thread, lock-free FIFO, drops oldest on overflow
  void analyse();                                              // worker/message thread: drains FIFO, FFT 4096 Hann 50 % overlap, tracks peaks
  int getCandidates(FeedbackCandidate* out,int maxCount) const;  // ranked, persistence-gated, snapshot copy
  void getSpectrumDb(float* out,int numBins) const;            // downsampled display spectrum
  void reset(); };
```

## Engine invariants

* `CrowdEngine` owns all DSP state; the processor/GUI talk to it through atomics and lock-free queues only.
* Panic is a dedicated atomic checked every block; recorder taps are taken before the panic gain.
* Every output channel passes through the same lookahead limiter, so plug-in latency is a single constant reported to the host.
