# CrowdMike Effects Specification

## 1. General effect contract

Every effect must provide:

- bypass with click-free transition
- wet/dry where applicable
- input/output gain where useful
- parameter smoothing
- deterministic reset
- prepare/reset/process interface
- serialization of all user-facing parameters
- bounded CPU and memory use
- no allocation or blocking on the real-time thread

Effects can be instantiated on inputs, groups, returns, or master buses depending on type.

## 2. High-pass filter

Purpose:

- remove sub-bass, stage rumble, HVAC noise, handling noise, and low-frequency acoustic loop energy

Parameters:

- cutoff: 20-500 Hz
- slope: 12/24/36/48 dB/oct
- resonance/Q where appropriate
- bypass

Default crowd-mic starting point: approximately 80-120 Hz depending on venue.

Implementation:
JUCE IIR/TPT filters or equivalent stable topology.

## 3. Low-pass filter

Purpose:

- remove excessive high-frequency spill, hiss, harshness, and high-frequency acoustic loop energy

Parameters:

- cutoff: 2-20 kHz
- slope: 12/24/36/48 dB/oct
- resonance/Q where appropriate

## 4. Parametric/notch EQ

Required because feedback is often narrow-band and cannot be addressed effectively by HPF/LPF alone.

At least 4 bands per input/group, 8 bands on master.

Each band:

- type: bell/notch/low shelf/high shelf
- frequency
- gain
- Q
- bypass

Dedicated notch mode:

- narrow Q range suitable for feedback attenuation
- depth 0 to -24 dB
- optional assignment from feedback detector suggestion

## 5. De-esser

Purpose:

- reduce harsh sibilance, whistles, shrieks, cymbal spill, and high-frequency crowd spikes

Parameters:

- frequency: approximately 2-12 kHz
- threshold
- ratio/range
- attack
- release
- listen/monitor band
- wideband or split-band mode

Initial implementation should prefer split-band reduction to avoid pumping the entire crowd signal.

## 6. Compressor

Parameters:

- threshold
- ratio
- attack
- release
- knee
- makeup gain
- auto makeup optional
- sidechain HPF
- mix for parallel compression

Metering:

- input level
- output level
- gain reduction

Crowd presets:

- Gentle Glue
- Dense Crowd
- Applause Lift
- Broadcast Crowd
- Aggressive Hype

## 7. Limiter

Purpose:
protect outputs and maintain headroom during applause peaks, effect throws, and summed multi-mic transients.

Parameters:

- ceiling
- threshold/input gain
- release
- lookahead
- true-peak mode: out of scope for beta, specified in POST_BETA_SPEC.md section 3

Requirements:

- deterministic latency
- host latency reporting
- clear gain-reduction meter
- final output limiter cannot be accidentally removed from a protected output unless explicitly unlocked

## 8. Digital delay

Modes:

- single delay
- ping-pong
- multi-tap
- tempo-sync where host tempo exists

Parameters:

- time in ms
- musical division
- feedback
- wet/dry
- HPF/LPF in feedback path
- stereo offset
- diffusion optional

Feedback parameter must be bounded below unstable unity gain.

## 9. Echo throw

A performance-oriented wrapper around delay.

Controls:

- momentary trigger
- capture length
- repeat count or decay
- send amount
- tail kill

Designed for controller/MIDI mapping.

## 10. Tape delay

Model components:

- variable delay
- saturation
- wow
- flutter
- head loss/tone
- age/noise
- feedback
- optional stereo head offsets

Parameters:

- delay time
- feedback
- drive
- age
- wow
- flutter
- low-cut
- high-cut
- noise amount
- wet/dry

No attempt to emulate a specific trademarked tape unit is required.

## 11. White-noise crowd enhancer

Purpose:
add controlled broadband excitement/density underneath crowd material.

The white-noise generator must never simply output full-band static at arbitrary level.

Signal design:

