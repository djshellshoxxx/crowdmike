# M8 cloud hardening and full code audit — 2026-10-30

Baseline: `1cf0a3da5cae6ed6bac49da56a2a3fac5f89674f`. Date is America/Los_Angeles (2026-10-31 UTC).

The roadmap marks M0–M7 complete and identifies **M8 Beta** as the next milestone. This work completes the available Linux cloud build, validation, hardening and repository audit. The user explicitly chose cloud checks with real hardware/DAW checks pending. **M8 release approval remains open:** the exact release commit needs the native CI/release matrix, real interface/host validation, and resolution of the two P1 audit findings below.

## Verified result

| Check | Observed result |
|---|---|
| Release build | All seven targets built: Standalone, VST3, CLAP, DSP, processor/UI, recording and soak executables |
| Fast CTest | 3/3 executables passed: **170 DSP + 175 processor/UI + 50 recording = 395 cases** |
| Windows-stack reproduction | Original scene-state test crashed at 1 MiB; final entire 175-case processor/UI suite passed with `ulimit -s 1024` |
| Full default soak | 4/4 CTest cases passed in 159.40 seconds; durations were unchanged: stress 120 audio-seconds at each of 48/96 kHz, long run 600 seconds, 300 reconnect cycles, recording/disk-full 300 seconds |
| Release-script regressions | 4/4 standard-library Python tests passed |
| VST3 pluginval | Strictness 5 passed on the final Linux VST3; separate Steinberg validator unavailable |
| CLAP validator | **44 run, 32 passed, 0 failed, 1 warning, 11 skipped**; all three state reproducibility tests passed |
| Snapshot concurrency | Deterministic held-snapshot and 100,000-publication coherence tests passed; the isolated map exchange tests also passed ThreadSanitizer |
| Packaging | Linux archive inventory and hashes verified; packaged Standalone/VST3/CLAP match final build binaries, runtime dependencies resolve in the activated environment |
| Standalone smoke | Packaged application remained running until the intentional eight-second Xvfb timeout; absent ALSA sequencer device warning observed |
| Environment tooling | Bootstrap Bash syntax and read-only existing-prefix checks passed; recreation install path documented, not rerun from an empty prefix |
| Independent review | Introduced streamed-restart regression was caught, reproduced and fixed; final review found no remaining introduced material defect |

CLAP's warning is the pinned wrapper accepting nonempty random state bytes because JUCE's state-load API returns void. Unsupported extension checks account for the skips. This warning is recorded, rather than represented as 44 passing tests. Initial CLAP output was misread as parameter data loss: a public-host probe proved **all 2,069 parameter values restored correctly**. The failure was missing `CLAP_PARAM_RESCAN_VALUES` notification after restore. The successful-restore host notification fixed all three validator failures without changing the state format or dependency pin.

The first full fast run exposed two documentation errors in the newly added environment guide (missing README index entry and ambiguous package-recreation wording). Both were corrected; the final fast and constrained-stack runs above passed. Assertions and test selection were retained.

## Native CI portability follow-up

The first native CI run for this branch built on macOS and Linux but failed one newly added test, `macro_snapshot_concurrent_publication_keeps_complete_mapping` (one failure among 175 processor cases on each platform). DSP and recording tests passed. The test combined a hard-coded exact `24.0f` endpoint assumption with the independent all-channel coherence check.

Two independent Linux probes reproduced the numerical failure, including one using the real JUCE `NormalisableRange`: contraction disabled produced exactly `24.0f`; fused multiply/add produced `23.999998092651367`. All sixteen channels remained exactly equal in that reproduction. JUCE's `0.01f` snapping interval is a binary approximation, so the fused result lies just below the upper clamp.

The test now captures each target's canonical endpoints through the public `RangedAudioParameter::convertFrom0to1` API before starting either thread. It separately asserts exact endpoint membership and exact equality across all sixteen channels. No epsilon, skipped assertion, iteration/load reduction or production change was introduced. An independent reviewer confirmed that stale/default/intermediate values still fail membership and mixed mapping generations still fail coherence. The corrected entire 175-case processor suite passed locally. Native rerun status is recorded by the PR checks; the earlier failing jobs are retained as evidence rather than counted as successful validation.

### Native Windows layout follow-up

The same initial native run completed all 175 Windows processor cases in 41.68 seconds without the original stack crash. Its only failure was the existing Record-page caption audit: `rec.autoAssign` measured 82.9247 pixels but its 86-pixel button provided only 82 pixels of audited caption space. This occurred in seven supported size/scale/contrast modes.

`RecordPage::resized()` now gives the three-button header row a 288-pixel minimum instead of 270. Equal weights and six-pixel gaps are retained, so every button grows from 86 to at least 92 pixels and no neighboring caption loses width. The available caption space becomes 88 pixels. Labels, actions, accessibility IDs and heights are unchanged. All five existing UI audit tests passed locally with their original checks. Independent review swept supported logical widths and compact modes and found positive metric space with valid button bounds. Native Windows rerun status remains in the PR checks.

After both native follow-ups, all seven targets were rebuilt and the full fast CTest suite passed all 395 cases in 62.71 seconds. Both validators, Linux packaging/hash/runtime checks and the eight-second startup smoke passed again. The full-soak result above covers the unchanged DSP/engine/recording paths; the subsequent changes were a test expectation and GUI width only.

## Changes completed

| Area | Result and permanent regression coverage |
|---|---|
| Windows CI crash | Four large scene-state fixtures moved to heap storage; compiler measured the original frame at 1,101,328 bytes, exceeding the default Windows stack. Existing assertions retained. |
| Panic/rehearsal | Output safety gain covers the dry/wet bypass blend; normal bypass and five mono/multichannel panic layouts are covered. |
| Deferred controls | Hub-owned weak lifetime guard cancels trigger releases after owner destruction; active release timing remains tested. |
| Macro/device snapshots | Shared bounded three-slot SPSC ownership transfer prevents a publisher overwriting a snapshot held by an audio callback. No callback locks, allocation or retry loops added. |
| Analysis lifecycle | Scoped non-audio suspension joins beat/feedback workers around prepare and preserves users; acquire/release cannot restart them mid-prepare. |
| Playback state | Restoring absent Bed/Soundcheck nodes or a missing session clears old state. Soundcheck unloading uses existing publication/retirement paths and generation invalidation. |
| Soundcheck limits | Reject negative/overflowing metadata and unrepresentable or over-budget assembled timelines before unsafe copies/allocations; valid long 64-bit metadata remains representable. |
| Streaming playback | A voice uses only a head matching its region/reverse key; in-block synced restarts refresh cached head pointers; stopped-before-fill beds release their rings. |
| Host state | Successful restore announces parameter changes to the host; malformed/newer rejected state stays silent. |
| Release workflow | Correct nested validator archive extraction, honest individual validator outcomes, direct-tested CI gate, and mismatched orphan-tag rejection. |
| Native Record-page layout | Header buttons retain equal widths with enough space for the Windows Auto-assign caption; existing full UI audit retained. |
| Build metadata/setup | macOS microphone usage metadata supplied; rootless Debian cloud setup script and development guide added. Native permission behavior still needs macOS validation. |

No parameter IDs, enum ordering, plug-in identity, state schema or preset schema changed. Public existing entry points were retained. PR #8 (`docs/readme-and-wiki`) and its branch were left intact; the README received only the environment-guide index entry required by the repository's documentation test.

## Remaining P1 findings

1. **Preset dialog lifetime — `Source/UI/EditKit.cpp:199–235`.** The static file chooser and popup callbacks retain the processor by reference. The captured safe anchor is not checked or passed into nested chooser callbacks. Closing the plug-in while a chooser is pending can leave a callback targeting a destroyed processor. Guard every callback with a live editor/anchor and give chooser lifetime an editor owner; test close-with-dialog-pending before shipping.
2. **Standalone MIDI real-time safety — `Source/Standalone/DeviceController.cpp:127` and JUCE `MidiMessageCollector`.** The audio callback calls a collector method that locks against the MIDI input thread; its initially unreserved MIDI buffer can also allocate. Replace this seam with a bounded callback-safe event hand-over, preserving multiple-input/timestamp semantics, and verify contention and allocation behavior.

These are pre-existing findings discovered by the full audit and remain open. This branch is a draft for review, not approval for live-show deployment.

**Follow-up (2026-11-11):** both findings are fixed in the follow-up change on top of this branch. (1) The preset dialog
is owned by the `EditorContext` (`UI/AsyncFileChooser.h`) and every menu/dialog callback is guarded by the anchor
component; the global chooser is gone (`tests/engine/PresetDialogLifetimeTest.cpp`). (2) The standalone replaces
`juce::MidiMessageCollector` with the bounded, lock-free `Standalone/MidiEventQueue.h`; the audio callback drains into a
pre-sized `MidiBuffer` and never locks or allocates (`tests/engine/MidiEventQueueTest.cpp`). The native host/dialog
reproduction and a real MIDI controller run remain manual checks.

## Other open findings

| Priority | Finding | Detailed evidence |
|---|---|---|
| P2 | Bypass re-entry can replay frozen latency-delay history | Engine E3 |
| P2 | Empty host blocks poison CPU telemetry | Engine E4 |
| P2 | Input pan steps in FX/pre-group paths; bypass routes omitted from advisory risk | Engine E5/E6 |
| P2 | Non-finite imported parameter/macro values reach raw/effective banks | Core C3 |
| P2 | Ignored long MIDI/SysEx messages allocate before filtering | Core C4 |
| P2 | Held shortcuts can latch on text focus/editor close | Wrapper W4 |
| P2 | Explicit unassigned bindings reconnect on restart | Wrapper W6 |
| P2 | Inputs REC arming mismatch; group-name number prefix; rejected show schemas reported loaded | Wrapper W7/W8/W9 |
| P2 | Energy duration/averages after decimation; capped FX totals; Tracking-off suppresses auto-export | Report R1/R2/R3 |
| P2 | Failed asset-preservation download can yield incomplete release checksums | Build finding 7 |
| P2 | Generic version bumper does not read CrowdMike's version sources; Linux installer can claim success for missing payload | Build findings 8/9 |
| P3 | Width bypass passes non-finite input; inherited starter tools mismatch project; updater doctor exit status | Engine E7; build 10/11 |
| P3 | Setup recreation should choose one cached package version; lexical extraction of multiple cached versions can overwrite the intended version | Final setup review; current single-version cache/fresh prefix unaffected |

Device-map exchange has an explicit SPSC contract: normal publication is from the control thread, with device-start publication only while the other producer/consumer are stopped. Automatic backend reprepare overlapping a binding edit is a separate lifecycle caveat requiring backend validation or producer-side serialization. Host-state restore threading also inherits the existing message-thread model contract.

## Scope and limits

Independent passes read Core/Control/Report, DSP/Engine/Routing, Recording/Playback, UI/processor/Standalone, CMake/CI/installers and all shared scripts, with corresponding specifications/tests. Fresh review then examined the combined diff, including new files. The appendices retain exact locations, reproductions, proposed remedies, rejected hypotheses and testing limits. Their investigation-time pending notices are superseded by the consolidated verification above.

