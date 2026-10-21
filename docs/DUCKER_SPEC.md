# CrowdMike Performer Ducker Specification

Status: implemented on branch `feat/usability` (module `Source/DSP/Ducker.*`, engine wiring in `Source/Engine/CrowdEngine.*`,
UI in `Source/UI/DuckerStrip.*`, tests in `tests/dsp/DuckerTest.cpp` and `tests/engine/DuckerEngineTest.cpp`).

## 1. Purpose

When the performer speaks or sings, crowd reinforcement and effect returns must not mask the voice and the acoustic feedback
risk of the open crowd microphones must drop. The ducker listens to one chosen *key input* (normally the performer's microphone
or a DI) and, while that input is active, pulls the chosen buses down by a fixed, smoothed amount. It is voice-activated,
deterministic, never boosts, and never touches recordings of raw or processed inputs.

Non-goals: it is not a general side-chain compressor (no ratio/knee), it does not process the key input, and it does not
duck the direct input-to-output matrix (a performer routed straight to the PA is never ducked).

## 2. Parameters

All parameters are appended to the **global** block (`GlobalP`, `Mod::Global`, empty ID prefix), after `scenePrev`. The IDs below
are permanent. The block is contiguous: `params::duckBase() == GlobalP::DuckOn` and the enum `params::DuckP` indexes it
(`p = effectiveGlobal + duckBase()`).

| `DuckP` | ID | Name | Kind | Range | Default | Unit |
|---|---|---|---|---|---|---|
| On | `duckOn` | Ducker On | bool | 0/1 | 0 | |
| Key | `duckKey` | Ducker Key Input | choice | `None`, `Input 1` .. `Input 16` (index 0..16) | 0 (None) | |
| ExcludeKey | `duckExcludeKey` | Ducker Exclude Key Input | bool | 0/1 | 1 | |
| Thr | `duckThrDb` | Ducker Threshold | float | -80 .. 0 | -35 | dB (dBFS of the filtered key) |
| Depth | `duckDepthDb` | Ducker Depth | float | -30 .. 0 | -6 | dB |
| Attack | `duckAttackMs` | Ducker Attack | float | 1 .. 200 | 10 | ms |
| Hold | `duckHoldMs` | Ducker Hold | float | 0 .. 2000 | 250 | ms |
| Release | `duckReleaseMs` | Ducker Release | float | 10 .. 5000 | 400 | ms |
| Target | `duckTarget` | Ducker Target | choice | `Crowd groups`, `FX returns`, `Both` | 2 (Both) | |
| Range | `duckRangeDb` | Ducker Range Clamp | float | -30 .. 0 | -30 | dB |
| KeyHpf | `duckKeyHpfHz` | Ducker Key HPF | float | 20 .. 2000 | 120 | Hz |
| KeyLpf | `duckKeyLpfHz` | Ducker Key LPF | float | 1000 .. 20000 | 6000 | Hz |
| Lookahead | `duckLookaheadMs` | Ducker Lookahead | float | 0 .. 10 | 0 | ms |

Meaning of the less obvious ones:

* **Depth** is the reduction applied while the key is active. **Range** is an absolute clamp on the applied reduction:
  `appliedDb = max(Depth, Range)` (both are <= 0, so the *smaller* magnitude wins). With the default Range of -30 dB it never
  limits; it exists so a macro or MIDI controller that modulates Depth can be bounded by a second, independent control.
* **Key HPF / LPF** shape the detector input only (2nd-order Butterworth each, Q = 0.7071). They never touch the audio that is ducked.
* **Lookahead** delays the ducked buses (see 4.4); it is not a detector parameter.
* "Depth 0 dB" is legal and means the ducker does nothing audible; the meters still show detection.

Because every parameter is an ordinary APVTS parameter the whole set is automatically host-automatable, saved in plug-in state,
captured and recalled by scenes (scope `Master`, none are transient), addressable from MIDI learn/bindings, from macros
(`MacroDest.paramIndex`) and from OSC (`/crowdmike/global/duckDepthDb`, `/crowdmike/param/duckOn`, `/crowdmike/paramnorm/...`).
No new ShowModel field is needed.

## 3. Algorithm (`crowdmike::dsp::Ducker`)

```cpp
class Ducker {
public:
    void prepare(double sampleRate, int maxBlock);          // allocates nothing large; may be called repeatedly
    void reset() noexcept;                                  // deterministic: same input afterwards => identical output
    // key: n samples of the (post-trim) key. p: DuckP block. enabled: engine-level enable (On && !Bypass && key valid).
    // gain: n linear gains (<= 1.0f). Returns true when any gain differs from 1.0f.
    bool process(const float* key, int n, const float* p, bool enabled, float* gain) noexcept;
    float getGainReductionDb() const noexcept;              // >= 0 dB of reduction at the end of the last block
    float getKeyPeakDb() const noexcept;                    // detector level (dBFS) at the end of the last block
    bool isOpen() const noexcept;                           // detector state (key active)
};
```

Per sample:

