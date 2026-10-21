# CrowdMike Line Check / Auto-Setup Specification

Status: implemented on branch `feat/usability` (probe + analyser `Source/Engine/LineCheck.*`, apply/undo adapter
`Source/Engine/LineCheckApply.*`, UI `Source/UI/LineCheckPanel.*`, tests `tests/engine/LineCheckTest.cpp`).

## 1. Purpose

Make a first set-up fast and safe: the operator presses *Start* and talks, claps or lets the crowd make noise for 10 seconds. CrowdMike
measures every active input, reports plain-language findings per input (Dead, Low, OK, Hot, Clipping, DC offset, Hum suspected, Possible
polarity mismatch), proposes a trim that brings the programme to a target level plus a high-pass suggestion, and applies the proposal on
request, all or per input, as undoable edits. A *Quick start* picks a wiring template for the detected channel count and sets sane defaults.

The panel is a normal section of the Inputs page (never a modal dialog): meters, mute and the rest of the application keep working while it listens.
Nothing is changed unless the operator presses an *Apply* button.

## 2. Probe (audio thread) - `crowdmike::linecheck::ProbeBank`

* Reads the **raw host input** of every input (after virtual-soundcheck substitution, before trim/polarity/any processing), so results
  do not depend on the current trim and the proposed trim is an absolute value.
* Disarmed (the default) the cost is one relaxed atomic load per block. Armed, it costs O(inputs x samples) with a handful of multiplies per sample.
* Accumulates fixed **frames of 100 ms** (`N = round(0.1 * sampleRate)` samples, identical frame boundaries for all inputs). A finished frame
  per input is pushed into a single-producer/single-consumer ring (capacity 256 frames per input, 25.6 s); a full ring drops the *new* frame
  and counts it (`dropped`). No allocation, locks, logging or IO on the audio thread; everything is sized in `prepare()`.
* `Frame` (plain data):

| Field | Meaning |
|---|---|
| `n` | samples in the frame (N) |
| `peak` | max `|x|` |
| `sum`, `sumSq` | `Σx`, `Σx²` (double precision accumulation) |
| `cross` | `Σ x_i * x_(i+1)` with the next input (0 for the last input) |
| `clip` | count of samples with `|x| >= 0.999` |
| `hum[8]` | squared amplitude `A² = 4|X(f)|² / N²` of the Goertzel bins at 50, 100, 150, 200, 60, 120, 180, 240 Hz |
| `generation` | arming generation (stale frames are discarded) |

* Frame length 100 ms makes `k = f * N / sr = f / 10` an exact integer for every sample rate in {22.05..192 kHz} (`N/sr = 0.1`), so the rectangular-window Goertzel bins fall exactly on the hum harmonics; the hum power in a frame is `A²/2`.
* `start()` (message thread) bumps the generation and arms; the audio thread resets its accumulators when it sees a new generation.
  `stop()` disarms. Frames with a different generation are ignored by the analyser.

## 3. Analyser (message thread) - `crowdmike::linecheck::Analyser`

Pure computation over the frames of one listening run (testable without audio). Listening time is `kListenSeconds = 10` (100 frames);
`Analyser::analyse()` also works with any frame count >= 1 (fewer frames only mean less certainty; the UI requires >= 20 frames, i.e. 2 s,
before it lets the operator stop early).

Per input, with `F` frames, `n_k` samples in frame k:

* `peak = max peak_k`, `peakDb = 20 log10(peak)`; `clips = Σ clip_k`.
* `mean = Σ sum_k / Σ n_k`; `dcDb = 20 log10(|mean|)`.
* `acMs_k = max(0, sumSq_k / n_k - (sum_k / n_k)²)` (AC mean-square of frame k).
* **Programme level** `programRmsDb = 10 log10(mean of the loudest ceil(0.3 F) values of acMs_k)`. Taking the loudest 30 % of frames makes the
  level independent of pauses between claps or phrases.
* **Noise floor estimate** `noiseFloorDb = 10 log10(acMs sorted ascending at index floor(0.1 (F-1)))` (10th percentile frame).
* **Hum**: take the quietest `ceil(0.3 F)` frames by `acMs_k`. For family 50 Hz (bins 0..3) and family 60 Hz (bins 4..7) compute the mean over
  those frames of `humMs = Σ_bins A²/2`, and `humRatio = humMs / mean(acMs_k)` (+1e-20). The stronger family is reported.

