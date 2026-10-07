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
- true-peak mode future option

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
- random seed persistence optional

Future noise colors:
pink, brown, bright, filtered hiss.

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

Optional v1.1 feature.

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

Optional but recommended.

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