1. `x = sanitize(key[s])` (non-finite -> 0).
2. `y = LPF(HPF(x))` with the key filters (coefficients recomputed only when `KeyHpf`/`KeyLpf` change by more than 0.5 %).
3. Peak envelope with instant attack and a fixed 5 ms release time constant: `e = max(|y|, e * c)`, `c = exp(-1 / (0.005 * sr))`.
   Level `L = 20 log10(max(e, 1e-6))` dBFS (a steady sine reads its peak level).
4. State machine with a fixed hysteresis of **3 dB**:
   * closed -> open when `L >= Thr` (and `enabled`);
   * while open, `holdLeft` is reloaded to `round(Hold * sr / 1000)` whenever `L >= Thr - 3`;
   * when `L < Thr - 3`, `holdLeft` counts down one per sample; when it reaches 0 the state closes;
   * when `enabled` is false the state closes immediately (the release ramp below still applies, so there is no click).
5. Target reduction `T = open ? max(Depth, Range) : 0` dB (<= 0).
6. The reduction state `g` (dB, <= 0) moves **linearly in dB** towards `T`:
   * span `S = max(|max(Depth, Range)|, |g|, 0.01)` dB;
   * attack (T < g): `g -= S / max(1, Attack * sr / 1000)` per sample, not below T;
   * release (T > g): `g += S / max(1, Release * sr / 1000)` per sample, not above T.
   So *Attack is the time to travel the whole Depth* and *Release is the time to travel back the whole Depth*.
   A 10 %..90 % traverse therefore takes exactly `0.8 * time`.
7. Linear gain `10^(g/20)` is written to `gain[s]`; samples where `g == 0` write exactly `1.0f`.

Timing accuracy: the only unavoidable offsets are the detector rise (at most one half-cycle of the key plus the key-filter delay,
< 1 ms for keys above 500 Hz) and, for release, the detector fall (`Hold + 5 ms * ln(10^((L - (Thr - 3))/20))`). The ramps
themselves are exact to one sample.

Steady state: `gain = 10^(max(Depth, Range)/20)` to within 0.001 dB.

## 4. Engine integration

### 4.1 Key tap

The key signal is `raw[key] * 10^(Trim_key / 20)` (the host input after virtual-soundcheck substitution, scaled by the key input's
**trim** only, block-constant). Mute, solo, polarity, filters, EQ, dynamics, link and group assignment of the key input have no
effect on detection. For a linked stereo pair only the channel selected as key is used. A key index at or above the number of
active inputs, or `None`, means "no key": the ducker is disabled (it releases and the meters read 0).

### 4.2 Where the gain is applied

Pipeline position: after the FX rack (ENGINE_SPEC 2, step 6) and before the master sum and output matrix.
Targets are exactly the stereo buses the engine sums into the master and offers to the bus matrix:

* **Crowd groups** (`Target` = Groups or Both): group `g` is ducked unless `ExcludeKey` is on and the key input is assigned to it
  (`RoutingState::getAssign(key, g) != 0`). A group that carries the performer is therefore never ducked, so the performer is
  never ducked through his own group.
* **FX returns** (`Target` = FX returns or Both): every effect return is ducked (the return carries the wet signal only).

Not ducked, by construction: the direct input-to-output matrix; the playback bus (clip route 9 / "Playback" bus row) and
playback routed straight to master (route 0); all input meters/histories; the FX **sends** (the sends are taken before the duck);
every recorder tap that precedes the duck.

### 4.3 Recorder taps

Recording must stay clean. The duck is applied to *copies* of the group/FX buffers (`duckBuf`), never in place:

| Tap | Ducked? |
|---|---|
| Raw input, processed input | never |
| Group, FX return | never (they see the pre-duck buffers) |
| Master, output pairs | **yes** (they record what the PA receives) |
| Playback | never |

When the ducker is idle (gain exactly 1, lookahead 0, nothing gliding) the engine uses the original buffers directly and does no copy.

### 4.4 Lookahead

`Lookahead` (0..10 ms) is implemented with the existing `dsp::ChannelDelay`: while the ducker is *enabled*, **all** group and FX-return
copies are delayed by `Lookahead` ms (targeted or not, so the relative timing of the crowd buses never changes), and the gain
curve is applied to the delayed audio, i.e. the gain falls `Lookahead` ms before the voice reaches the ducked audio. `ChannelDelay`
glides when the value changes (no clicks). The delay is *not* added to the reported plug-in latency: the direct input matrix and the
non-delayed paths are not delayed, so use lookahead only for crowd/FX buses that are mixed with the performer acoustically.
With `Lookahead = 0` (default) no delay is processed.

### 4.5 Panic, Bypass, Rehearsal, Soundcheck

* **Panic**: the output stage mutes exactly as before (5 ms ramp, after the recorder taps). The ducker keeps running so its meters stay
  honest and the state is correct the moment panic is released. The ducker can only reduce gain, so it can never unmute a panicked output.
* **Bypass**: `enabled = On && !Bypass`. While bypassed the ducker releases (over `Release`) and its meters fall to 0; the bypass dry
  path is the raw input and is never ducked. The wet path is crossfaded out by the existing 10 ms bypass mix.