1. noise source
2. band shaping
3. optional envelope follower from crowd signal
4. optional transient follower
5. stereo decorrelation
6. gain control
7. limiter/safety cap

Parameters:

- amount
- color/tilt
- HPF
- LPF
- crowd-follow amount
- attack/release
- stereo width
- random seed (`seed` parameter, persisted with state and scenes; same seed gives the same noise)

Noise colors (built): White, Pink, Brown, Bright (`color` choice), plus filtered hiss through the HPF/LPF and tilt controls.
Further colors are out of scope for beta.

## 12. Reverse Chunk effect

Core requirement:
continuously capture audio into a circular buffer and play selected chunks backward.

Default chunk length:
4.0 seconds.

Parameters:

- chunk length: 0.25-8.0 s
- trigger mode: manual, repeating, transient, beat-sync
- wet/dry
- crossfade
- retrigger behavior
- stereo link
- pre/post source selection

Modes:

### Triggered
On trigger, freeze the most recent chunk and play it backward while live audio continues on dry path.

### Continuous reverse
Alternating buffers allow continuous reversed blocks with crossfades.

### Repeat reverse
Triggered chunk can loop backward repeatedly with decay.

Implementation:

- preallocate circular buffers
- no copying large blocks on trigger
- store write/read indices and buffer generation
- crossfade start/end to suppress clicks
- define behavior if sample rate changes
- ensure channel synchronization

## 13. Polarity reversal

Exact sample multiplication by -1.

Available per:

- input
- group
- output

UI name should be “Polarity Ø” or “Invert Polarity,” not “Silence Crowd.”

## 14. Continuous phase/all-pass tool

Built: the per-input `phaseStages` (Off, 1 to 4 all-pass stages) and phase frequency parameters.

Use cascaded all-pass filters to vary phase relationship without changing steady-state magnitude strongly.

Useful for:

- correlated microphone alignment experiments
- creative combing
- reducing some coherent sums

Must include phase/correlation visualisation when stereo.

## 15. Width/decorrelation

Crowd enhancement benefits from controlled spaciousness.

Parameters:

- width
- decorrelation amount
- Haas offset with safe range
- mono compatibility control
- low-frequency mono cutoff

Do not use unrestricted short delay on low frequencies; keep bass centered/compatible where possible.

## 16. Gate/expander

Purpose:

- suppress crowd-mic stage spill during quiet moments
- reduce HVAC/noise when no crowd is active

Parameters:

- threshold
- range
- ratio
- attack
- hold
- release
- sidechain HPF/LPF
- hysteresis

## 17. Transient/applause enhancer

Built (per-input transient enhancer, `TransientEnhancer`).

Purpose:
emphasize applause onset without simply increasing overall loudness.

Controls:

- attack gain
- sustain gain
- sensitivity
- mix

## 18. Effect ordering

Default input chain:

Trim -> Polarity -> HPF/LPF -> Notch EQ -> Align Delay -> Gate -> De-esser -> Compressor

Default group chain:

EQ -> Compressor -> Width -> Sends

Default master chain:

EQ -> Glue Compressor -> Limiter

Creative effects normally run as sends/returns to avoid destroying intelligibility and preserve a dry path.

Send/return effects (FX slots, fixed processing slot numbers; the on-screen order is a display-only `fxOrder` permutation):

| Slot | Effect | Id prefix | Section |
|---|---|---|---|
| 1 | Digital delay | `fx.delay.` | 8 |
| 2 | Echo throw | `fx.echo.` | 9 |
| 3 | Tape delay | `fx.tape.` | 10 |
| 4 | Reverse chunk | `fx.reverse.` | 12 |
| 5 | Noise enhancer | `fx.noise.` | 11 |
| 6 | Stadium reverb | `fx.reverb.` | 19 |
| 7 | Crowd multiplier | `fx.multi.` | 20 |

