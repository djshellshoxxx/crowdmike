# BEAT_SYNC_SPEC -- tempo sources, beat tracking, trigger quantise, scene recall scope

Status: implemented on branch `feat/beat-sync`. Code: `Source/DSP/{BeatTracker,TapTempo,MidiClockTempo,TriggerQuantiser}`,
`Source/Control/TempoHub`, parameter module `Mod::Tempo` (`tempo.*`). Tests: `tests/dsp/BeatSyncTest.cpp`, `tests/engine/BeatSync*Test.cpp`.

## 1. Purpose

Every tempo-synced effect (Delay division, Echo, Reverse beat division) and every quantised trigger reads one `FxContext`
tempo (`bpm`, `hasTempo`, `ppqPosition`, `playing`). Until now it came only from the host playhead, which does not exist
in the standalone app and is often unset in live rigs. Beat Sync lets the user pick where tempo comes from, and lets
Echo Throw, Reverse Chunk and clip triggers land on the beat.

## 2. Sources (`tempo.source`)

| Index | Source | bpm | ppq / playing |
|---|---|---|---|
| 0 Host (default) | host `AudioPlayHead` | host bpm (unset => hasTempo=false) | host ppq / isPlaying; if the host gives bpm but no ppq the position free-runs from 0 while playing |
| 1 Tap | last tap tempo (+ nudge) | hasTempo only after a tempo is known | free-running beat clock, always "playing" |
| 2 Manual | `tempo.manualBpm` (+ nudge) | always | free-running, always playing |
| 3 MIDI Clock | incoming 0xF8 clock | after 3 ticks, smoothed | clock-derived ppq; playing follows Start/Continue/Stop |
| 4 Auto | BeatTracker on `tempo.autoSource` | locked tempo (+ nudge), held | phase-locked to the detected beat grid; playing while a lock exists |

Default source is Host, so existing shows behave exactly as before. Free-running sources advance ppq by
`bpm/60 * n / sampleRate` per block. A change of source, tempo or a resync never produces a NaN, a negative ppq or a jump
of more than one beat in the reported position other than the defined resync/phase-lock correction.

## 3. Parameters (module `tempo`, appended after the macros; all earlier indices unchanged)

| ID | Name | Type / range | Default | Scene recall | Notes |
|---|---|---|---|---|---|
| `tempo.source` | Tempo Source | choice Host/Tap/Manual/MIDI Clock/Auto | Host | yes | |
| `tempo.manualBpm` | Manual BPM | 20..300 BPM, step 0.01 (skew centred at 120) | 120 | yes | |
| `tempo.nudgeBpm` | Tempo Nudge | -10..+10 BPM | 0 | yes | added to Tap/Manual/Auto; result clamped 20..300 |
| `tempo.quantise` | Trigger Quantise | choice Off/1/4/1/2/1 bar | Off | yes | |
| `tempo.autoSource` | Auto Tempo Input | choice Master, Input 1..16 | Master | yes | |
| `tempo.barBeats` | Beats Per Bar | choice 4,3,2,5,6,7 | 4 | yes | |
| `tempo.tap` | Tap Tempo | momentary bool | 0 | never (transient) | rising edge = tap |
| `tempo.resync` | Resync Downbeat | momentary bool | 0 | never (transient) | rising edge = resync |

Params are normal ParamBank parameters: saved in the plug-in state and `.crowdshow`, automatable, macro- and MIDI-bindable,
OSC-addressable (`/crowdmike/param/tempo.source ...`). State saved by schema-v2 builds without these parameters loads with
all tempo params at their defaults and every pre-existing parameter value unchanged (regression test).

## 4. Tap tempo (`TapTempo`)

Keeps the last 8 taps. A tap more than 2.5 s after the previous one starts a new sequence (the old tempo is kept until a
new one is known). Taps closer than 0.2 s are bounce and ignored. bpm = 60 / mean of inlier intervals; with at least 4
intervals those further than 25 % from the median are rejected (if fewer than half remain, restart from the newest two
taps); with 2-3 intervals a newest interval differing by more than 50 % from the earlier mean restarts the sequence.
A tempo needs 2 taps. bpm is clamped to 20..300. Tap sources: GUI TAP button, `tempo.tap` parameter (MIDI note/CC via
binding target `TempoTap`, or a Param binding), OSC `/crowdmike/tempo/tap`. A tap also aligns the beat grid: the free-running
beat position snaps to the nearest whole beat at the next block start.

## 5. MIDI clock (`MidiClockTempo`)

24 ticks per quarter note. Tick positions are sample times on the audio clock (block start + event offset). Tempo: with 25
ticks known it is the span of exactly one beat (cancels jitter); fewer: mean interval. One-pole smoothing 0.25 per tick;
a change of more than 25 % snaps. A gap above 0.5 s clears history but keeps the last tempo and flags low confidence.
Start (0xFA) resets position to 0 and runs; Continue (0xFB) runs; Stop (0xFC) stops, position kept. A tick without any
Start/Stop seen is treated as running. ppq is extrapolated between ticks and never beyond the next expected tick
(monotonic). Song position pointer is not interpreted. Clock bytes are consumed by the hub and never reach the MIDI binding
layer. Events per block are capped at 128 (excess ticks are dropped, never allocated).

## 6. Auto tempo (`BeatTracker`)

* Audio thread: `pushSamples()` computes a band-split onset-strength value per hop (about 4 ms) and writes it to an SPSC
  FIFO of at least 16 s. No allocation, locks, IO or logging. Overflow drops the oldest frames and restarts analysis.
* Worker (`AnalysisThread`, 25 ms cadence, acquired only while `tempo.source == Auto`): drains the FIFO, keeps an 8 s onset
  history, and every 0.5 s estimates tempo by autocorrelation combined with a 4-harmonic comb over 60..200 BPM, with a
  mild tempo prior favouring the middle of the range.