### 3.1 Verdicts (thresholds)

Evaluated in this order; the first match is the **primary verdict**, the remaining items are independent **flags**.

| Verdict | Rule |
|---|---|
| Dead | `programRmsDb < -75 dBFS` **and** `max(0, peak - |mean|) <= 0.001` (-60 dBFS). A constant DC with no AC is Dead + DC. |
| Clipping | `clips >= 1` (a sample at or above 0.999 full scale) |
| Hot | `peak >= 0.7079` (-3 dBFS) **or** `programRmsDb > -10 dBFS` |
| Low | `programRmsDb < -34 dBFS` (more than 14 dB below the -20 dB target) |
| OK | otherwise (`-34 <= programRmsDb <= -10`, peak below -3 dBFS) |

| Flag | Rule |
|---|---|
| DC offset | `|mean| >= 0.01` (-40 dBFS) |
| Hum suspected | `humRatio >= 0.30` **and** `10 log10(humMs) >= -70 dBFS` (hum rms), reports 50 or 60 Hz |
| Polarity mismatch | see 3.2 |

### 3.2 Polarity mismatch vs neighbour

For adjacent inputs `i`, `i+1` (never across a Dead input): `corr = Σ cross_i / sqrt(Σ sumSq_i * Σ sumSq_(i+1))` over all frames.
The *effective* correlation includes the polarity switches already set: `corrEff = corr * (pol_i != pol_(i+1) ? -1 : 1)`.
Flag input `i+1` (message "Possible polarity mismatch vs input i") when **all** hold: `corrEff <= -0.5`, both programme levels
`>= -50 dBFS`, and `|programRmsDb_i - programRmsDb_(i+1)| <= 12 dB`. Independent sources give |corr| of a few percent (< 0.2 for >= 2 s of
signal), so the threshold is far from random noise; it can still fire for two microphones that capture the same source from opposite
sides, which is exactly the case where a flip is wanted. The proposal only *suggests* the flip.

### 3.3 Trim proposal

Constants: `kTargetRmsDb = -20` (default, operator may change), `kTrimClampDb = 24`, `kPeakCeilingDb = -1`.

```
ideal      = target - programRmsDb
headroom   = kPeakCeilingDb - peakDb                // trim at which the loudest peak would sit at -1 dBFS
proposed   = min(ideal, headroom)
if clips > 0: proposed = min(proposed, currentTrimDb)  // never raise a clipping input
proposedTrimDb = clamp(proposed, -24, +24)
noChange   = |proposedTrimDb - currentTrimDb| < 0.5
```

`limitedByHeadroom` is set when `headroom < ideal`. Dead inputs get no proposal (`hasTrim = false`). Because the probe sees the pre-trim signal the
proposal is absolute (it replaces the trim), never relative. A proposal only changes the trim parameter (`inputN.trimDb`), range -60..+24.

### 3.4 High-pass suggestion

For every non-Dead input: `suggestHpOn = true`, `suggestHpHz = humSuspected ? 150 : 100`. `noChange` when the input already has the HPF on at a frequency
`>= suggestion`. (A high-pass attenuates the 50/60 Hz fundamental but cannot cure hum: the text recommends checking ground/DI.)

## 4. Apply and undo - `linecheck::applyProposals`

```cpp
struct ApplyTarget { juce::UndoManager& undo; std::function<void(const juce::String& id, float actual)> setParam; };
void applyProposals(ApplyTarget&, const std::vector<Result>&, const std::vector<int>& inputs, bool includeTrim, bool includeHpf, bool includePolarity);
```

* One call = one undo transaction (`UndoManager::beginNewTransaction("Line check: apply")`), so *Apply all* is one undo step and *Apply* on a single
  row is one undo step. `CrowdMikeAudioProcessor::getUndoManager().undo()` restores every trim/HPF/polarity value. Parameters change through the APVTS
  (host automation sees them as normal edits).
* Trim: `inputN.trimDb = proposedTrimDb` (skipped when `noChange` or Dead). HPF: `highPassEnabled = 1`, `highPassHz = suggestHpHz`. Polarity (only when
  `includePolarity`, which the UI sets only from the explicit *Flip polarity* button): `polarity = !current`.
* Clipping inputs are never raised (rule in 3.3, re-checked at apply time against the live trim).
* Non-active inputs and invalid indices are ignored.

## 5. Quick start