All seven slots run in parallel from their own send bus (input sends and group sends) and sum into the master bus and their own bus-source rows. There is no serial FX chain: a slot never feeds another slot. Slot numbers and `fx.*` / `fxSendN` ids are frozen; new slots are only ever appended. A saved `fxOrder` with five entries (older builds) loads as that order followed by slots 6 and 7.

## 19. Stadium Reverb (FX slot 6, id `reverb`)

Purpose:
a large-space reverb that places the crowd inside a stadium or arena: long, dense, dark tails with no metallic ringing, and instant control over how much of it stays.

Signal design (stereo send in, stereo WET out):

1. pre-delay (free ms or tempo-synced)
2. 4 + 4 input diffusion allpasses (different lengths per channel)
3. 8-line feedback delay network, orthogonal 8x8 Hadamard feedback matrix (scaled 1/sqrt 8)
4. per line: slow modulated delay (cubic interpolation), one-pole high-frequency damping, loop gain from RT60
5. decorrelated stereo output taps (orthogonal sign patterns), width (M/S)
6. low-cut on the wet signal, wet level
7. the `Return` level is applied by the FX rack, like every send/return effect

The output is wet only. There is no dry/mix control.

Parameters (frozen ids `fx.reverb.<suffix>`; the order of the table is the enum order and only grows at the end):

| Suffix | Range | Default | Meaning |
|---|---|---|---|
| `on` | off/on | off | Off: input and return fade out in 10 ms, state is cleared, CPU cost drops to a buffer clear |
| `size` | 0..1 | 0.6 | Scales the delay-line lengths (x0.15 .. x1, so line 1 spans 5.6 .. 37 ms and line 8 spans 20 .. 132 ms) |
| `decay` | 0.2..20 s | 2.5 s | RT60 (mid band): every mode of the network decays 60 dB in this time |
| `preDelayMs` | 0..250 ms | 20 ms | Pre-delay when `preSync` is off |
| `preSync`, `preDiv` | off/on, division | off, 1/16 | Pre-delay from the transport tempo (same divisions as the digital delay, clamped to 250 ms); without a tempo the free value is used |
| `hfDampHz` | 1..20 kHz | 8 kHz | High-frequency damping inside the loop (one-pole). 20 kHz is transparent |
| `lowCutHz` | 20..1000 Hz | 120 Hz | Low-cut (high-pass) on the wet output only |
| `diffusion` | 0..1 | 0.7 | Input allpass coefficient (0 .. 0.72) |
| `modDepth` | 0..1 | 0.3 | Delay modulation depth, up to 1.2 ms |
| `modRate` | 0.05..5 Hz | 0.6 Hz | Modulation rate (each line has its own rate ratio 0.71 .. 1.29) |
| `width` | 0..1 | 1 | 0 = mono wet, 1 = fully decorrelated |
| `wetDb` | -60..+12 dB | -3 dB | Wet level |
| `freeze` | off/on | off | Hold the tail (also follows the global Freeze) |
| `tailKill` | off/on (momentary) | off | While held: tail fades out in about 40 ms and the loop drains; release fades back in from silence |
| `returnDb` | -80..+6 dB | 0 dB | FX return level (applied by the rack) |

Contract and invariants:

