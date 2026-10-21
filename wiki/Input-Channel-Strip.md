# Input Channel Strip

> Each input runs its own trim, polarity, filters, notch, four-band EQ, phase, alignment delay, gate, de-esser, compressor, and sends.
> Defaults are off or neutral, so a new input passes sound straight through until you change something.

## Order of processing

Default input chain (from the effects specification):

Trim, then polarity, then HPF and LPF, then notch and EQ, then alignment delay, then gate, then de-esser, then compressor.

The phase all-pass, the transient enhancer, and gain riding are also part of the input chain.

## Controls

Values are as shown in the app. Parameter IDs are in brackets for automation and OSC (`inputN.` prefix, N = 1 to 16).

| Control | Range | Default | ID suffix |
|---|---|---|---|
| Trim | -60 to +24 dB | 0 dB | `trimDb` |
| Polarity invert | off or on | off | `polarity` |
| Mute, Solo | off or on | off | `mute`, `solo` |
| Pan | -1 (left) to +1 (right) | centre | `pan` |
| Stereo link with next | off or on | off | `linkNext` |
| High pass | 20 to 500 Hz, 12 / 24 / 36 / 48 dB per octave | off, 80 Hz, 12 dB | `highPassEnabled`, `highPassHz`, `highPassSlope` |
| Low pass | 2 k to 20 kHz, same slopes | off, 18 kHz, 12 dB | `lowPassEnabled`, `lowPassHz`, `lowPassSlope` |
| Notch | 100 Hz to 10 kHz, Q 2 to 60, depth -24 to 0 dB | off, 1 kHz, Q 20, -12 dB | `notchOn`, `notchHz`, `notchQ`, `notchDepthDb` |
| EQ bands 1 to 4 | on, type, 20 Hz to 20 kHz, +/-24 dB, Q 0.1 to 30 | see below | `eq1On` to `eq4Q` |
| Phase stages | Off, 1 to 4 all-pass stages | Off | `phaseStages` |
| Phase frequency | 20 Hz to 5 kHz | 500 Hz | `phaseHz` |
| Alignment delay | 0 to 500 ms | 0 ms | `alignDelayMs` |
| Gate (expander) | see below | off | `gate*` |
| De-esser | see below | off | `deess*` |
| Compressor | see below | off | `inComp*` |
| Transient enhancer | attack and sustain -12 to +12 dB, sensitivity, mix | off | `trans*` |
| Gain riding | off or on | off | `gainRiding` |
| FX sends 1 to 7 | -80 (off) to +6 dB | -80 dB | `fxSend1` to `fxSend7` |
| FX sends pre-fader | off or on | off | `fxSendPre` |

### EQ bands

Each input has four parametric bands. Their defaults are:

| Band | Type | Frequency |
|---|---|---|
| 1 | Low shelf | 120 Hz |
| 2 | Bell | 400 Hz |
| 3 | Bell | 2.5 kHz |
| 4 | High shelf | 8 kHz |

Types are Bell, Notch, Low shelf, and High shelf. A Notch band is a dedicated narrow cut with a depth from 0 to -24 dB. Use it for a feedback frequency the [[Feedback Assistant|Feedback-Assistant]] points out.

### Gate (expander)

Use the gate to quieten the mic between cheers, so stage spill and HVAC noise do not build up.

| Control | Range | Default |
|---|---|---|
| Threshold | -80 to 0 dB | -60 dB |
| Range | -80 to 0 dB | -24 dB |
| Ratio | 1:1 to 20:1 | 2:1 |
| Attack | 0.1 to 100 ms | 5 ms |
| Hold | 0 to 1000 ms | 50 ms |
| Release | 5 to 2000 ms | 200 ms |
| Hysteresis | 0 to 12 dB | 3 dB |
| Sidechain HPF / LPF | 20 Hz to 2 kHz / 1 kHz to 20 kHz | 100 Hz / 12 kHz |

### De-esser

Reduces harsh sibilance, whistles, and cymbal spill. The default mode is split-band, which only pulls down the sibilant band and avoids pumping the whole crowd.

| Control | Range | Default |
|---|---|---|
| Frequency | 2 to 12 kHz | 6.5 kHz |
| Threshold | -60 to 0 dB | -24 dB |
| Ratio | 1:1 to 10:1 | 3:1 |
| Range | 0 to 24 dB | 12 dB |
| Attack / Release | 0.05 to 20 ms / 5 to 500 ms | 1 ms / 60 ms |
| Listen | off or on | off (hears the band being reduced) |
| Mode | Split-band or Wideband | Split-band |

### Compressor

| Control | Range | Default |
|---|---|---|
| Threshold | -60 to 0 dB | -18 dB |
| Ratio | 1:1 to 20:1 | 2.5:1 |
| Attack | 0.1 to 200 ms | 10 ms |
| Release | 5 to 2000 ms | 120 ms |
| Knee | 0 to 24 dB | 6 dB |
| Makeup | -12 to +24 dB | 0 dB |
| Auto makeup | off or on | off |
| Sidechain HPF | 20 Hz to 1 kHz | 80 Hz |
| Mix | 0 to 100 percent | 100 percent (use less for parallel compression) |

The Live strip shows the compressor's gain reduction. The effects spec also lists crowd compressor presets (Gentle Glue, Dense Crowd, Applause Lift, Broadcast Crowd, Aggressive Hype). Check the preset menu for which ones ship in your build.

### Gain riding

Gain riding slowly levels a mic that drifts between quiet and loud, within a small range. It aims at about -20 dBFS, moves at most 6 dB, and holds still when the input is below about -60 dBFS so noise is not pumped up.

## Feedback risk

Each strip on the Live page shows a feedback-risk indicator for its route to a nearby speaker. It is advisory. It uses the trim, the bus gain, compressor makeup, delay feedback, and any persistent peaks. It is not a guarantee. See [[Concepts|Concepts]].

## Good starting points

- Start with trim at 0 dB and the high-pass on at 80 to 120 Hz. Line Check proposes the trim for you.
- Use the notch only on a peak you can hear or see, not as a general EQ.
- Leave the gate off until you have seen the room with the crowd in it.
- Use the FX sends for the performance effects, and keep the dry path clean.
