# CrowdMike Routing and Wiring Specification

## 1. Routing model

CrowdMike uses a directed signal graph with explicit source and destination classes.

Sources:

- hardware/host input channels
- processed input channels
- group outputs
- FX returns
- playback channels
- master buses

Destinations:

- crowd groups
- FX inputs
- recorder taps
- master buses
- hardware/host outputs

The route validator must reject accidental DSP cycles unless a specific feedback-capable effect owns and bounds that loop internally.

## 2. Default wiring

```text
Mic Inputs
  -> Input Processing
  -> Crowd Groups
  -> FX Sends/Returns
  -> Crowd Master
  -> Output Matrix
  -> PA / Broadcast / Record / Other destinations
```

Raw input taps feed the recorder before processing when enabled.
Processed-input taps are also available.
Group and master taps are independently recordable.

## 3. Output use cases

Examples:

- Output 1/2: main PA crowd reinforcement
- Output 3/4: broadcast crowd mix
- Output 5/6: stage/artist monitor crowd mix
- Output 7/8: recorder/stream feed

These are templates, not fixed assignments.

## 4. Delay and alignment

There are three distinct delay concepts.

### Alignment delay
Used to time-align microphones or groups.

### Reinforcement delay
Intentional latency between captured crowd and reinforced crowd. This may reduce correlation with direct sound in some situations, but it is not a feedback guarantee.

### Creative delay
Effect delay with repeats/feedback/tone shaping.

The GUI and state model must keep these concepts separate.

## 5. Polarity and phase

Per input/group/output:

- polarity invert: exact 180-degree polarity inversion
- continuous phase/all-pass control is built for inputs (`phaseStages` / all-pass phase parameters on the Inputs page and inspector); groups and outputs keep polarity invert only

Use cases:

- correct inverted wiring
- improve summing between correlated microphones
- creative cancellation/comb effects
- attempt localized reduction of correlated reinforced signals

The product must not label polarity inversion as guaranteed crowd cancellation.

## 6. Feedback-aware routing rules

When a live microphone is routed to a loudspeaker near that microphone, CrowdMike computes a risk indicator from:

- route existence
- input trim
- bus gain
- output gain
- compressor makeup
- delay feedback
- measured persistent spectral peaks
- user-defined microphone/speaker zone metadata if available

Risk scoring is advisory, not an acoustic guarantee.

## 7. Route transitions

Enabling/disabling routes uses short gain ramps.
Scene changes never hard-switch a high-level audio route unless panic is engaged.

## 8. Wiring presets

Provide templates:

- 1 mic -> stereo PA
- 2 mic stereo crowd -> stereo PA
- 4 mic room -> stereo crowd bus
- front/rear crowd -> PA + broadcast
- crowd capture only
- virtual soundcheck
- DJ performance FX
- venue multi-zone

## 9. Channel naming

Each port supports:

- stable internal ID
- user display name
- optional physical label
- optional location/zone

Stable IDs are used for persistence so renaming does not break scenes.

## 10. Bus-source table (persisted matrix rows)

The output bus matrix maps bus sources (rows) to output channels. The row numbers are part of the persisted format (`Routing` ValueTree, property `bus`: rows x 16 outputs, output-major, stride = row count) and recorder tap ids reference them indirectly, so they are frozen: new rows are only ever appended after the existing ones.

| Rows | Source | Notes |
|---|---|---|
| 0..15 | Group 1..8 L/R | `group * 2 + channel` |
| 16..25 | FX 1..5 returns L/R (Delay, Echo, Tape, Reverse, Noise) | `16 + fx * 2 + channel` |
| 26..27 | Playback L/R | |
| 28..29 | Master L/R | |
| 30..31 | FX 6 return L/R (Stadium Reverb) | appended with FX slot 6 |
| 32..33 | FX 7 return L/R (Crowd Multiplier) | appended with FX slot 7 |

Total 34 rows (`kBusSrcCount`). Helpers: `busRowForFxReturn(fx, ch)`, `busRowIsFxReturn(row)`, `busRowFxIndex(row)` (Limits.h); `kBusSrcLegacyCount` = 30 is the original row count and `kBusSrcFxExtraBase` = 30 the first appended row.

Persistence: `bus` keeps the legacy 30-row stride so a state saved by a 5-FX build loads identically; the appended rows travel in the extra property `busFx2` (4 rows x 16 outputs). A missing `busFx2` loads as silent rows (no route), and a new save is still readable by older builds (they ignore `busFx2`). Direct and assign matrices are unaffected.

Recorder taps: the tap ids of the 5 legacy FX returns, master and output pairs keep their numbers; the returns of FX 6 and 7 are the two ids after the output pairs (`TapMap::FxExtra`, `TapMap::forFx(fx)`, `TapMap::fxSlotOf(id)`).

FX sends: input and group FX sends 6 and 7 are appended parameters (`inputN.fxSend6/7`, `groupN.fxSend6/7`, default -80 dB = off) placed after every earlier parameter; use `inputSendParam(fx)` / `groupSendParam(fx)` instead of computing `Fx1 + slot`. The route validator treats all seven FX as send/return nodes that terminate in the master and the bus matrix (no FX-to-FX cycles are possible).