* Octave handling: a candidate at 2x (or 1/2) the current lock replaces it only when its score is within the octave
  ratio (0.97) of the best and it lies inside 60..200; otherwise the lock stays. Hysteresis: a new tempo must win on
  consecutive estimates before the lock moves; small drifts are tracked smoothly.
* Beat phase: a comb over the recent onsets at the locked period gives the last beat position; the hub extrapolates ppq
  from it and slews toward it with a 0.8 s time constant. The reported tempo follows the lock with a 0.3 s time constant.
* Confidence 0..1 from the peak-to-mean ratio of the autocorrelation; it decays when the input is silent or aperiodic. The
  tempo is held (never reset) while confidence is low; `lowConfidence` is reported below 0.3.
* Deterministic: identical samples give identical estimates regardless of block size or worker timing.
* Accuracy target (tests): click, kick and kick+snare patterns at 70/100/128/140/174 BPM, with noise at 15 dB SNR, report
  within +/-1.5 BPM after 12 s.

## 7. Trigger quantise (`TriggerQuantiser`)

Applies to Echo Throw (`fx.echo.trigger`), Reverse Chunk (`fx.reverse.trigger`) rising edges and clip triggers
(`PlaybackEngine::trigger(slot, offset)`). Grid = 1 beat (1/4), 2 beats (1/2) or `barBeats` beats (1 bar) on the TempoHub
ppq. A request made in a block fires at the first sample whose position reaches the next grid line (sample-exact:
`ppq(offset) >= grid > ppq(offset-1)`); if the block holds no grid line it stays pending. Requests while pending coalesce.
A pending request never waits more than 2 bars + 1 beat. If quantise is Off, tempo is unavailable, or the transport is not
playing, triggers behave exactly as before (pass-through, no added latency). The FX rack splits the block at the fire
offset so the effect sees the trigger on exactly that sample.

## 8. Tempo nudge and resync

Nudge adds a BPM offset (Tap/Manual/Auto) so the user can ride a drifting band. Resync (`tempo.resync`, GUI button, OSC
`/crowdmike/tempo/resync`, MIDI binding `TempoResync`) sets the free-running beat position to 0 at the next block start,
i.e. "this is beat 1". Host and MIDI-clock sources ignore resync (they have their own position).

## 9. Output guarantees (TempoHub)

`process()` returns bpm in [20, 300] whenever hasTempo is true; ppq finite and >= 0; no NaN/inf for any input
(including host bpm 0, NaN, huge values, block sizes 1..2048, 44.1-192 kHz). Loss of signal or clock holds the last
tempo and flags low confidence. `snapshot()` is a relaxed-atomic copy for the UI.

## 10. UI

* Status bar second row: source selector, BPM read-out ("--.-" without tempo, "?" suffix when low confidence), TAP button,
  beat LED (filled during the first fifth of each beat, accent colour on the downbeat), confidence bar, quantise selector.
* Settings page, section "Tempo / Beat Sync": source, manual BPM, nudge, beats per bar, Auto input, quantise, Resync
  button, live read-out. The OSC cheat sheet lists the new addresses.
* Scenes page: the recall-scope toggles write the shared `ui.recallScope` and follow changes made by MIDI/OSC.

## 11. MIDI / OSC

OSC: `/crowdmike/tempo/tap`, `/crowdmike/tempo/resync` (alias `downbeat`), `/crowdmike/tempo/bpm|source|quantise|nudge|autoSource|barBeats <value>`.
MIDI: new binding targets `TempoTap`, `TempoResync`, `SceneScopeToggle` (appended to `BindTarget`; older shows load unchanged);
any tempo parameter is also bindable as a Param target. MIDI realtime clock messages feed the hub.

## 12. Scene recall scope (audit fix)

Previously MIDI and OSC scene recall ignored the recall scope (always a full recall). Now `ControlHub` routes every scene select/next/prev through
`SceneBank::recall(slot, scope, ramp)` with the show's current `ui.recallScope` (bits: inputs, groups, fx, master, outputs,
routing, names, macros). Scope can be set remotely: OSC `/crowdmike/scene/scope <mask 1..255>`,
`/crowdmike/scene/scope/<inputs|groups|fx|master|outputs|routing|names|macros|all> [0|1]` (no argument toggles; ignored if it would empty the scope), and MIDI
binding target `SceneScopeToggle` (targetIndex 0..7 toggles a bit, 8 = all). A mask of 0 is rejected (scope never becomes
empty). Tempo params are recalled with the FX scope; `tempo.tap` and `tempo.resync` are never captured.

## 13. Acceptance tests

1. BeatTracker: click, kick, kick+snare at 70/100/128/140/174 BPM, with noise, within +/-1.5 BPM; no octave errors; silence holds tempo; deterministic across block sizes; outputs finite.
2. TapTempo: steady taps, outlier rejection, 2.5 s reset, bounce, tempo change, clamps.
3. MidiClockTempo: 24-ppqn math at several rates, jitter, jump, gap, Start/Stop/Continue, ppq monotonic.
4. TriggerQuantiser: sample-exact on grid for all modes, block sizes and rates; coalescing; timeout; pass-through.
5. TempoHub: each source; finite/limits for hostile inputs; nudge; resync; state publish.
6. Engine wiring: Echo/Reverse/clip triggers land on the grid sample-exactly through `CrowdEngine`; tempo-synced delay follows the hub.
7. State: tempo params round-trip through state and `.crowdshow`; schema-v2 state without tempo params loads identically; tap/resync not captured by scenes.
8. ControlHub: MIDI and OSC scene recall honour scope; OSC scope addresses and MIDI scope binding; tempo OSC addresses resolve.