* **Rehearsal**: ducker runs as usual (outputs are silent, recording unaffected).
* **Virtual soundcheck**: the key is read after soundcheck substitution, so a soundcheck session can exercise the ducker.
* **Scene recall**: parameters change like any other; the existing recall ramp does not stretch the ducker (its Attack/Release are the
  user's timing), but the applied gain is continuous because it is a state variable.

### 4.6 Real-time rules

`process()` is `noexcept`, allocation-free and lock-free. The gain curve buffer, the copy buffers and the delay lines are sized in
`CrowdEngine::prepare()`. Block sizes 1..2048 and 44.1..192 kHz are supported; output is always finite. When disabled and settled
(`g == 0`, filters idle) a call costs a handful of instructions and `gain` is filled with exactly 1.

## 5. Meters (EngineMeters, relaxed atomics)

| Member | Meaning |
|---|---|
| `duckGrDb` | applied reduction, >= 0 dB (end of the last block) |
| `duckKeyPeak` | `AtomicPeak` of the detector (filtered key) in linear units |
| `duckOpen` | detector state |

## 6. UI

* **Live page**: a ducker strip (`ui::DuckerStrip`) between the ribbon and the strips: `On` toggle, key choice, `Thr`, `Depth`, `Atk`,
  `Hold`, `Rel`, target choice, a key-level bar with the threshold marker and a gain-reduction bar. Every control is a `ParamSlider`/`ParamToggle`/
  `ParamChoice`, so right-click learn/macro assignment, fine-adjust and undo behave like everywhere else. State is shown with text + glyph.
* **Inputs page**: top bar choice `Duck key` bound to `duckKey`, plus a small `KEY` marker on the row of the selected key input.
* Settings: nothing extra (all values are parameters).

## 7. Edge cases

* Depth 0 or Range 0: gain stays 1; the meters still show detection; `process` returns false.
* `Thr` = 0 dB: opens only on a full-scale key.
* `KeyHpf` above `KeyLpf` is allowed (the detector is then a narrow band-stop of nothing: it reads ~0 and never opens); no instability.
* Key input muted/soloed-out: still detected (key is pre-processing). Use `Key = None` or `On = 0` to disable.
* Scene recall with a different key while open: the state machine continues from the current reduction value; no jump.
* `prepare()` while running: state reset; first block after prepare has unity gain.

## 8. Acceptance tests

DSP (`tests/dsp/DuckerTest.cpp`), at 44.1/48/96 kHz and block sizes 1/16/64/512/2048:

1. `ducker_attack_time_matches_formula`: key burst of 1 kHz at -10 dBFS, Thr -35, Depth -12, Attack 20 ms. The time from 10 % to 90 % of depth equals `0.8 * 20 ms` within 1 ms; time from burst start to full depth equals `20 ms` within 1 ms (+ detector rise < 1 ms).
2. `ducker_release_time_matches_formula`: release 100/400 ms, same tolerance, measured from the first release sample.
3. `ducker_hold_delays_release`: hold 0 vs 100 ms differ in release start by `100 ms` within 1 ms.
4. `ducker_depth_accuracy`: steady gain = `10^(Depth/20)` within 0.01 dB for Depth -3, -6, -12, -30; Range clamp honoured (`Depth -20, Range -8` -> -8 dB).
5. `ducker_threshold_and_hysteresis`: key at Thr + 1 dB opens, at Thr - 1 dB stays closed, and a key between Thr - 3 and Thr does not close an open ducker.
6. `ducker_key_filters`: a 40 Hz key with HPF 120 does not open, a 1 kHz key does; an 12 kHz key with LPF 6 kHz does not open.
7. `ducker_deterministic_and_block_size_independent`: identical output after `reset()`; outputs for block sizes 1 and 2048 are bit-identical.
8. `ducker_disabled_is_unity`: `enabled=false` -> exactly 1.0f after settling; releases without a step.
9. `ducker_survives_non_finite_and_extreme_params`.

Engine (`tests/engine/DuckerEngineTest.cpp`, through `CrowdMikeAudioProcessor`):

10. `duck_engine_groups_ducked_by_depth`: key tone on input 1 (routed nowhere), crowd tone on input 2 -> group 1; group output drops by Depth.
11. `duck_engine_target_selection`: Target Groups ducks groups only (FX return untouched), FX returns only, both.
12. `duck_engine_exclude_key`: with the key assigned to the group and ExcludeKey on, that group is not ducked; off -> ducked.
13. `duck_engine_recorder_taps_untouched`: group tap and processed-input tap (via `pushTap` capture) are identical with the ducker on and off, master output is ducked.
14. `duck_engine_panic_bypass_interplay`: panic silences output with the ducker active; ducker released after bypass; bypass dry path is not ducked.
15. `duck_engine_state_scene_macro_osc_midi`: save/restore state, scene recall, macro destination moves Depth, OSC address resolves, MIDI CC binding sets `duckOn`.
16. `duck_engine_regression_old_state_loads`: a schema-v2 state without ducker IDs loads with ducker defaults and unchanged other parameters.
17. `duck_engine_lookahead_delays_buses`: with Lookahead 5 ms the group output is delayed by exactly 5 ms relative to Lookahead 0 and gain falls before the burst reaches the output.