No real audio interface, acoustic loop, DAW, screen reader, Windows/macOS executable/installer, or old OS deployment target was exercised in this Linux executor. Native CI and packaging are required for the release SHA; PR checks are the live record for this change. Signing, notarization, ASIO, AU and auxiliary buses retain their documented beta exclusions. Installed base system files were unchanged; local packages/config/caches live under `/workspace/work/crowdmike-env`.

The local validation archive is `CrowdMike-0.3.0-cloud-validation-Linux-x86_64.tar.gz`, 22,291,736 bytes; SHA256 `f4dc3a85dba3103fb06b5642065c044a30943ac7e9124b39ea979c1e8436102a`. It was staged for inspection, not published as a beta. Installer help was checked without installing into the user's home. Scratch probes/logs are session-local; permanent regression cases are committed under `tests/`.

Sparse legacy-state defaults were specifically investigated and **refuted as a bug** by a reused-processor regression. The ordinary streamed-bed fade leak candidate was also rejected before a distinct stop-before-fill leak was reproduced and fixed. A concurrency stress run not finding a race was not represented as proof of the old ownership scheme.

## Detailed audit and review appendices

---

# CrowdMike engine, DSP and routing audit — 2026-10-31

Baseline: `1cf0a3da5cae6ed6bac49da56a2a3fac5f89674f`; reviewed alongside the current M8 cloud-hardening work. Reviewer read `AGENTS.md`, `docs/MODULES.md`, engine/routing/effects/ducker/testing specs and relevant regression tests. Frozen parameter IDs, APIs and schema were treated as constraints. No refactor or identity/state migration is proposed.

## Findings

### E1 — P1 — Panic and rehearsal leave dry bypass audible

**Location:** `Source/Engine/CrowdEngine.cpp:647` (baseline). The output safety gain multiplies only the wet sample; dry bypass is mixed after that gain. At settled bypass (`b = 1`), panic/rehearsal output gain has no effect at all.

**Reproduction:** prepare any supported layout; feed nonzero input; settle `bypass = 1`; set `panic = 1` and process beyond its 5 ms ramp. Output remains the dry input. `rehearsal = 1` has the same error. This contradicts `DUCKER_SPEC.md` section 4.5 and the global Panic/Rehearsal contract. Tests previously exercised the two modes separately.

**Fix:** apply the existing output gain to the result of the dry/wet bypass blend. Keep recorder taps before this gain and retain the explicit cue/monitor behavior after it. This is an expression-placement change, with no API/ID/state change.

**Status:** fixed in the current working tree. Root confirmed the new tests RED before the production edit: all five panic layouts and rehearsal failed (six assertions across three tests), while normal dry bypass passed. `tests/engine/OutputSafetyTest.cpp` covers normal bypass, panic on/off in 1x1, 1x2, 2x2, 8x8 and 16x16, and rehearsal on/off in 8x8. GREEN rebuild/result pending at this update.

### E2 — P1 — Re-preparing Auto tempo can race the running beat-analysis worker

**Locations:** `Source/DSP/BeatTracker.cpp:26` and `:33`; integration at `Source/PluginProcessor.cpp:87` and `:537`.

**Evidence:** `BeatTracker::prepare()` replaces its unique_ptr FIFO and resets/assigns analysis buffers and plain analysis counters. `analyse()` accesses those same objects without mutual exclusion. The processor acquires `beatAnalysis` when Auto tempo is selected, but `prepareToPlay()` calls `engine.prepare()` / `beat.prepare()` without releasing or joining that worker. Audio callback suspension does not suspend this separate worker. FeedbackDetector, in contrast, serializes prepare/analyse with its work mutex.

**Reproduction path:** select Auto tempo, allow the processor timer to start the worker, then change device/sample rate/buffer configuration while analysis is running. A thread-sanitized stress test of prepare versus analyse should report the data race; a real host/device reconfiguration may crash or corrupt tempo state. The race is confirmed by ownership/control-flow inspection; an ad-hoc 5000-iteration ASan stress did not happen to catch a memory violation. No claim of a reproduced crash is made. The wrapper auditor independently corroborated the same lifecycle path and notified root.

**Fix:** stop/join the Auto worker before reinitializing its tracker and restore the held worker afterward, or serialize non-audio prepare/analyse inside the tracker. Never acquire that mutex from pushSamples/getEstimate. Add a lifecycle test that enables Auto analysis before repeated prepare.

### E3 — P2 — Re-entering bypass replays old delay history

**Location:** `Source/Engine/CrowdEngine.cpp:295` through `:299`; `Source/DSP/ChannelDelay.cpp:64` onward.

**Evidence:** the latency-alignment delay advances only while bypass is on or crossfading. When wet mode runs for a while, the delay ring is frozen. On the next bypass activation its first latency-length output contains audio from the previous bypass interval (or zeros on the first activation), rather than the immediately preceding live input. The dry/wet fade does not restore the missing/current history.

**Reproduction:** settle bypass while feeding +0.2; switch to wet and feed -0.2 for longer than all fades; re-enable bypass. The first approximately 72 samples at 48 kHz use +0.2 from the stale ring instead of -0.2. Compare against a continuously advanced reference delay. Existing engine bypass test checks settled DC only. This finding follows directly from ring progression; no runtime trace was captured in this pass.

**Fix:** continuously advance the short dry alignment delay, even at zero dry mix. Preserve the reported limiter latency and existing bypass mapping. Add a transition test with distinguishable old/new input.

### E4 — P2 — A zero-sample host block permanently poisons CPU telemetry

**Location:** `Source/Engine/CrowdEngine.cpp:131`–`:135`.

**Evidence/reproduction:** call process on a prepared engine with a zero-sample AudioBuffer. Its budget is zero; `used / budget` becomes infinity (or NaN), then cpuSmoothed stays nonfinite on every subsequent normal block. Such empty parameter/transport flush blocks are valid host behavior. This is a diagnostics fault, not an audio-buffer overrun.

**Fix:** return before timing division for empty blocks, or skip timing/overrun accounting when total is zero. Reset cpuSmoothed when preparing as a defensive lifecycle measure. Test an empty block followed by normal audio and assert finite CPU load and sensible block/overrun counts.

### E5 — P2 — Input pan changes hard-switch the FX send/pre-group paths

**Location:** `Source/Engine/CrowdEngine.cpp:416`–`:420`, `:457`–`:467`.

**Evidence:** group assignments use per-cell pan/gain smoothers, but FX send contributions multiply the new block's pan gains directly; only the send level is smoothed. The Reverse pre-group source also uses the new pan gains immediately. A pan automation step on sustained audio therefore steps these paths at a block boundary, contrary to ENGINE_SPEC section 4 (pan requires 10–30 ms smoothing).

**Reproduction:** open an input's FX send, use a steady tone, automate its pan from hard left to hard right between blocks; capture the FX send or delayed return and compare the first changed sample against the previous sample. Current routing/FX tests do not cover pan automation continuity. This is source-confirmed; no audio render was measured during this pass.

**Fix:** use shared per-input smoothed pan trajectories for both send channels and the pre-group mix, avoiding separate smoother advancement per effect. Keep linked-pair balance semantics and frozen parameter IDs.

### E6 — P2 — Feedback route risk ignores audible bypass routes

**Location:** `Source/Engine/InputRisk.cpp:37` and the route traversal below it; compare `CrowdEngine.cpp:648`.

**Evidence/reproduction:** clear all routing, mute input 1, then enable global bypass. The dry bypass still sends raw input 1 to output 1, but evaluateInputRisk returns immediately for the muted strip and reports no route. Even without mute, the cleared matrices report no route while bypass is audible. This can suppress an advisory warning during a live microphone-to-PA path. Panic/rehearsal gating should also be modeled consistently.

**Fix:** explicitly model global bypass's direct raw mapping before normal strip/matrix traversal; include global safety state when deciding audibility. Add a routing-risk integration test for bypass with muted/cleared wet routes.

### E7 — P3 — Settled WidthProcessor bypass violates nonfinite-input contract

**Location:** `Source/DSP/WidthProcessor.cpp:79`–`:81`.

**Evidence:** idle bypass returns without sanitizing either channel, whereas `docs/MODULES.md` says nonfinite input is treated as zero. A standalone compiled probe calling prepare(48000, 1), then process with nullptr parameters (defaults off), NaN left and Inf right prints `bypassed WidthProcessor finite left=0 right=0`. The active path sanitizes and its tests cover that case, but not idle bypass.

**Impact:** usually masked by upstream sanitization in CrowdMike's current engine chains. It remains a public DSP-module contract defect and a risk for later reuse.

**Fix:** clean each channel in the settled-off branch while preserving finite samples bit-for-bit. Add a bypass nonfinite regression. The scratch probe executable is `/tmp/crowdmike-width-audit`.

## Scope and coverage

Reviewed the format-neutral engine's complete process flow and lifecycle: input/raw/soundcheck taps; mono/stereo link and solo; group and FX sends; trigger quantisation and oversized blocks; ducker lookahead/copies; master/output chain; recording and monitor taps; global safety/bypass; meters/history; route persistence/validation/templates and risk scoring.

DSP source review covered filter coefficient design/cascades, EQ/slope/allpass interpolation, dynamics/gate/de-esser/transients/rider, delay/limiter, digital/tape/echo/reverse effects, noise/width, reverb/multiplier, loudness/correlation/history, feedback/spectrum FIFO/analysis and beat/tap/MIDI-clock timing. Reviewed corresponding test intent in engine/DSP suites and the soak allocation guard. This is a source audit with targeted probes, not a proof of every numerical parameter combination or a substitute for the root's build/test run.

Positive observations: production processing storage is preallocated; all inspected engine-owned views stay within the 16-channel capacities; tap IDs and appended FX rows remain compatible; final output channels are explicitly cleared; fixed routing topology is acyclic; output limiter latency remains constant; FX feedback/state bounds are explicit; feedback/spectrum locks are in worker/query paths, not their audio push paths; BeatTracker getEstimate retries are bounded. No new audio-thread file/network IO or obvious heap allocation was found in the inspected engine/DSP callback flow.

## Verification and unresolved limits

- Windows test-stack diagnosis is separate in `work/windows-stack-diagnosis.md`; compiler size/frame evidence is precise, with runtime reproduction owned by root.
- Root owns baseline and final CMake/CTest verification. This reviewer has not claimed those results while the build is still running.
- New OutputSafety tests were run RED by root before the production edit. GREEN rebuild/result is pending; update E1 after that result.
- ASan concurrency probe required `ASAN_OPTIONS=detect_leaks=0` because LeakSanitizer reports that it cannot operate under the execution environment's ptrace. The stress run completed without an ASan finding; it does not detect data races. TSan was not run.
- Real audio hardware, device drivers/reconnect, acoustic feedback, DAW load/automation behavior, Windows/macOS binaries and platform-specific lock-freedom remain unverified here.
- No claim is made that the existing historical feature-audit TODO list is current; later implementation/spec evidence supersedes several old items.


---

# Core, Control and Report audit

Scope: all `Source/Core/*` and `Source/Control/*`; corresponding Core, persistence, scene, MIDI/OSC, Beat Sync and golden-state tests; `docs/CONTROL_SPEC.md`, `docs/MODULES.md`, repository `AGENTS.md`, and relevant processor call sites. Report module review was delegated independently and is recorded in `work/audit-report.md` with its executable driver `work/report-repro.cpp`.