`linecheck::chooseTemplate(inputs, outputs)` returns a `WiringTemplates::Id` index:

| Condition | Template |
|---|---|
| 1 input | 1 (One mic > stereo PA) |
| 2 inputs | 2 (Two mic stereo crowd > stereo PA) |
| 3..4 inputs | 3 (Four mic room) |
| >= 8 inputs and >= 8 outputs | 8 (Venue multi-zone) |
| 5+ inputs and >= 4 outputs | 4 (Front/rear crowd > PA + broadcast) |
| otherwise | 3 |

`CrowdMikeAudioProcessor::applyWiringTemplate()` is called (it begins its own undo transaction for the parameter changes); then, in the same transaction,
every active input gets `highPassEnabled = 1`, `highPassHz = 100` if its HPF was off. Trims are untouched (use Line Check for levels). The panel then
suggests "Run Line Check" next. The routing change is not part of the UndoManager (as for the existing template command).

## 6. UI (`ui::LineCheckPanel`, Inputs page)

* Toggle button `Line Check` in the Inputs page top bar opens/closes a card at the bottom of the page (it can be open together with *Advanced*).
* States: **Idle** (`Start 10 s listen`, `Quick start...`), **Listening** (progress bar, live elapsed text, `Cancel`; inputs keep working),
  **Results**, **Applied** (`Undo` enabled).
* Results table, one row per active input: `#`, name, verdict badge (glyph + text), programme dBFS, peak dBFS, noise floor dBFS, flags text
  ("DC offset -35 dB", "Hum 50 Hz", "Polarity vs 1"), `trim now -> proposed`, HPF suggestion, `Apply` (per input) and `Flip polarity` (only when flagged).
  Header buttons: `Apply all`, `Undo`, `Re-run`.
* Target level box (default -20 dBFS, -30..-10).
* Accessibility: verdicts always shown as text; component IDs `linecheck-*` for tests.

## 7. Real-time and edge cases

* Probe memory: 16 inputs x 256 frames x ~72 bytes ~ 300 kB, allocated in `prepare()`.
* Layout change (re-prepare) disarms the probe and clears the rings.
* Sample-rate range 22.05..192 kHz, block sizes 1..2048 (frames are independent of the block size; results are bit-identical for different block sizes).
* Non-finite samples count as 0 and do not corrupt statistics.
* Inputs beyond the active count are never probed.
* Fewer than 1 frame: analyser returns Dead/no proposal for every input and sets `valid = false`.

## 8. Acceptance tests (`tests/engine/LineCheckTest.cpp`)

1. `linecheck_dead_input`: zero signal -> Dead, no trim proposal.
2. `linecheck_ok_input_trim_to_target`: -26 dBFS rms noise -> OK, `proposedTrimDb = +6 +-0.3`; -45 dBFS -> Low and the proposal clamps at +24 when needed.
3. `linecheck_hot_and_clipping`: 0.8 peak -> Hot (proposal negative); hard-clipped to 1.0 -> Clipping, proposal <= 0 and <= current trim; never raised.
4. `linecheck_headroom_limits_trim`: peaky signal (rms -30, peak -6) is limited so peak lands at -1 dBFS (`limitedByHeadroom`).
5. `linecheck_dc_offset`: 0.05 DC + noise -> DC offset flag with `dcDb` about -26.
6. `linecheck_hum_50_and_60`: quiet 50 Hz hum (-40 dBFS) and 60 Hz hum under speech-like noise -> Hum suspected with the right family; plain noise -> no hum flag.
7. `linecheck_polarity_pair`: noise on input 1, its inverse on input 2 -> flag on input 2; same with `polarityInverted[1] = true` -> no flag; independent noise -> no flag.
8. `linecheck_probe_block_size_independent`: identical results for block sizes 1, 64, 2048 and at 44.1/48/96 kHz; disarmed probe produces no frames.
9. `linecheck_probe_ring_overflow_counts_drops`.
10. `linecheck_apply_and_undo`: apply all through the processor: trims and HPFs set; one `undo()` restores them; per-input apply touches only that input; clipping inputs not raised.
11. `linecheck_engine_wiring`: running the engine with the probe armed produces frames from `raw` (pre-trim): trim change does not change the measured level.
12. `linecheck_quick_start_template_choice` and `applies defaults`.
13. UI: `ui_all_pages_render_without_error` renders the Inputs page with the Line Check panel open (listening and results states).
