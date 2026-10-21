# Audit part B: control, recording, playback, report

Branch `feat/audit-b`, cut from `main` at 6b58610 (the worker rules mention a70e0d0, which is not what main points at). Scope: RECORDING_SPEC (incl. 9.1, 9.2, 14-16), CONTROL_SPEC 3-6 and 8-10, BEAT_SYNC_SPEC, SHOW_REPORT_SPEC, LINE_CHECK_SPEC (UI/undo side), TESTING_SPEC.

Status words: **verified** (complete and covered by a test), **untested** (implemented, no direct test), **partial**, **stub**, **missing**.

Test results after the fixes (Linux, Release): `crowdmike_dsp_tests` 170 pass, `crowdmike_recording_tests` 40 pass (was 39), `crowdmike_tests` 132 pass under xvfb (was 129). Nothing weakened or skipped.

## Matrix

| Area | Status | Evidence |
|---|---|---|
| MIDI CC/note/pitch bend binding, learn, audio-thread FIFO to message-thread pump | verified | `Source/Control/ControlHub.cpp` (`handleMidi`, `pump`), `tests/engine/CoreTest.cpp:111-141` |
| OSC resolver: params by id/name, `/param`, `/paramnorm`, scene, record, clip, macro, panic, tempo, scope | verified | `Source/Control/OscAddress.cpp:61-139`, `BeatSyncEngineTest.cpp` |
| OSC/MIDI for tempo tap, resync, source, quantise, nudge, barBeats | verified | `OscAddress.cpp:98-112`, `ControlHub.cpp:153-154` |
| MIDI/OSC for ducker, reverb/multi triggers, scope params | partial | only as `Param` bindings (every param is learnable from its widget menu, `Widgets.cpp:44`) and `/crowdmike/param/<id>`; no friendly aliases. Fine, listed P3 |
| MIDI/OSC for report export | was missing, fixed | `BindTarget::ReportExport`, `/crowdmike/report/export` (`ShowModel.h`, `OscAddress.cpp`, `ControlHub.cpp`), test `auditB_report_export_reachable_by_osc_and_midi` |
| MIDI learn for record/scene/tempo/report actions | was partial, fixed | Only scenes and clips had a learn button, so record start/stop/marker/split, next/prev scene, tempo tap/resync could not be bound from the GUI even though the targets existed. New "Learn action..." menu in `SettingsSections.cpp` |
| MIDI/OSC for line check start, monitor on/off/pair | missing | line check is a GUI workflow (needs a hardware pass and a confirm step); monitor settings only via GUI/params. P2 |
| Scene capture/recall, scope bits, locks, ramp | verified | `Source/Core/SceneBank.cpp:74-114`, `CoreTest.cpp:163-` |
| Scene recall republishes macros to the audio thread | was a bug, fixed | MIDI/OSC/host-button recalls never reached `showModelChanged()`, so recalled macro assignments stayed stale on the audio thread. `PluginProcessor.cpp` `scenes.onRecalled` now calls `bank.publishMacros`. Test `auditB_scene_recall_republishes_macros` |
| Empty recall scope never accepted | was partial, fixed | hub refused it (`ControlHub::setRecallScope`) but the Scenes page toggles and a hand-edited/old state could set 0. `ScenesPage.cpp` `writeScope` reverts; `ShowModel::fromValueTree` falls back to all. Test `auditB_loaded_empty_recall_scope_falls_back_to_all` |
| Undo of line-check apply (single transaction) | verified | `Source/Engine/LineCheckApply.cpp`, `LineCheckPanel.cpp` apply/undo, existing line check tests |
| Autosave / crash recovery (30 s autosave, `running.flag`, `lastsession.cmstate`) | untested | `Source/Standalone/DeviceController.cpp:163,280-304`. New settings (monitor, report, scope, tempo) ride in the `Show` child and APVTS so they are included. No automated test (needs the standalone device layer). P2 |
| State schema v2 compat with the new settings | verified | `StateIO.cpp` migration tests; ShowModel loads tolerate missing nodes; the BindTarget clamp now allows the appended value |
| Recorder taps cover FX returns (slots 6/7 via FxExtra 55/56) | verified | `Source/Recording/TapMap.h`, `CrowdEngine.cpp` `pushTaps`, recorder tap integration test |
| Recorder: FIFOs, writer thread, pre-roll, split, WAV 4 GB cap, FLAC24, disk guard | verified | `RecorderEngine.cpp`, `tests/recording/RecorderTest.cpp`, `RecorderExtrasTest.cpp` |
| Transport-record trigger | verified | `TransportRecordTrigger.h`, polled at 20 Hz from the processor timer, tests in `RecorderExtrasTest.cpp` |
| Monitor bus (cue pair) and feedback guard | verified | `MonitorBus`, `CrowdEngine::mixMonitor` |
| Streaming playback: resident vs streamed, head pre-buffer, loop, reverse, underrun counting | verified | `PlaybackEngine.cpp`, `StreamPool.cpp`, `tests/recording/StreamingTest.cpp` |
| Streaming read error / pool exhaustion shown to the operator | was partial, fixed | `readErrors` and `starvedAcquires` were counted (`StreamPool.cpp:97,116,139,170`) but the Playback page only showed underruns, so a vanished file played silence with no warning. Now shown (`PlaybackPage.cpp`). UI only, no unit test (page render tests cover construction) |
| Quantiser vs clip tempoSync double-quantise | was a bug (one direction), fixed | When the engine quantiser fires, the clip must not wait again (kept). When it *passed through* (stopped transport, no usable tempo) the engine still marked the trigger as already quantised, so a synced clip lost its free-running grid from 9.2. `triggerFromAudioThread(..., alreadyQuantised)`. Test `TempoSync_EngineHandOffDoesNotDoubleQuantiseButPassthroughKeepsClipSync` |
| Tap tempo accuracy | was partial, fixed for the GUI | GUI TAP went through the `tempo.tap` parameter, polled at 20 Hz: up to 50 ms of jitter and short clicks could be lost. Now `TempoWidget.cpp` calls `tempoTap()` on mouse-down. MIDI/OSC taps already went direct. The host-automation path (`tempo.tap` param) is still polled, which is fine for a button |
| BEAT_SYNC: sources, auto, MIDI clock, resync, quantise for echo/reverse/clips | verified | `TempoHub`, `TriggerQuantiser.h`, `BeatSyncEngineTest.cpp`, `TempoSyncTest.cpp` |
| Show report: windows, markers, peaks, writers HTML/CSV/JSON, shutdown export | verified | `Source/Report/ShowReportService.cpp`, report tests |
| Report events vs recorder markers | verified | auto markers go through `addMarkerAt` with the same clock; "Crowd peak" appears in both |
| Report export from controllers | was missing, fixed | see above |
| Line check: probe bank, analyser, proposals | verified | LineCheck tests |
| TESTING_SPEC soak coverage of newer modules | was partial, fixed | `enableEverything` only opened 5 FX; now also sends 6/7 (inputs and groups), reverb, multiplier, ducker, tempo Manual + quantiser, reverb pre-delay sync, plus reverb freeze/tail kill and tap automation in `wiggle`. See run note below |
| Audio-thread safety of new code | verified by reading | no locks, allocation or IO in the new FX, quantiser, monitor and tap paths. The only changes this audit made on the audio side are an extra bool on an SPSC command |