## Findings fixed in this working tree

### C1 — P1: Deferred trigger releases outlived their parameter owner

Original location: `Source/Control/ControlHub.cpp:53`–`:57`; class lifetime in `Source/Control/ControlHub.h`.

`pulseParam()` queued a `Timer::callAfterDelay()` callback holding only a raw APVTS parameter pointer. Destroying the hub/processor before that timer fired did not cancel the callback: `ControlHub` stopped only its own polling timer. In a plugin teardown the parameter is then destroyed, so the pending callback dereferences freed memory when the message thread next dispatches timers.

Deterministic regression: `tests/engine/ControlLifetimeTest.cpp`. A temporary independent hub pulses a still-live processor parameter, then is destroyed before pending timers are dispatched. Keeping the parameter alive exposes the forbidden post-destruction write without intentionally invoking undefined behaviour. The original implementation reset the parameter to zero after hub destruction; the test expected the value to remain one. A separate test verifies that a live hub still resets its pulse.

Observed RED: `./build/crowdmike_tests control_lifetime` failed exactly `control_lifetime_destroyed_hub_cancels_pending_pulse` (actual `0`, expected `1`); active pulse test passed. The minimum fix adds a hub-owned lifetime token and captures a weak token in the callback. Expired callbacks skip the parameter pointer. Parameter IDs, pulse timing and live release behavior remain unchanged. Integrated GREEN/full-suite verification belongs to the parent build after all agents' stable changes.

### C2 — P1: Four rotating macro slots lacked reader ownership

Original locations: `Source/Core/ParamBank.cpp:145`, `:173`–`:178`; previous slot members in `Source/Core/ParamBank.h`.

The audio callback retained a reference to the current slot for the entire macro application loop. The producer rotated through four slots without observing whether the audio callback had finished reading any slot. A legal schedule is: audio obtains slot 0 and pauses; producer publishes slots 1, 2, 3, then 0; producer overwrites slot 0 while the audio callback still owns its reference. The release/acquire active index does not synchronize those later writes with the outstanding reads. This violates the C++ data-race rule and the promised coherent per-block mapping. Multiple scene recalls in one MIDI pump can publish repeatedly.

Fix: `ParamBank` now uses `Core/SnapshotExchange.h<MacroSet>`, the same three-slot SPSC ownership exchange used for device maps. One message-thread producer publishes; `refresh()` acquires once per audio block; repeated producer updates cannot overwrite the consumer's held slot. A separate message-thread `latestMacros` value preserves the public `currentMacros()` API without making the producer borrow a consumer-owned slot. The contract is documented in `ParamBank.h` and the shared helper/module index.

The shared exchange has a deterministic held-snapshot regression and a 100,000-publication coherent-generation test in `tests/engine/DeviceMapSafetyTest.cpp`. `tests/engine/MacroSnapshotTest.cpp` adds a behavioral integration test: two configurations set all 16 input trims to either -60 or +24 dB, and no block may mix configurations. A scratch stress run against the old four-slot code (`work/macro-repro.cpp`) did **not** happen to expose the race; the ownership failure is demonstrated by the explicit schedule and deterministic exchange regression, not claimed as a failing stress reproduction. The same standalone APVTS-owner driver rebuilt against the fixed ParamBank completed successfully (`work/macro-repro-green`: `incoherent macro blocks=0`, exit 0).

## Confirmed findings remaining open

### C3 — P2: Non-finite imported parameter and macro values reach the effective audio parameter bank

Locations: `Source/Core/StateIO.cpp:88`–`:93`; `Source/Core/PresetManager.cpp:144`–`:151`; `Source/Core/ShowModel.cpp:189`–`:191`.

State restore forwards XML `PARAM` values directly to APVTS, and whole-show preset import converts supplied values without a finite check. Macro import applies `jlimit()` to numeric fields, but `jlimit()` does not remove NaN. JUCE parses `"nan"` as a floating NaN and its normalisable range conversion preserves it. These paths therefore admit invalid raw/effective parameters despite ordinary ControlHub and per-module preset updates rejecting non-finite values. Any downstream code converting such values to integer indices or filter coefficients cannot assume the public parameter range contract.

Executable evidence: `work/state-import-repro.cpp` uses a minimal AudioProcessor/APVTS owner and existing compiled Core/JUCE objects. Running `work/state-import-repro` produced:

```text
state nan master.levelDb=nan
show preset nan master.levelDb=nan
macro nan master effective=nan
```

The driver restores `<CrowdMikeState schemaVersion="2"><PARAM id="master.levelDb" value="nan"/></CrowdMikeState>`, applies a whole-show `<Params master.levelDb="nan"/>`, and loads a macro destination with `min="nan"`. No DSP output claim or crash was inferred from this parameter-bank reproduction.

Minimum fix: sanitize known imported parameter values before applying them, using each parameter's declared actual default for non-finite/malformed data; retain unknown IDs for compatibility where the current format does so. Give imported macro endpoints and curve finite defaults before range limiting. Cover state, whole-show preset and macro import separately, including valid negative values and unknown IDs. This was deliberately left audit-open by the parent scope decision.

### C4 — P2: Ignored long MIDI messages allocate on the audio callback

Locations: `Source/Control/ControlHub.cpp:11`–`:12`; `Source/PluginProcessor.cpp:117`–`:123`.

Both loops call `MidiMessageMetadata::getMessage()` before classifying a message. For a SysEx message larger than JUCE's inline `PackedData` storage (8 bytes on this build), the resulting `MidiMessage` invokes `std::malloc()` in `juce_MidiMessage.cpp:349`–`:355` and `std::free()` on destruction (`:343`–`:346`). The message is ultimately ignored, but a large SysEx therefore allocates/frees twice on the realtime callback. This contradicts repository AGENTS rule 6 and the ControlHub audio-thread contract.

Minimum fix: reject metadata whose `numBytes > 3` before constructing a MidiMessage in both loops. Every control and realtime-clock message handled here fits within three bytes, so unsupported SysEx can be skipped without changing supported control behavior. Preserve lightweight FIFO overflow behavior. This finding is established from the exact constructor/allocation call chain; no dropout timing claim or malloc-interposition runtime test was made.

## Rejected hypothesis and added characterization

Sparse legacy states do **not** retain previously active values for missing parameter IDs. A new reused-processor regression in `tests/engine/StateDefaultsTest.cpp` passed against unchanged `StateIO.cpp`: loaded v1 trim/routing were preserved and missing Master/Reverb/Multi/Tempo parameters reverted to their declared defaults. APVTS appends a missing adapter tree, whose `valueTreeChildAdded()` listener invokes `setNewState()` with the declared default. Initial static review missed this listener side effect; the executable test refuted the hypothesis before any StateIO implementation edit. The useful migration characterization was retained with parent approval.

## Report findings and verification limits

See `work/audit-report.md` for three independently reviewed P2 report findings: capacity-decimation duration/mean errors, FX totals truncated by the detail-event cap, and disabled Tracking bypassing recorder-stop/shutdown auto-export. The first two have numerical reproductions. The existing pure tracker/writer suite passed all 25 tests; the lifecycle finding was explicitly control-flow reviewed rather than runtime reproduced.

Read the full Core/Control implementations and relevant import/export consumers; checked stable IDs/module offsets, bounded macro destination counts, scene indexes, transient exclusions, scope/lock handling, routing migration handoff, imported recorder track validation handoff, OSC argument/index resolution and socket ownership, message-thread delivery, FIFO producer/consumer ownership, TempoHub finite/bounds handling and publication. Existing tests exercise ordinary round trips, golden 5-FX compatibility, MIDI learning/queueing, scopes and remote action reachability. No further material defect was confirmed from those paths.

This audit did not verify Windows execution, DAW-host state-callback thread schedules, actual device callbacks, network transport under load, sanitizers, disk failure behavior or a long hardware soak. The parent task owns the integrated Linux build/test results and supported-platform gaps. Application source changes in this subtask are limited to ControlHub lifetime and ParamBank snapshot ownership; no commit or push was made.


---

# Recording and ordinary audio playback audit

Scope: `Source/Recording`, `tests/recording`, session sidecar parsing, loader/streamer/writer lifecycle, decoded and streamed playback, trim/loop transitions, recorder alignment/splitting/pre-roll, and source retirement. Product ownership was limited to `SessionMetadata.cpp` and `PlaybackEngine.cpp`; the UI audit owns SoundcheckPlayer changes. This is a source review plus Linux Release reproductions; device, DAW-host and Windows behavior remain unverified.

## Confirmed and fixed

### P1 — malformed negative session offsets crash virtual soundcheck loading

