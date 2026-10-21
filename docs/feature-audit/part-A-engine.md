# Feature audit part A: DSP, engine, parameter and state layer

Branch `feat/audit-a`. Scope: ENGINE_SPEC, EFFECTS_SPEC (incl. 19-20 Stadium Reverb / Crowd Multiplier), ROUTING_SPEC, IO_SPEC,
CONTROL_SPEC 1-3 and 7-10, DUCKER_SPEC, LINE_CHECK_SPEC, BEAT_SYNC_SPEC (engine side). Parameter IDs are unchanged (nothing added or renamed).

Status legend: verified complete / implemented untested / partial / stub / missing. Evidence is file:line at the audited commit.

## Feature matrix

| Feature | Status | Evidence |
|---|---|---|
| 7 parallel FX slots (kNumFx=7, 5 legacy) in sends, returns, smoothers, duck buffers | verified complete | `Source/Engine/CrowdEngine.cpp:36-47` (buffers sized by kNumFx, duckBuf `2*kMaxGroups+2*kNumFx`), `Source/Engine/FxRack.cpp:66` |
| Send slots 6/7 map to appended Fx6/Fx7 params (IDs frozen) | verified complete | `Source/Core/ParamTables.h` inputSendParam/groupSendParam; test `audit_fx_slot_tables_are_consistent_for_all_seven_slots` |
| Stadium Reverb (FX 6), Crowd Multiplier (FX 7) DSP wired into rack | verified complete | `Source/Engine/FxRack.cpp`, existing reverb/multi DSP tests |
| Reverb freeze / tail-kill under panic | fixed (was partial) | `Source/Engine/FxRack.cpp` panicKillReverb; test `audit_panic_kills_a_frozen_reverb_tail_so_it_cannot_resurface` |
| Routing bus rows 30..33 (FX 6/7 returns), `busFx2` persistence, validator nodes | verified complete | `Source/Routing/*`, golden 5-FX state test, kBusSrcCount=34 |
| Recorder taps 55/56 (TapMap::FxExtra), names "Reverb Return" / "Multiplier Return" | verified complete | `Source/Engine/TapMap.h`; ducker test checks the taps stay pre-duck |
| Ducker covers groups and FX 6/7 returns, look-ahead delay, target modes | verified complete | `CrowdEngine.cpp` applyDucker; test `audit_ducker_ducks_the_new_fx_returns_and_keeps_their_recorder_taps_clean` and `tests/engine/DuckerEngineTest.cpp` |
| Ducker look-ahead in reported plug-in latency | partial (by spec) | not reported; documented in DUCKER_SPEC 4.4 |
| Tempo hub -> engine BlockInfo -> FX context / quantiser / playback tempoSync | verified complete (after fix) | `CrowdEngine.cpp:351-357, 385, 503`; test `audit_beat_synced_fx_see_the_same_ppq_for_oversized_host_blocks` fails without the fix |
| Quantiser vs playback tempoSync (no double quantisation) | verified complete | `immediate` flag in the triggerClip / requestClipTrigger path |
| Global Playback Level | fixed (was stub: slider with no DSP read) | `CrowdEngine.cpp:386-402`; test `audit_playback_level_scales_clips_on_every_destination` |
| Feedback-risk scoring through FX sends | fixed (was partial: only delay/tape boost) | `Source/Engine/InputRisk.cpp` fxLevels loop; test `audit_feedback_risk_follows_input_sends_into_fx_returns` |
| State restore without `<Scenes>` | fixed (target instance scenes leaked) | `Source/Core/StateIO.cpp` restore; test `audit_state_without_scenes_clears_the_scenes_of_the_target_instance` |
| Schema v1 -> v2 -> current migration, fxOrder 5 -> 7 | verified complete | `StateIO.cpp`, migration and golden tests |
| Scenes: transient params (reverb Freeze/TailKill), scope bits | verified complete | `Source/Core/SceneBank.*` |
| Presets for reverb and multiplier | verified complete | `Source/Core/PresetManager.cpp`, PresetBrowser |
| OSC `.tailKill` momentary; ParamPicker entries for reverb/multi | verified complete | `Source/Control/OscAddress.cpp`, `Source/UI/ParamPicker.cpp` |
| Panic / bypass / rehearsal interplay with new modules | verified complete (after fix) | panic zeroes outputs and now also kills the reverb tail; bypass path is FX-independent |
| Plug-in bus layouts 1..16 ch, main bus only | partial | `Source/PluginProcessor.cpp` isBusesLayoutSupported; no aux buses, no AU |
| Latency reporting (look-ahead limiter, output delay) | verified complete | `meters.latencySamples`, processor tests |
| Line check, soundcheck, DensityToSend in engine | verified complete | `CrowdEngine.cpp:407-409` |
| Meters for FX 6/7 returns | verified complete | MatrixPage display order, EngineMeters sized by kNumFx |
| `Source/Engine/MeterBank.h` | partial | included by a single file; possibly dead code |

## Fixes made (tests in `tests/engine/AuditAEngineTest.cpp`)

1. `GlobalP::PlaybackLevel` was defined and shown as a slider (PlaybackPage, MatrixPage) but nothing in DSP read it. The engine now applies it as a smoothed gain over all 10 playback buses.
2. FX context ppq used the host-block ppq and ignored the chunk offset for host blocks larger than the prepared block size, so beat-synced FX drifted. It now uses the chunk-accurate `qt.ppq`.
3. Panic did not silence a frozen or long Stadium Reverb tail: it could return at full level on release. Panic now forces TailKill on the reverb.
4. Feedback-risk analysis ignored input sends into the 7 FX slots. Open sends to running effects now count as mic-to-speaker paths (via the return bus row or the master).
5. `StateIO::restore` of a state with no `<Scenes>` child left the target instance's scenes in place. It now resets them.

## Remaining gaps

| Pri | Gap |
|---|---|
| P1 | No DAW / pluginval / device runs for the new modules (host-block-size dependence, multichannel layouts). |
| P2 | UI exposure: GlobalP Rehearsal, UnlockLimiters, OutLimRelease, DensityToSend, RecordAll, SceneNext/Prev and ducker Range, KeyHpf, KeyLpf, Lookahead have no dedicated widget (reachable only through ParamPicker, MIDI, OSC or host automation). |
| P2 | Parameter smoothing (including `tempo.manualBpm` feeding the TempoHub) advances per host block, so the moments after a parameter change depend on host block size. Output stays correct but is not bit-identical across block sizes. |
| P3 | Ducker look-ahead not added to reported latency (spec-documented). |
| P3 | `getTailLengthSeconds` ignores the Crowd Multiplier tail. |
| P3 | Plug-in has one main bus (no aux buses, no AU). |
| P3 | `MeterBank.h` appears unused; some OSC aliases are unmapped. |