## Fixes made (one commit each, in order)

1. Republish macros after any scene recall.
2. Report export binding and OSC address, "Learn action..." menu, empty recall scope refused (GUI and load). The BindTarget enum only grew at the end, and the load clamp moved with it, so old shows load unchanged.
3. Clip tempo sync kept when the global quantiser passes through.
4. Playback page shows stream read errors and pool exhaustion; GUI tap goes straight to the tempo hub.
5. Soak helper covers the newer modules; tempo sync hand-off test.

Docs touched: `docs/CONTROL_SPEC.md` (one line for the new binding target and address). No parameter IDs changed, no file format changed, no migration needed.

## Remaining gaps

| Pri | Gap |
|---|---|
| P1 | None found that loses audio or data. |
| P2 | No MIDI/OSC action for starting a line check or toggling the monitor bus (only through params/GUI). |
| P2 | Autosave/crash recovery has no automated test; it lives in the standalone-only device layer. |
| P2 | The soak run with the new modules was only run shortened (see note), not for the nightly 10 minutes. |
| P3 | MIDI events that overflow the 512-slot audio-to-message FIFO are dropped silently (no counter). |
| P3 | `RecorderEngine::getLastSessionFile` takes `writerLock`, so a message-thread call can wait behind a slow disk write. Used rarely, from the UI. |
| P3 | `undoLastApply` for the line check does not confirm the top undo transaction is its own; a user edit between apply and undo would be undone instead. |
| P3 | The Playback tap records only the direct playback bus (route 9). Documented behaviour, but worth a spec sentence. |
| P3 | No short OSC aliases for ducker, reverb and multiplier parameters; `/crowdmike/param/<id>` works. |
| P3 | Commit 11b2b44 references the report export action before the enum exists in the same commit, so that single commit does not build on its own (the branch tip does). |

## Run notes

The soak target is not part of the three required targets. I built it and ran it with `CROWDMIKE_SOAK_STRESS_SECONDS=20 CROWDMIKE_SOAK_LONG_SECONDS=60`; all 5 soak tests passed with the new modules enabled: 0 audio-thread allocations at 48 and 96 kHz (8.1x and 3.6x realtime for 16x16), and the 60 s long run grew by 1 outstanding allocation with flat RSS (114 MiB).