Before the fix, `SessionMetadata::read` copied file `startFrame` and `frames` directly into the session, rejecting only a nonpositive session sample rate. `SoundcheckPlayer::runLoad` then converted the offset and passed it to `AudioBuffer::copyFrom` as a destination index (`SoundcheckPlayer.cpp:173-206` before the UI agent's changes). A valid 4-second mono WAV plus `startFrame=-1024` was enough to write before the allocated audio buffer.

Reproduction before the fix: link `work/recording-repro.cpp` against existing Release recording/JUCE objects, then run `ulimit -c 0; ./work/recording-repro negative`. Actual output: `load accepted=1`, exit 139. This confirmed a crash, rather than merely an unsupported schema concern.

Fix: `SessionMetadata.cpp:145` rejects negative session start/pre-roll/total frame bounds; `SessionMetadata.cpp:162` rejects negative part offsets/lengths and an overflowing `startFrame + numFrames`. Positive 64-bit frame offsets remain supported by metadata, preserving valid long recordings. A separate upper bound on the assembled decoded soundcheck timeline was reported to the SoundcheckPlayer owner, since large positive offsets require validation at that consumer too.

Regression tests: `SessionSafetyTest.cpp` checks all three negative session bounds, negative part start/length, signed overflow, consumer rejection before asynchronous loading, and round-trip preservation of valid offsets above 32 bits.

### P2 — streamed trim changes can play the previous region's pre-buffered head

`PlaybackEngine::attachRing` correctly detected whether a cached head matched the requested trim/reverse key and set the voice to wait for a fresh ring when it did not. However, `renderVoice` unconditionally reused the clip's cached head once the wait ended. The first second could therefore come from the original region, with a jump to the requested region when the head ended.

Reproduction before the fix: `./work/recording-repro trim` printed `first audible sample=0.25 expected about 0.75 (old head is 0.25)`. `StreamTrimSafetyTest.cpp` makes the path deterministic: retain the original voice, publish one head rebuild, request a second trim, retrigger while adoption is delayed by the retained voice, fill the fresh ring, and inspect the audible output. Both channels produced 0.25 where the requested half of the WAV contains 0.75.

Fix: `PlaybackEngine.cpp:73,676,868` snapshots the usable head length when the voice attaches its ring. Rendering uses this snapshot, so a head whose trim/reverse key did not match is never read. This keeps the existing asynchronous rehead and retirement model and adds no audio-thread allocation or blocking. Final review then caught a related in-block restart edge: `renderVoice` cached the head count/pointers before a tempo-synced loop restart could attach a newly valid head. `Streaming_SyncedRestartRefreshesHeadAfterRestoringTrim` reproduced it through public APIs by restoring the cached trim key before a beat boundary inside the block; both channels emitted 0 instead of the expected 0.140625 after the declick ramp. The restart branch now refreshes its local head count/pointers immediately after `attachRing`.

### P2 — stopping a streamed bed before its first fill retains its ring

The normal unload-after-playback candidate was initially rejected (see below). A separate startup path was confirmed: enable the streamed bed, process once without pumping so it requests a ring while its envelope remains zero, then disable the bed or unload its source before the initial fill. `renderBed` returned at its zero-envelope/null-source checks before reaching `renderBedStreamed` ring cleanup. The unneeded ring and its source reader remained active until a later bed restart or engine preparation.

Reproduction before the fix: `work/recording-repro-bedstart.cpp`, derived from the supplied scratch case by stopping before the initial fill, printed `rings before unload=1` and `rings after unload/fade=1`. `Streaming_BedStoppedBeforeInitialFillReturnsItsRing` then failed for both disable and unload variants (two assertions), proving the bug independently of file-handle inspection.

Fix: `PlaybackEngine::renderBed` now returns both grain rings and clears bed-running state before the zero-envelope/null-source early return. Beds whose envelope is still positive continue through the existing fade/tail path.

## Verification evidence

Before implementation, an ad hoc executable linked the new test cases against unchanged Release production objects. It produced 9 expected assertion failures across 4 tests: seven invalid-metadata acceptance failures and two stale-audio mismatches (`0.25 != 0.75`).

After the first two fixes, an executable compiled the changed production files and those four cases and reported `CrowdMike: 4 tests passed`. The startup-bed test subsequently produced two expected RED assertions before its fix. A focused executable containing six cases reported `CrowdMike: 6 tests passed` after those fixes (exit 0). The final-review synced-restart case subsequently produced two expected RED audio failures before the restart-pointer fix. The final focused executable now includes seven cases and freshly reported `CrowdMike: 7 tests passed` (exit 0). The final CMake recording-suite run is owned by the root agent. The root agent also owns full supported Linux build/test verification; do not treat this focused result as the entire test matrix.

## Rejected candidate and remaining coverage limits

The supplied ordinary `bedleak` scratch reproduction did not reproduce a leak: `rings before unload=1`, `rings after unload/fade=0`. Source review confirms that the normal streamed-bed fade completion returns its rings. The normal fade path was preserved. The distinct stop-before-initial-fill defect above was subsequently reproduced and fixed.

Existing tests exercise WAV16/24/32f and FLAC recording, bit-exact multitrack alignment, dropped blocks as silence, duration/manual/byte splitting, pre/post-roll, disk-full and removed-destination behavior, naming, markers, memory accounting, mono/stereo playback, resampling, reverse/trim, loops, tempo sync, panic ramps, long streaming beds, and soundcheck mapping. This audit did not run a thread sanitizer on file decoding/streaming or simulate a decoder blocked beyond the existing 5/10-second thread-stop timeouts. Those timeout paths remain a coverage limit; no separate lifecycle defect was claimed without a reproduction.


---

# UI, processor and standalone audit

Audit date: 2026-10-31. Initial scope was read-only. The parent subsequently authorized focused fixes for physical-map publication and missing playback/soundcheck state and analysis-worker suspension during prepare, with regression tests; other findings remain unmodified.

## Coverage and limits

Read repository AGENTS.md, docs/MODULES.md and IO_SPEC.md; inspected PluginProcessor.*, PluginEditor.*, Standalone/*, shared editor context/ticker/parameter controls, shell, file/preset workflows, input/group/FX/matrix/record/playback/scenes/settings/diagnostics page control paths, metering/analysis widgets, MIDI/OSC/device settings, report panel wiring and associated Ui*/WrapperAudit tests. Checked relevant JUCE MIDI collector implementation and recording/soundcheck lifecycle APIs where these cross wrapper boundaries. This is a source/control-flow audit, not a pixel-by-pixel or assistive-technology certification. Existing UI tests mostly drive callbacks directly and do not exercise native chooser lifetime, real keyboard focus transitions or actual device hotplug.

Linux build/test coordination belongs to the parent. Hardware, native DAW, Windows/macOS, real assistive technology, device removal/recovery and audio-loopback checks remain explicitly unverified per the user's manual-testing deferral. No claim is made that those gates passed.

## Confirmed findings

### W1 — P1 — Callback map storage can be overwritten during an in-flight block (fixed)

Original locations: Source/Standalone/DeviceController.cpp:90-105 and :118; original storage in DeviceController.h:65-72. A callback held a reference into one of two plain Maps slots. Two control-thread publications could cycle back to that slot while the callback still used it, creating a data race and potentially inconsistent physical routing. Deterministic reproduction retained a callback map, published generations 2..19, and observed that the retained generation-1 map changed. A concurrent coherence test also failed against a test adapter of the exact previous two-slot algorithm.

Fix: Source/Core/SnapshotExchange.h gives producer, consumer and exchange slots exclusive ownership with one bounded atomic exchange per publication/acquisition. DeviceMapExchange initializes disconnected maps and DeviceController acquires once at callback entry. The generic helper is also used by ParamBank (core auditor owns that integration). No callback lock, allocation or retry loop was added. tests/engine/DeviceMapSafetyTest.cpp covers retained-reference stability, latest delivery and 100,000 concurrent coherent updates. Both focused tests pass normally and under ThreadSanitizer. The reference contract ends at the consumer's next acquire, documented in code and docs/MODULES.md.

### W2 — P1 — Restoring absent or missing playback state leaves earlier soundcheck/bed active (fixed; full verification pending)

Source/PluginProcessor.cpp:372-385 only applied Bed/Soundcheck nodes when present. Restore a default/legacy show after enabling a bed and loading/arming a soundcheck session: ShowModel no longer contains them, but old bed settings/session remain resident. A new missing soundcheck path similarly skips loading without unloading the prior session. Recorded inputs from an earlier show can therefore remain available while the UI describes an empty/missing reference.

Fix: reset missing Bed to defaults; on absent/changed/missing Soundcheck state unload old data and lock/disarm the player. SoundcheckPlayer::unloadSession clears control metadata/lastSources and publishes an empty immutable Data through the existing pending/retire path; the audio thread never frees it. A minimum accepted data generation invalidates the previous current session even while retirement capacity delays adoption, and obsolete Arm commands cannot override the cleared armed intent. No releaseResources call occurs during processing. WrapperStateSafetyTest.cpp was written before production changes; the parent confirmed 19 expected behavioral failures in 2 tests (RED). The regressions also prove a cleared session cannot be rearmed or reloaded by subsequent sample-rate prepare; GREEN/full build is coordinated by the parent.

### W3 — P1 — Preset chooser can outlive the processor captured by reference (outstanding)

Source/UI/EditKit.cpp:154 owns a process-global static FileChooser. Menu actions at :217-235 launch save/load callbacks capturing &proc. The chooser is neither owned by the editor nor cancelled by processor teardown; safeAnchor is captured at :199 but never checked and is absent from the chooser callbacks. Open a module preset save/load native chooser, destroy the plug-in instance in the host, then accept the chooser: captureBlocks/getUndoManager/getParameters accesses the destroyed processor.

Minimal fix: tie chooser lifetime to an editor-owned service and guard every async callback with a safe owner; dismiss it when that owner closes. Native host/chooser reproduction remains unverified on this headless machine, but the unowned reference lifetime is explicit in the code.

### W4 — P2 — Held performance keys can stay latched after text focus or editor teardown (outstanding)

Source/PluginEditor.cpp:134-147 returns early whenever a TextEditor has focus, including releases. Hold K, focus an editable label while holding it, then release: both TailKill parameters remain 1 until a later eligible key-state event. Closing the editor while K is held also leaves the parameters set; the destructor (:27-33) does not release them, and a new editor starts its local flags false. E/V flags have the same lifecycle issue.

Minimal fix: always service releases of owned held keys, suppress only new presses while typing, and release owned momentary parameters on focus/editor teardown. Test focus transitions and destruction while held.

### W5 — P1 — Standalone MIDI collection takes a blocking lock on the audio thread (outstanding)

Source/Standalone/DeviceController.cpp audioDeviceIOCallbackWithContext calls midiCollector.removeNextBlockOfMessages. JUCE juce_MidiMessageCollector.cpp:86 immediately takes midiCallbackLock shared with the MIDI input producer, even when there are no messages. It also copies events into the unreserved midiBuffer via addEvent, which can grow allocation. This violates AGENTS.md's real-time no-lock/no-allocation contract.

Minimal fix: use a fixed-capacity short-message FIFO fed by the MIDI callback, with bounded lock-free consumption and defined overflow handling. The separate SysEx meta.getMessage allocation issue in PluginProcessor/ControlHub was reported to the parent/core auditor.

### W6 — P2 — Explicit unassigned device binding is reconnected on restart/reprepare (outstanding)

Source/Standalone/DeviceController.cpp:68-71 fills every empty binding with the corresponding active physical channel on every audioDeviceAboutToStart call. Empty is also the persisted representation of an explicit `(unassigned)` selection (SettingsSections.cpp:63-64; DeviceController.cpp:239-262). Select unassigned/silent, then change sample rate/buffer or restart: it is silently rebound.

Minimal fix: distinguish absent first-run configuration from an explicitly saved empty binding and apply defaults only to the former. Hardware-triggered reproduction remains manual; the conflicting read/write representations are directly traceable.

### W7 — P2 — Inputs REC button mistakes configured track existence for armed state (outstanding)

Source/UI/Pages/InputsPage.cpp:104-109 and :184-188 use findInputTrack >= 0 as the arm state. Configure a raw-input track, then disarm it on Record: Inputs still shows REC enabled, and clicking removes the configured track rather than arming it. LiveStrips.cpp:39-42 correctly checks t.armed, confirming inconsistent user behavior.

Minimal fix: use one shared armed predicate/setter (or equivalent small correction) that preserves existing configuration and sets armed=true when the track exists but is disarmed.

### W8 — P2 — Editable group names include an artificial index prefix (outstanding)

Source/UI/Pages/GroupsPage.cpp:64 stores nameLabel.getText() verbatim. refresh() at :111 displays `group number + groupDisplayName` in that same editable label. Editing the existing displayed value persists the synthetic prefix; later refresh/edit cycles can accumulate `1 1 ...` prefixes.

Minimal fix: keep the number in a separate noneditable label or put only the actual name in the editable label, as LiveGroupStrip does.

### W9 — P2 — Show loader claims success for rejected schemas (outstanding)

Source/UI/ShowFiles.cpp:16-20 checks only the root XML tag, calls void setStateInformation and returns true. StateIO::restore rejects missing/invalid/newer schemaVersion. Loading such a .crowdshow reports success and SettingsSections.cpp:610-615 clears undo, although the show did not change.

Minimal fix: validate the state schema before claiming success, or expose the restore result through a narrow file-loading adapter and display that error. Preserve the newer-schema rejection.

### W10 — P1 — Analysis workers can read buffers replaced by prepare (fixed; full verification pending)

PluginProcessor::prepareToPlay previously called engine.prepare while beatAnalysis remained acquired for Auto tempo. BeatTracker::prepare replaces its FIFO and analysis vectors without synchronizing with analyse. Device/sample-rate changes could race the worker. The engine auditor independently corroborated this.

Fix: AnalysisThread::ScopedSuspension holds a non-audio lifecycle mutex, joins in-flight work, prevents concurrent acquire/release from restarting it during prepare, and resumes only its previous reference-counted users. prepareToPlay suspends both beat and feedback workers before replacing engine buffers. The audio callback/run loop never acquires this lifecycle mutex. AnalysisLifecycleTest.cpp covers in-flight joining, concurrent acquire blocked until scope exit, preservation of two users, idle workers staying idle and active-worker processor reprepare. The two isolated worker-lifecycle tests pass in a focused executable linked against JUCE core; processor integration/full-suite verification is coordinated by the parent.

### W11 — P1 — Sparse soundcheck timelines can overflow bounded audio storage (fixed)

SoundcheckPlayer::runLoad formerly converted unchecked rescaled file offsets with llround, capped the final allocated sample count at INT_MAX-1, then retained the uncapped total as Data::frames and cast offsets to int during copyFrom. A tiny valid file at a huge positive 64-bit session offset could therefore request gigabytes, wrap the copy destination, or expose out-of-buffer playback. SessionMetadata must keep valid 64-bit offsets for long recordings; the in-memory soundcheck consumer must enforce its own limits.

Fix: runLoad validates finite/nonnegative rescaled offsets against representable and memory-bounded frame limits before llround, checks offset + decoded frames without overflow, and checks assembled channel-count times timeline memory before allocation. Allocation is no longer silently truncated below Data::frames. Invalid input produces a clean asynchronous timeline error without publication. SoundcheckBoundsSafetyTest.cpp covers huge positive offsets, sparse multichannel timelines whose assembled memory exceeds the cap, and an ordinary valid sparse session. Baseline RED ran under a 512 MiB address-space cap to prevent the old allocation exhausting the machine: 2 expected error assertions failed, valid case passed. GREEN ran without that cap: all 3 tests passed.

## Cross-module confirmed concerns sent to the parent
- Host state get/set methods synchronously read/write message-thread ShowModel/SceneBank data; a host may invoke state methods off the GUI thread. Needs a documented and tested cross-thread state adapter, not an audio-thread GUI lock. Core auditor also reported this; no speculative patch made.

## Verification performed by this auditor

- Small mapping regression executable compiled from TestMain.cpp + DeviceMapSafetyTest.cpp against the old algorithm adapter: failed retained-reference/coherence assertions (RED).
- Same executable against SnapshotExchange/DeviceMapExchange: 2 tests passed (GREEN).
- Same focused tests with -fsanitize=thread: 2 tests passed; no race report.
- Parent confirmed wrapper-state behavioral RED: 19 expected failures in 2 tests. Application GREEN builds and lifecycle regression results are recorded by the parent once complete.

- Additional focused SoundcheckBoundsSafetyTest.cpp RED/GREEN: 2 expected baseline failures under safe allocation cap; 3 tests passed after guard, without cap.
- Focused AnalysisThread lifecycle executable: 2 tests passed (join/restart exclusion/reference counts; idle guard).
- Existing plus new Soundcheck regression sweep: 6 tests passed. Fresh independent final_diff_review review found no introduced defect in Soundcheck bounds/unload, wrapper restore or worker suspension, and independently reran the 6 Soundcheck and 2 worker tests successfully.


---

# Read-only Report audit

Scope: `Source/Report/*`, `tests/engine/ShowReportTrackerTest.cpp`, `ShowReportWritersTest.cpp`, `ShowReportServiceTest.cpp`, relevant processor/recorder integration, `docs/SHOW_REPORT_SPEC.md`, `docs/MODULES.md`, and repository `AGENTS.md`. No application source was changed.

## Confirmed findings

### R1 — P2: Decimation gives a short bucket the full doubled duration, and averages ignore bucket weights

Locations: `Source/Report/EnergyTracker.cpp:84` (`flushBucket`, especially 86–88); `Source/Report/EnergyTracker.cpp:213` (row weights), `Source/Report/EnergyTracker.cpp:229` (recorded duration), `Source/Report/EnergyTracker.cpp:230` and `:238` (unweighted averages).

When the series is full, `flushBucket()` doubles `step` by halving existing rows, then immediately writes the already accumulated old-width bucket. Every stored row is subsequently assigned the doubled `step`, including that short bucket. This occurs with the production capacity immediately after 43,200 one-second samples. A full report can claim an extra second and an extra recorded second; after further decimation the wrong weights also contaminate merged levels. Separately, a pending bucket with less than `step` raw samples receives the same mean weight as a full stored bucket, so power averages are wrong even though its true duration is available as `acc.n`.

Executable reproduction: `work/report-repro.cpp`, compiled against the unchanged pure tracker/detector. The capacity is reduced to 4 to reach the same transition quickly:

```text
5 second recording: duration=6 recorded=6 step=2 times=0,2,4,
6 raw samples (five -30, one -10): average=-15.8922 expected=-17.5696
```

Minimum fix: if halving raises the required bucket width, retain the incomplete accumulator until it reaches the new width instead of immediately flushing it. Preserve duration/weight for odd-capacity leftover rows if arbitrary capacities remain supported. Weight power means by each row's actual contributing sample count, including partial buckets, and clamp recorded time to actual window overlap. Regression coverage should assert varying-level means and exact duration/recorded time immediately before/after capacity transitions, not only a constant-level long show.

### R2 — P2: FX summary silently loses earlier uses after the capped event log fills

Locations: `Source/Report/EnergyTracker.cpp:119`–`:122` (FX events and cap); `Source/Report/EnergyTracker.cpp:274`–`:278` (summary derived exclusively from retained events).

The default `maxEvents=500` caps `fx_` at 2,000 entries. The series preserves all FX deltas, but the report's per-kind summary is recalculated from the truncated log. A take with 2,001 echo throws therefore reports only 2,000; subsequent events keep erasing earlier counts. The documented summary covers the full report window, with no indication that FX totals have been truncated.

Executable reproduction uses the production defaults and 2,002 samples, incrementing the echo counter each second after the baseline:

```text
2001 echo throws: summary=2000 series=2001
```

Minimum fix: preserve per-kind totals in the bounded time buckets (or another aggregation that supports the requested report window) rather than deriving full-window totals from the capped detail list. Add a test beyond the default FX cap and a take-window test proving preceding-take events remain excluded.

### R3 — P2: Turning Tracking off suppresses the separate recorder-stop and shutdown auto-export behavior

Locations: `Source/Report/ShowReportService.cpp:134`–`:138` (disabled early return and clock flag), `:162` (`syncRecorderState` unreachable while disabled), `:285`–`:295` (shutdown is conditional on that flag).

Deterministic control-flow reproduction: start a take with Tracking and Auto-export enabled; poll long enough to collect history; turn only Tracking off and allow a poll; stop the recorder. All subsequent polls return before observing the falling recording edge. No stop report is written even though Auto-export is still enabled. Closing the app also skips the existing history/take because the disabled poll set `clockRunning=false`. The tracker can still hold valid data, and the UI's separate Tracking toggle promises only to stop sampling and auto markers (`Source/UI/ReportPanels.cpp:107`; SHOW_REPORT_SPEC sections 7, 8, 10).

This finding is confirmed by the unconditional early return and shutdown condition; a GUI/recorder runtime reproduction was not run in this subtask.

Minimum fix: continue recorder lifecycle observation and export dispatch while sampling is disabled; keep clock initialization/history ownership separate from the enabled-sampling flag. Add a stop/export and app-exit test with Tracking disabled after a take has accumulated data.

## Validation and coverage

Ran the existing pure Report tracker/writer suite against unchanged application sources:

```sh
g++ -std=c++17 -ISource -Itests tests/TestMain.cpp \
  tests/engine/ShowReportTrackerTest.cpp tests/engine/ShowReportWritersTest.cpp \
  Source/Report/EnergyTracker.cpp Source/Report/PeakDetector.cpp \
  Source/Report/ReportWriters.cpp -o work/report-tests
work/report-tests
```

Result: **25 tests passed**. The independent defect driver also compiled and ran successfully:

```sh
g++ -std=c++17 -ISource work/report-repro.cpp Source/Report/EnergyTracker.cpp \
  Source/Report/PeakDetector.cpp -o work/report-repro
work/report-repro
```

Inspected writer escaping/UTF-8 sanitization, finite-number formatting, locale handling, bounded lists/series, detector state, marker offset integration, report ownership naming, atomic meter reads, feedback worker acquisition/release, shutdown integration, and tests. No further material defect was confirmed in those areas. This does not constitute runtime Windows, DAW host, device, disk failure, or complete message-thread lifecycle verification. The full application build/test matrix remains the parent task's responsibility.


---

# Build, CI, installer, and shared-script audit

Audit baseline: `1cf0a3da5cae6ed6bac49da56a2a3fac5f89674f` (`main`), 2026-10-31.
Local M8 release fixes are called out separately below. Other agents own engine, UI, and standalone changes.
PR #8 (`docs/readme-and-wiki`) was preserved.

## Scope and method

Read all of `CMakeLists.txt`, `.github/actions/setup-build/action.yml`, the four workflows in
`.github/workflows/`, `installer/CrowdMike.iss`, all installer READMEs, `installer/linux/install.sh`,
and all fifteen files under `shared/scripts/`:

`auto_update_doctor.py`, `build_release.py`, `bump_version.py`, `ci_configure.sh`, `create_project.py`,
`find_starter_repo.py`, `generate_release_notes.py`, `inspect_juce_project.py`, `make_beta_notes.py`,
`package_beta.sh`, `port_audit.py`, `setup_sparkle.sh`, `setup_updates.py`, `setup_winsparkle.sh`, `vm_registry.py`.

Also read `AGENTS.md`, `docs/RELEASE_PROCESS.md`, relevant implementation/testing specifications,
standalone startup, and the pinned JUCE plist generation code. Python syntax and shell syntax checks
passed. Script findings were reproduced with read-only/dry-run commands or isolated temporary fixtures.
No release, tag, branch, wiki, updater, installation, or remote CI state was mutated.

The local Linux toolchain is a workspace sysroot, activated with
`source /workspace/work/crowdmike-env/activate.sh`. Windows/macOS binaries and installers were not run.
Hardware and DAW-host checks remain explicitly pending as requested.

## Confirmed release findings fixed locally

1. **P1 — Validator download setup could not reach either validator.** Baseline
   `.github/workflows/release-beta.yml:263` downloaded an Ubuntu ZIP, but line 264 tried to extract
   a nonexistent top-level `.tar.gz`. The actual upstream `0.4.1` release asset
   `clap-validator-0.4.1-127-g152b982-ubuntu-22.04.zip` contains
   `clap-validator-0.3.2-127-g152b982-ubuntu-22.04.tar.gz`; that tarball contains the executable.
   Fixed extraction at current workflow lines 267–269 by selecting the ZIP, unpacking it, then
   unpacking the nested tarball. A representative archive fixture executes the shipped shell step.
   The original step failed with `Cannot open: No such file or directory`; it now extracts executable validators.

2. **P1 — A successful PR run could incorrectly satisfy the exact-commit release gate.**
   Baseline workflow lines 106–109 selected all successful runs by API `head_sha`, regardless of event.
   `ci.yml:32` checks out the default PR synthetic merge ref, while the PR run's API `head_sha`
   identifies its branch head. A manual release from that branch could therefore claim that the
   packaged commit itself had passed CI. Current gate line 110 accepts only successful `push` or
   `workflow_dispatch` runs. Extracted-shell fixtures reproduced acceptance of PR-only green and
   PR-green/push-red; both now fail, while direct-checkout green runs pass.

3. **P1 — An existing orphan tag could point a new release at a different commit.** Baseline
   reconciliation at workflow line 337 checked SHA only when a release existed. If `v<version>`
   already existed without a release, `gh release create --target SHA` kept that tag's existing
   commit even though assets and notes were built from `SHA`. Current lines 363–369 peel an existing
   tag to its commit and refuse a mismatch. Fresh releases and same-SHA lightweight/annotated tags
   still pass, and tags are left unchanged. Temporary Git repositories exercise the real shell step.

4. **P2 — Release notes reported both validators as having run even when setup failed.** Baseline
   publish env line 318 treated any non-skipped validator job as a completed run; job-level
   `continue-on-error` also obscured setup failure. Current workflow lines 239–241 export actual step
   outcomes, lines 322–323 default absent outcomes to `skipped`, and `make_beta_notes.py:64` renders
   each validator separately as passed/failed/did not run/cancelled. The original `--validators-ran`
   CLI remains supported. Real notes-shell fixtures cover success, failure, setup skips, partial
   execution, and cancellation. Validator results remain informational per the existing release policy.

Regression command: `python3 -B -m unittest discover -s tests/scripts -v`.
Four tests pass, covering seven CI gate cases, four orphan tag cases, archive extraction, and five
validator outcome combinations. `.github/workflows/ci.yml:34` runs them on Linux with the standard library.

## Confirmed remaining findings / separate ownership

5. **P1 — The current main commit has a real Windows CI failure, so a release is not yet eligible.**
   GitHub run `39538365325` for exact SHA `1cf0a3d` has Linux/macOS success but Windows failure.
   [Windows job log](https://github.com/djshellshoxxx/crowdmike/actions/runs/39538365325/job/119020342042)
   shows `crowdmike_tests` terminating with `***Exception: SegFault` after 2.40 seconds; DSP and
   recording tests passed. The log contains no individual failing test or stack trace. Nightly runs
   succeeding on the same SHA do not satisfy the release gate. This needs a new successful Windows
   run after source fixes, ideally with crash diagnostics; local Linux passing is insufficient.

6. **P1 — macOS standalone microphone usage metadata is absent at the audit baseline.**
   `CMakeLists.txt:46` creates a Standalone target but supplied no `MICROPHONE_PERMISSION_ENABLED`
   or usage text. Generated `build/CrowdMike_artefacts/JuceLibraryCode/Info.txt` confirmed both fields
   empty. JUCE 9.0.3 `juce_PlistOptions.h:53` defaults the permission off and
   `juce_PlistOptions.cpp:146` only emits `NSMicrophoneUsageDescription` when enabled.
   `Source/Standalone/Main.cpp:37` opens the input device immediately. A beta requiring microphone
   access must supply the usage description; macOS permission behavior remains a platform check.
   Root owns the small CMake metadata fix. This audit did not edit CMake.

7. **P2 — A failed preservation download can leave release checksums incomplete.**
   `.github/workflows/release-beta.yml:355` suppresses every existing-asset download failure with
   `|| true`. A partial rerun after a network/API failure can regenerate notes and `SHA256SUMS`
   from only the assets that happened to download, while previously uploaded assets remain on the
   server (`gh release upload --clobber` does not remove them). Fix by distinguishing an existing
   release with zero assets from a failed download, and abort on download failure when assets exist.
   This is not a reason to delete other platform assets. Reported to root for triage.

8. **P2 — The generic version bump tool is unsafe for CrowdMike's current version sources.**
   `shared/scripts/bump_version.py:55` reads only `.env`, with fallback version `0.0.1` at lines 60–62.
   CrowdMike has no `.env`; its authoritative CMake version is `0.3.0`.
   `python3 -B shared/scripts/bump_version.py --dry-run --project-root .` reports `0.0.1 → 0.0.2`.
   A real invocation writes `.env` and CMake (lines 226–231) but leaves `kAppVersion`, the state
   serializer fallback version, and the installer defaults unchanged. The release prepare step
   checks only CMake (`release-beta.yml:76`), so metadata disagreement is not automatically rejected.
   The current four numeric versions agree; this is a future maintenance hazard, not an existing
   package version mismatch. Derive numeric metadata from one source or add consistency checks;
   do not use the inherited bump tool unchanged. No version sources were modified by this audit.

9. **P2 — Linux installer reports success for a missing/incomplete payload.**
   `installer/linux/install.sh:34` and `:39` silently skip missing files, and lines 35–38 skip a missing
   VST3 bundle; lines 41–44 still print all components as installed and return success. A truncated
   extraction or invoking the installer from the source directory appears successful. Validate all
   required source files before modifying any installed component. Official packaging currently
   requires those payload files, so this is installation diagnostics/error handling, not evidence
   that the generated archive is incomplete. No actual user/system install was attempted.

10. **P3 — Inherited starter tools do not describe or build the active CrowdMike project.**
    `shared/scripts/build_release.py:47` requires nonexistent `scripts/build.sh`/`scripts/build.ps1`.
    Its dry run returns 1 (`Missing build script`); the supported project path is CMake plus the
    CI scripts. `inspect_juce_project.py:156` only recognizes `build.yml`, so its JSON reports
    `ci: false`, `version: 0.0.0`, and `platforms: []` on this repository. These are generic
    scaffolding utilities rather than active release dependencies. Mark/document their scope or
    adapt them in a separate utility change; they should not be used to assess M8 readiness.

11. **P3 — Updater doctor has no failing process status.**
    `shared/scripts/auto_update_doctor.py:253` computes failed checks but line 258 always returns 0.
    On this checkout it reports 3 fail / 17 warn with exit 0. Consequently
    `setup_updates.py:133` using `check=True` cannot detect failed doctor checks. Updaters are not
    integrated into the current beta, so this is dormant utility behavior. A dedicated change
    should define whether failures should return nonzero; no updater changes were made.

## Verification and remaining risk

- All release regression fixtures pass after each fix; each material release bug was first observed
  failing against the original behavior. Python and Bash syntax checks passed; `git diff --check` passed.
- Actual validator archives are retained under `/workspace/work/beta-pipeline-check/`. The prepared
  executables are `pluginval/pluginval` and `clap-validator/clap-validator`. `ldd` found no missing
  libraries. CLAP reports version 0.4.1; pluginval help starts under the activated xvfb environment.
- Actual plug-in validation awaits the Linux plug-in build. Its results belong in the final M8
  validation record; downloading validators alone does not validate CrowdMike.
- Hosted CI, Windows/macOS packaging, Inno Setup installation, old OS deployment targets, signing,
  notarization, real DAW loads, hardware disconnects, and long hardware use remain unverified locally.
  The existing release documentation accurately treats the current unsigned/unnotarized beta and
  manual hardware checks as limitations. No claim of full M8 completion is justified yet.


---

# CLAP state validator diagnosis

Diagnosis of the three state reproducibility failures in `/workspace/work/crowdmike-env/validator-clap-clean.log` (approximately lines 354–421). **The observed failure is a missing host notification; these results do not show state data loss.** The initial interpretation that 1,741 randomized values reverted to defaults was incorrect; the validator was reporting fresh defaults changing to correctly restored values without a host rescan. The investigation first remained read-only; the subsequently authorized application fix is recorded below. No dependency source was changed.

## Exact failure

The pinned CLAP wrapper calls `processor->setStateInformation(...)` in `build/_deps/clap_juce_extensions-src/src/wrapper/clap-juce-wrapper.cpp:2468`, clears its scratch buffer and returns success. It does not call the host's `params.rescan(CLAP_PARAM_RESCAN_VALUES)` after state restore.

The wrapper does support the notification indirectly: `audioProcessorChanged()` maps JUCE `programChanged` to `paramsRescan(CLAP_PARAM_RESCAN_VALUES)` at approximately line 666. CrowdMike's `setStateInformation()` restores state and applies show changes without publishing that display change. Parameter listener callbacks do request deferred `params.flush` calls, but those requests do not satisfy a value rescan.

The relevant CLAP contract is in `build/_deps/clap_juce_extensions-src/clap-libs/clap/include/clap/ext/params.h:40`–`:44`: loading a preset must call the host's parameter rescan if anything changed.

## Why the log looks like values reverted

The exact validator revision is available in `work/clap-validator-152b982/src/tests/plugin_instance/state.rs`. It snapshots the recreated instance's **default values before load** (lines 206–207), loads the saved state (209–214), observes host callback events (216–235), and compares those defaults to the newly restored values (237–244). If values changed without a rescan, it emits the observed message and stops the test.

Only afterward, at lines 268–272, does it compare restored values to the previously saved parameter values. That separate comparison has a different error: “do not match the previously saved values.” The actual log never reached that comparison. Its `-12.0` versus `-22.7` example therefore represents default → restored randomized value, not saved randomized value → lost default.

## Direct binary evidence

Compiled a small CLAP host, `work/clap-state-report-probe.cpp`, against the built `build/CrowdMike_artefacts/Release/CLAP/CrowdMike.clap`. It:

1. Creates and initializes an instance.
2. Sends valid randomized values for all 2,069 parameters in one active `process()` call, respecting `CLAP_PARAM_IS_STEPPED` values.
3. Saves state, destroys the instance, creates another instance, and loads the saved bytes.
4. Queries every restored value directly through `clap_plugin_params.get_value`.
5. Exposes `clap_host_params` callbacks and counts rescan and flush requests.

Result:

```text
save=1 bytes=106059
load=1 differences=0 count=2069 rescans=0 flushRequests=1761
```

All 2,069 values roundtrip. No value rescan occurs. A simpler single-parameter inactive `params.flush` reproduction also roundtripped correctly.

The scratch host does not exercise the validator's complete host/event-loop matrix; the exact validator source plus the direct callback result establish the missing-notification cause of these particular logged failures.

## Minimum fix alternatives

- Preferred application-level option: after a successful CrowdMike state restore, call JUCE's host-display notification with `programChanged` set. The existing CLAP adapter already translates that into `CLAP_PARAM_RESCAN_VALUES`, and other JUCE formats receive the appropriate state-change notification through their wrappers.
- Wrapper-level option: after `stateLoad()` restores the processor, call `_host.paramsRescan(CLAP_PARAM_RESCAN_VALUES)` when the host supports params. This should be a reproducible pinned dependency patch or upstreamed fix, not an unrecorded edit under `build/_deps`.

Do not change the saved parameter values, serialized schema, or validator assertions to address this failure. Rerun the actual three state tests after the notification fix. Their later binary-equality check may reveal a separate issue (for example, prepared-versus-unprepared `EngineConfig` metadata); no claim is made here that a notification fix alone establishes complete validator success.

## Implemented fix and regression

After `StateIO::restore()` returns `Ok`, `CrowdMikeAudioProcessor::setStateInformation()` now applies show changes and calls `updateHostDisplay(juce::AudioProcessorListener::ChangeDetails{}.withProgramChanged(true))`. Malformed XML, invalid binary chunks and unsupported newer schema versions retain their early return without publishing a successful restore notification.

`tests/engine/StateHostNotificationTest.cpp` observes the public JUCE processor-listener boundary. Its valid-state case restores a literal -6 dB master level and requires one `programChanged` notification; its rejected-state case sends malformed bytes and a schema-99 state and requires none. An ad hoc executable linked against the unchanged processor object produced the expected RED: the level restored correctly but the notification assertion failed; rejected inputs passed (one failure in two tests). Recompiling only the updated processor into a separate scratch executable produced GREEN: **two tests passed**. These isolated builds wrote only under `work/` and did not alter the shared Ninja build or dependency sources.

The parent task owns the final integrated processor/plugin rebuild and actual CLAP validator rerun. The reproducible focused validator invocation is `clap-validator validate --in-process -t '^state-reproducibility-basic$' build/CrowdMike_artefacts/Release/CLAP/CrowdMike.clap`; it reproduced the missing-rescan failure before the fix. In-process mode avoided an unrelated sandbox rejection in the validator's out-of-process wait-timeout helper during this subtask.


---

# Independent M8 final diff review

Review date: 2026-10-31. Baseline: `1cf0a3d`. Reviewed tracked changes and the untracked source/test additions in `/workspace/crowdmike`. Read `AGENTS.md`, `work/m8-plan.md`, `work/progress.md`, `docs/MODULES.md`, relevant engine/control/recording/testing contracts, and the subsystem audit reports. No production files were changed and no shared Ninja build was started.

## Final verdict

**No unresolved introduced correctness or safety defect confirmed in the final reviewed diff.** The initial review found one P2 playback regression; the owner corrected it and added a public-API regression with RED/GREEN evidence. Complete Linux application builds/tests and the platform/manual gates remain the parent agent's responsibility.

### R1 — P2 — A streamed tempo-loop restart can use unfilled ring samples for the rest of the block (fixed)

Location: `Source/Recording/PlaybackEngine.cpp:868` and the restart branch at `:930-938` (line numbers before the follow-up correction).

The change correctly snapshots head eligibility in `Voice::headFrames`, but `renderVoice` copies that value into local `hN` only before its per-sample loop. A tempo-synced restart later calls `attachRing`, which updates `v.headFrames`. If the voice initially had no eligible cached head and current trim/reverse later returns to the clip's cached-head key, `attachRing` switches from zero to a positive usable head length. It requests the new ring starting at that head length. The unchanged local `hN` is still zero, so the remainder of this block reads ring indices starting at zero even though those samples were intentionally skipped by the producer. This can play silence or stale ring data until the next block.

This is reachable when a voice starts during an asynchronous trim/head update, then the trim is restored before a later synced loop restart. It is introduced by replacing unconditional `c->headFrames` with the voice snapshot without refreshing local head pointers/count after a same-block sequence restart.

Deterministic isolated reproduction: `work/review-loop-head.cpp` includes the current production translation unit and constructs the documented intermediate voice state, without changing shared build files. The existing cached head contains 0.25, the old sequence ring contains 0.75, and the next loop starts 120 samples into the block. It prints `head after restart=48000 ... post=0 expected post>0.1` and exits 1. Repeating with only the old unconditional `hN = c->headFrames` rule in a scratch source copy prints `post=0.140625` and exits 0, confirming this particular failure is introduced by the new rule. The voice's published head count changed while the renderer's local count did not. This is an internal-state probe, not a new application test.

Correction reviewed: after a successful restart `attachRing`, the branch now refreshes `hN`, `h0`, and `h1` from the newly attached sequence before further sample rendering. The owner added `Streaming_SyncedRestartRefreshesHeadAfterRestoringTrim` in `tests/recording/StreamTrimSafetyTest.cpp`. Its public-API RED produced 0 instead of 0.140625 on both channels; the owner's freshly compiled focused executable then passed all seven cases. I independently reran that seven-case executable and recompiled my isolated probe against the corrected production source: the probe now prints `post=0.140625` and exits 0.

## Other reviewed invariants

- `SnapshotExchange`'s three-slot ownership transfer keeps producer, consumer and exchange slots distinct; acquire/publish use bounded lock-free atomic work. The returned reference ends at the consumer's next acquire. Device maps are acquired once per complete callback. ParamBank's normal producer and `currentMacros()` view are message-thread-only as documented. Existing host state callback threading remains a separate baseline risk.
- The analysis suspension mutex is absent from `run` and audio processing. Joining the active analysis jobs before engine buffer replacement and serializing acquire/release closes the original prepare race. Existing user reference counts are retained.
- The ControlHub deferred callback checks its owner's weak lifetime token under the module's existing message-thread execution contract. The tests observe cancellation safely while retaining parameter storage.
- Missing/changed soundcheck state now clears reload metadata, locks/disarms, and publishes retirement-safe empty data. The generation lower bound prevents old current audio being rearmed if publication adoption is delayed. No audio-thread delete was added.
- The session-frame checks avoid negative/signed-overflow part extents while preserving nonnegative 64-bit metadata. A subsequent narrow soundcheck-consumer fix also validates finite/nonnegative scaled offsets before rounding, bounds each decoded part extent by checked subtraction, and bounds the assembled channel-count times timeline before allocation. The allocated frame count can no longer be silently truncated below `Data::frames`. I re-reviewed these bounds and their ordinary-sparse/huge-offset/multichannel regressions; no introduced defect confirmed.
- The bed's zero-envelope early exit returns both rings; restarting resets the grain state through the existing path. Positive-envelope fade behavior is preserved.
- Panic/rehearsal output gain now covers the dry/wet blend; the cue bus intentionally remains after that gain per `RECORDING_SPEC` section 14.
- No parameter tables/IDs, module enum values, plugin identities, state schema version or preset schema changed. Public entry points were retained; `unloadSession` and `ScopedSuspension` are additive. The Windows fixture correction preserves the four-instance characterization while moving storage to the heap.
- Added tests exercise behavioral failure modes, including message callback lifetime, retained snapshots/concurrent coherence, output layouts, absent/missing state, worker pause/restart, invalid timeline bounds, stale trimmed heads and stopped-before-fill rings. `StateDefaultsTest` is a useful passing characterization, without a speculative production edit.

## Release/build sub-review

Delegated independently to `release_review`; detailed report: `work/final-release-review.md`. No introduced workflow/script/CMake defect confirmed. `python3 -B -m unittest discover -s tests/scripts -v` passed all four tests, including actual shell fixtures for gate/tag/archive behavior. MacOS microphone permission options match JUCE's metadata interface; platform behavior remains unverified.

Scoped `git diff --check` passed during this review. No full app/build result is claimed here.

## Final narrow follow-up verification

After the restart correction and added soundcheck bounds, re-read `PlaybackEngine`'s restart branch, `SoundcheckPlayer.{h,cpp}`, `PluginProcessor`'s playback restore, and `AnalysisThread`'s suspension diff. No additional introduced lifecycle, real-time, or transport bug was confirmed. The bounds are outside the callback; the unload hand-over and worker pause retain the invariants described above.

Independent focused reruns: `work/recording-safety-tests-green` **7 tests passed**; `work/soundcheck-regressions` **6 tests passed** (existing soundcheck transport plus new bounds); `work/analysis-lifecycle-focus` **2 tests passed**. These are focused executables supplied by the source owners, not a substitute for the parent's pending complete build/test pass. The private-state playback probe was independently recompiled from the corrected source and passed.

## Host state notification follow-up

Reviewed the final successful-restore notification in `PluginProcessor::setStateInformation` and both `StateHostNotificationTest.cpp` cases. No introduced defect confirmed. The pinned CLAP wrapper's `audioProcessorChanged` maps `programChanged` to a main-thread `paramsRescan(CLAP_PARAM_RESCAN_VALUES)`, which is the missing notification established by the separate validator diagnosis. The application publishes it only after a successful restore and `applyShowChanges`; malformed/rejected schemas retain their early return. This changes neither saved values nor parameter/schema identities.

The regression observes the public processor-listener boundary and verifies actual restored value plus exactly one notification; its rejection case checks malformed binary and unsupported schema with zero notifications. I independently reran the owner's `work/state-host-green`: **2 tests passed**. Actual rebuilt CLAP validator outcomes are owned by the parent, rather than inferred from this focused test.

## Cloud setup deliverables follow-up

Reviewed `docs/CLOUD_DEVELOPMENT.md`, `shared/scripts/setup_linux_cloud.sh`, and the README link. The documentation accurately bounds recreation to the same Debian 13 amd64 base, distinguishes exact cached versions from future apt resolution, and identifies unexecuted install/platform gates. The script defaults to read-only checks, confines the environment prefix to `/workspace`, configures private apt paths before invocation, downloads/extracts without executing maintainer scripts, and confines symlink repairs to the overlay. No current correctness or safety blocker confirmed. Independent sub-review passed `bash -n` and `--check` against the prepared prefix; no install was run and no shared Ninja build was started.

Low-priority maintenance caveat: the install path extracts all cached `.deb` files in lexical order. If a future rerun retains multiple versions of one package, an older archive could overwrite a newer extraction. Fresh-prefix recreation and the current prepared archive set are unaffected. Selecting only apt's current package/version set during extraction would make repeated upgrades more robust; this does not block the present environment or product validation.

## Baseline risks distinguished from this diff

The existing subsystem reports retain P1 numeric import/nonfinite handling, native preset chooser lifetime, standalone MIDI callback locks/large MIDI-message allocation, and host state callback threading concerns. The partial-release preservation download suppressing failures remains baseline P2 at `.github/workflows/release-beta.yml:356`. These were not introduced by the reviewed changes and must not be relabeled as fixed by this patch. Hardware/DAW-host, native Windows/macOS and acoustic checks remain explicit manual gates.


---

# Independent final release diff review

Baseline: `1cf0a3d`. Reviewed the current tracked changes in `.github/workflows/ci.yml`,
`.github/workflows/release-beta.yml`, `shared/scripts/make_beta_notes.py`, and the microphone
permission additions in `CMakeLists.txt`, plus untracked `tests/scripts/test_beta_release.py`.
Read `AGENTS.md`, `docs/RELEASE_PROCESS.md`, `work/audit-build-release.md`, the surrounding workflow,
and setup-build action. No tracked files were modified and no build was started.

## Verdict

**No confirmed introduced correctness or safety defect in this scope.**

- The gate now accepts successful direct-commit push/manual CI runs and rejects PR synthetic merge
  runs. Existing workflow triggers/checkout behavior support that distinction.
- Orphan tag validation peels both annotated and lightweight tags and refuses package/tag commit
  disagreement before release creation. The fetch-depth-zero checkout supplies existing tags.
- Validator extraction matches the recorded nested upstream archive format; the fixture executes the
  actual shipped shell step. Per-step outcomes correctly distinguish success, failure and skipped
  setup, while preserving the existing informational-only release policy.
- Notes retain the existing `--validators-ran` interface and add optional explicit outcomes; no release
  version, plugin identity, or public state/parameter schema changes are present.
- JUCE microphone permission options add the expected macOS usage-description metadata. Actual
  macOS permission behavior still needs its documented platform check.

## Independent verification

`python3 -B -m unittest discover -s tests/scripts -v`: **4 tests passed**, including actual workflow
shell extraction for seven CI gate cases, four orphan tag cases, nested archive extraction, and five
validator outcome combinations. Scoped `git diff --check` passed.

## Unchanged audit-open issue

The existing-release preservation download still uses `gh release download ... || true`
(`.github/workflows/release-beta.yml`, currently line 356). A failed partial download can omit retained
remote assets from regenerated checksums. This predates the reviewed changes and is already recorded
as P2 in `work/audit-build-release.md`; it is not an introduced regression.

Hosted Actions execution, Windows/macOS packaging and permissions, live release reconciliation, and
upstream archive changes beyond the recorded format were not executed in this review.

## Cloud setup followup

Reviewed untracked `docs/CLOUD_DEVELOPMENT.md` and `shared/scripts/setup_linux_cloud.sh` after the
initial release review. `bash -n shared/scripts/setup_linux_cloud.sh` and the script's `--check`
against `/workspace/work/crowdmike-env` both passed. No package download, installation, configure,
or build was performed. No confirmed current defect blocks the documented same-base, fresh-prefix
recreation procedure. The guide distinguishes the observed environment, the unexecuted recreation
path, and pending platform/hardware checks accurately.

Low-priority maintenance caveat: script lines 53–55 extract every `.deb` retained in the cache, in
lexical filename order. A future `--install` rerun after package updates could extract multiple
versions of a package and let an older lexical-last archive overwrite the latest one (for example,
`1.9` sorts after `1.10`). Fresh-prefix recreation is unaffected. Prefer one selected package version
per name/architecture or document recreation into a fresh prefix before relying on update reruns.


---

# Windows test stack diagnosis

Repository baseline: main `1cf0a3da5cae6ed6bac49da56a2a3fac5f89674f` (before root fixes). Read-only diagnosis on Linux x86_64 / GCC 14.2, 2026-10-31. No product files changed by the diagnosing agent.

## Evidence

Main CI https://github.com/djshellshoxxx/crowdmike/actions/runs/39538365325/job/119020342042: Windows compiles, DSP tests pass, recording tests pass, `crowdmike_tests` crashes with a CTest SegFault classification after 2.40 s. PR #8 repeats the same pattern after 3.30 s; Linux/macOS jobs pass. Logs contain no individual-test trace/stack.

`audit_state_without_scenes_clears_the_scenes_of_the_target_instance` (`tests/engine/AuditAEngineTest.cpp:282`) has four simultaneous local processors (`src`, `dst`, `withScenes`, `again`). Release header size probes:

| Type | Bytes |
|---|---:|
| CrowdMikeAudioProcessor | 275264 |
| CrowdEngine | 146008 |
| PlaybackEngine | 76824 |
| Four processors | 1101056 |

GCC `-O2 -fstack-usage` measures that test frame at **1101328 bytes**, above the Windows default 1 MiB executable stack reserve. Root subsequently confirmed the existing filtered test exits with segmentation fault status 139 under a 1 MiB Linux stack before the fix, corroborating the Windows stack-overflow diagnosis.

## Exact probes

From repository root, with fetched JUCE headers:

```bash
c++ -std=c++20 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DNDEBUG -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -I build/_deps/juce-src/modules -I Source -S -x c++ -o - - <<'CPP' | rg -A2 '\.size\s+(processor_size|engine_size|playback_size|four_processors_size)'
#include "PluginProcessor.h"
char processor_size[sizeof(CrowdMikeAudioProcessor)];
char engine_size[sizeof(crowdmike::CrowdEngine)];
char playback_size[sizeof(crowdmike::rec::PlaybackEngine)];
char four_processors_size[4 * sizeof(CrowdMikeAudioProcessor)];
CPP

c++ -std=c++20 -O2 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DNDEBUG -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -I build/_deps/juce-src/modules -I Source -I tests -I tests/engine -fstack-usage -c tests/engine/AuditAEngineTest.cpp -o /tmp/crowdmike-audit-a-probe.o
sort -t $'\t' -k2,2nr /tmp/crowdmike-audit-a-probe.su | head -16
```

Other candidate frames were compiled from `/tmp` with the same flags and absolute include/source paths (one compiler invocation):

```bash
c++ -std=c++20 -O2 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DNDEBUG -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -I /workspace/crowdmike/build/_deps/juce-src/modules -I /workspace/crowdmike/Source -I /workspace/crowdmike/tests -I /workspace/crowdmike/tests/engine -fstack-usage -c /workspace/crowdmike/tests/engine/CoreTest.cpp /workspace/crowdmike/tests/engine/BeatSyncEngineTest.cpp /workspace/crowdmike/tests/engine/FxNewWiringTest.cpp /workspace/crowdmike/tests/engine/DuckerEngineTest.cpp /workspace/crowdmike/tests/engine/WrapperAuditTest.cpp /workspace/crowdmike/tests/engine/ShowReportServiceTest.cpp
```

| Next-largest release frame | Bytes |
|---|---:|
| FxNewWiring matrix persistence | 826288 |
| Core state migration | 826064 |
| BeatSync old-tempo-state migration | 825968 |
| Ducker maximum | 551072 |
| ShowReportService maximum | 550944 |
| WrapperAudit maximum | 550768 |

All engine tests, Rig/Runner fixtures, large arrays and callback captures were searched. No second four-processor test or >1 MiB candidate was found. Scoped sequential fixtures reuse storage in these optimized compiler probes.

## Minimal fix and runtime verification

Heap-allocate the four processors in the identified AuditA test (unique_ptr); preserve existing assertions and source/destination lifetimes. No production change or executable stack-size increase is needed.

```bash
ulimit -s 1024
xvfb-run -a build/crowdmike_tests audit_state_without_scenes
```

Run before and after the fixture change, then the full suite under the same 1 MiB limit. The built executable location may differ; use the configured target path. Windows CI remains the authoritative platform check.


---

# Macro snapshot regression portability diagnosis

## Native failure

The macOS job `124463025275` on PR 9 failed only `macro_snapshot_concurrent_publication_keeps_complete_mapping` (`MacroSnapshotTest.cpp`, former line 40): one failure among 175 processor tests. The failed flag combined an exact hard-coded -60/+24 dB endpoint check with a separate exact equality check across all 16 input channels, so the original assertion did not distinguish numerical endpoint rounding from a mixed snapshot.

The shared three-slot snapshot exchange's deterministic held-snapshot and coherent-generation checks did not identify an ownership defect. The endpoint check assumed JUCE's interval snapping always returns the exact decimal endpoint `24.0f`; that assumption is not portable across fused and separate multiply/add evaluation.

## Local reproduction

JUCE 9's `NormalisableRange::snapToLegalValue()` computes:

```cpp
v = start + interval * std::floor((v - start) / interval + 0.5f);
return (v <= start || end <= start) ? start : (v >= end ? end : v);
```

The input trim range is `[-60, 24]` with interval `0.01f`. Its top endpoint uses a step count of 8,400. Because `0.01f` is a binary approximation, a fused multiply/add yields a value just below 24, which the final range clamp leaves unchanged. Separate operations round the multiplication to 84 before adding -60 and yield exactly 24.

`work/macro-endpoint-probe.cpp` reproduces the JUCE snapping expression with runtime float inputs. Both binaries were built with GCC 14.2.0 on the same FMA-capable Linux CPU:

```sh
g++ -std=c++17 -O3 -mfma -ffp-contract=fast work/macro-endpoint-probe.cpp -o work/macro-endpoint-fast
./work/macro-endpoint-fast
g++ -std=c++17 -O3 -mfma -ffp-contract=off work/macro-endpoint-probe.cpp -o work/macro-endpoint-off
./work/macro-endpoint-off
```

Observed output:

```text
# contraction enabled
snapped=23.999998092651367 fused=23.999998092651367 exact-endpoint=0
all-16-equal=1
# contraction disabled
snapped=24 fused=23.999998092651367 exact-endpoint=1
all-16-equal=1
```

`std::fma(0.01f, 8400.0f, -60.0f)` independently gives the same lower value as the contracted expression. Disassembly of the contracted binary contains `vfmadd132ss`. Thus the old endpoint assertion can fail while all 16 channels remain exactly coherent; no concurrency or snapshot failure is needed to reproduce it.

## Test contract correction

Only `tests/engine/MacroSnapshotTest.cpp` changed. Before starting either thread, the test obtains each target parameter's exact natural-unit endpoints through its public `RangedAudioParameter::convertFrom0to1(0.0f/1.0f)` API. The concurrent loop now verifies:

- Every channel equals one of that target's two canonical endpoint values, using exact equality.
- Every channel equals the first channel, also using exact equality.

The two results have separate assertions, `validEndpoints` and `coherentMapping`, so future failures identify whether an unsupported value appeared or two complete mapping generations were mixed. A stale unmodulated default still fails endpoint membership. A block mixing low and high mappings still fails channel coherence. There is no epsilon, test skip, change to publication load/counts, or production change.

Deriving endpoint values from the public range API isolates this snapshot ownership test from platform-specific arithmetic inside JUCE. It does not relax the exact comparison of the observed audio-block values.

## Verification status

Both local arithmetic reproductions completed as shown, and `git diff --check` passes. The parent task owns the focused test rebuild and the required new native macOS CI result. The previous macOS failure is not recorded as fixed until that native rerun passes.

---

# Record header caption correction

The native Windows CI build passed and ran all 175 engine tests. Its only failure was the existing UI layout audit: `rec.autoAssign` measured `Auto-assign` at 82.9247 px, but the minimum 270 px header-action row gave each of its three equal-weight buttons 86 px. With the audit's unchanged 4 px caption padding, only 82 px was available. Seven supported size/scale combinations exposed the same deficit.

The real `CMLookAndFeel` uses a 15 px bold font for this 34 px button row. A focused Linux JUCE glyph probe measured all three captions before the change:

| Caption | Glyph width | Width including audit padding |
| --- | ---: | ---: |
| Record all | 64.9168 px | 68.9168 px |
| Split file | 52.2180 px | 56.2180 px |
| Auto-assign | 77.5764 px | 81.5764 px |

The native Windows Auto-assign width is larger than Linux's, which explains the platform-only failure. The correction increases the existing row's minimum width from 270 to 288 px and retains all three equal weights. After two existing 6 px gaps, each button receives 92 px and provides 88 px of caption space, exceeding the reported Windows requirement by 5.0753 px. All three buttons gain 6 px; no other caption is narrowed. The row's maximum width, controls, handlers, text, spacing and flow remain unchanged. The central readout region loses 18 px at narrow layouts but retains positive dimensions; the existing broad UI audit verifies sibling bounds and overlaps across supported modes.

Production change: Source/UI/Pages/RecordPage.cpp only. UiAuditTest assertions remain unchanged. Local focused verification results will be appended after execution; native Windows GREEN requires the root's CI rerun.

Local verification completed: `cmake --build build --target crowdmike_tests --parallel 4` rebuilt RecordPage.cpp and linked successfully. `build/crowdmike_tests ui_audit` passed all 5 existing tests, including the full supported size/scale/contrast layout matrix. Logs: work/record-header-build.log and work/record-header-ui-audit.log. `git diff --check` passes. No test or assertion was changed.