- Unconditional stability. Decay is clamped to 0.2 .. 20 s, the loop gain of every line is below 1 for every parameter value (freeze uses 0.99998), the feedback matrix is orthogonal, damping, interpolation and the allpasses have gain <= 1. Independently of that, every value written to a line passes a soft limiter (knee 4) and a hard clamp (+/-8), inputs are sanitised (non-finite = 0, clamped to +/-64) and NaN/Inf parameters fall back to their defaults. The output is finite and bounded for any parameter combination, time-varying delays included.
- Level: the output is scaled so that the impulse-response energy per channel is about 1 for any `decay` / `size` (a noise-like send returns at about the same level at `wetDb` = 0).
- RT60: measured on the output below 2 kHz, the decay time matches `decay` within 20 % for 0.5 .. 20 s at 44.1 / 48 / 96 / 192 kHz.
- Freeze: the input is gated closed (50 ms), the loop gain becomes 0.99998 and damping is faded out, so the tail holds at constant level (bounded, no growth). Freeze never changes the level.
- Tail kill is the panic for a tail: >60 dB down in 80 ms, state drained. It is a momentary parameter (not stored in scenes, OSC pulse `/crowdmike/fx/reverb/tailKill`, hotkey `K`).
- Real-time: no allocation, locks or denormal stalls in `process()`; cost per sample is independent of the settings; all delay/filter state advances per sample and control updates run on a persistent 16-sample counter, so the output is bit-identical for any block size (1 .. 2048) and for repeated `reset()`.
- Tail length reported to the host is `max(4 s, decay + pre-delay)` while the effect is on.

Factory presets (additive, never replacing existing ones): Arena Hall, Stadium Bowl, Small Club, Endless Wash.

## 20. Crowd Multiplier (FX slot 7, id `multi`)

Purpose:
make a few microphones sound like many people: 2 .. 16 decorrelated voices built from modulated micro-delays (a "crowd chorus") that thicken a crowd capture without a pitched chorus sound.

Signal design (stereo send in, stereo WET out):

1. one stereo delay buffer shared by all voices
2. per voice: base delay 2 .. 26 ms, a smoothed random LFO (cubic interpolation between random targets, per-voice rate ratio 0.7 .. 1.3) adding up to `depth` ms, a delay-ramp detune (two crossfaded ramp taps), a small stereo time offset, equal-power pan, gain jitter (up to +/-30 %) and a tone tilt around 1.2 kHz
3. voices are summed and scaled by 1 / sqrt(N)

Parameters (frozen ids `fx.multi.<suffix>`):

| Suffix | Range | Default | Meaning |
|---|---|---|---|
| `on` | off/on | off | Off: fade out in 10 ms and clear |
| `voices` | 2..16 | 8 | Number of voices. Voice k is identical whatever N is; voices fade in/out in 30 ms |
| `depthMs` | 0..20 ms | 4 ms | Random delay modulation depth |
| `rateHz` | 0.05..8 Hz | 0.8 Hz | Mean LFO rate |
| `detuneCents` | 0..50 | 6 | Maximum detune (each voice uses 40 .. 100 % of it, alternating down/up) |
| `scatter` | 0..1 | 0.7 | Pan spread, stereo time offset and gain jitter |
| `tone` | 0..1 | 0.5 | Per-voice tone tilt (+/-0.5 around 1.2 kHz) |
| `seed` | 1..9999 | 1 | Deterministic seed of all per-voice traits |
| `levelDb` | -60..+12 dB | 0 dB | Output level |
| `returnDb` | -80..+6 dB | 0 dB | FX return level (applied by the rack) |

Contract and invariants:

- Deterministic: the same seed, parameters and input give bit-identical output after `reset()` and for any block size. Changing the seed glides the voices (no clicks), changing the voice count fades voices in or out; a newly woken voice is fully re-initialised, so the result never depends on history.
- Loudness: the sum is scaled by 1/sqrt(N); uncorrelated (crowd-like) material returns within about -4 .. 0 dB of the input level for every voice count, a fully coherent signal never exceeds 2.47 sqrt(N) times the input.
- Mono compatible: the fold-down (L+R)/2 stays within 5 dB of the channel level for every `scatter`; no voice combination cancels.
- Detune is a chorus-type delay-ramp detune (the long-term mean pitch is unchanged); `detuneCents` = 0 and `depthMs` = 0 gives pure static delayed copies.
- Stability: bounded and finite for any parameter value, non-finite parameters fall back to defaults, input clamped to +/-64.
- Real-time: no allocation or locks in `process()`; control updates every 16 samples; cost grows with the active voices only.

Factory presets (additive): Double The Crowd, Packed Terrace, Full Stadium.
