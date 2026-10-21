# Crowd Energy Tracker + Show Report (SHOW_REPORT_SPEC)

Status: implemented on branch `feat/show-report`. Schema version of the report files: **1**.

## 1. Purpose

Performers, venues and promoters want proof of the night: how loud and how energetic the crowd was, when the big moments
were, what effects and scenes were used and whether anything went wrong. CrowdMike already measures all of this inside the
engine. This feature samples those meters once per second, detects applause/energy peaks, optionally drops recorder markers
on them and writes a self-contained report next to the recording.

Non-goals: calibrated SPL (levels are dBFS on the engine's crowd bus, not dB SPL), cloud upload, audio analysis beyond the
meters named below.

## 2. Module layout and contracts

| Module | Path | Depends on | Contract |
|---|---|---|---|
| LoudnessMeter | `Source/DSP/LoudnessMeter.*` | std | Audio-thread K-weighted short-term loudness + 400 ms RMS + resettable peak. |
| Report model | `Source/Report/ReportTypes.h` | std | Plain data (`Sample`, `PeakEvent`, `ReportData`, ...). No JUCE. |
| PeakDetector | `Source/Report/PeakDetector.*` | std | Online applause-peak detector over a 1 Hz level series. No JUCE. |
| EnergyTracker | `Source/Report/EnergyTracker.*` | std | Bounded 1 Hz series, counters to deltas, events, peak detection, `buildReport()`. No JUCE. |
| Writers | `Source/Report/ReportWriters.*` | std | Pure functions `renderHtml/renderCsv/renderJson/escapeHtml/escapeJson`. No JUCE. |
| ShowReportService | `Source/Report/ShowReportService.*` | JUCE core, Engine meters, RecorderEngine, ShowModel | Message-thread adapter: reads meters, drives the tracker, drops markers, exports files. |
| UI | `Source/UI/ReportPanels.*` | UI kit, service | `EnergySparkline` (Live page), `ShowReportPanel` (Record page). Read-only views of the service. |

Threading: the engine only increments relaxed atomics (section 3). Everything else (sampling, detection, export) runs on the
message thread from the processor's existing 20 Hz timer; the service gates itself to 1 Hz. Nothing in `Source/Report` ever
runs on the audio thread. Exports write files on the calling (message) thread; a report is a few hundred KB at most
(12 h of 1 Hz data is about 43 200 rows).

## 3. Engine additions (audio thread, additive)

`EngineMeters` gains (all relaxed atomics, single writer = audio thread):

| Field | Meaning |
|---|---|
| `crowdLevelDb` | `DensityFollower::getLevelDb()`: 300 ms RMS (channel-mean power) of the summed crowd groups, pre-FX, dBFS. This is the "crowd level". Floor -120. |
| `masterRmsDb` | Unweighted 400 ms RMS of the master bus (mean of L/R power), dBFS. Floor -120. |
| `shortTermLufs` | K-weighted short-term loudness, section 4. Floor -120. |
| `reportMasterPeak` | `AtomicPeak` of the master bus. Reset only by the tracker (independent of the Live page meters). |
| `events.clipEvents` | Output clip episodes: rising edge of "any output channel reached 1.0 in this chunk". |
| `events.limiterEvents` | Limiter episodes: master or any output limiter gain reduction rises above 1.0 dB (re-arms below 0.3 dB). |
| `events.panicEvents` | Rising edges of the Panic parameter. |
| `events.echoThrows` | Rising edges of the Echo Trigger parameter. |
| `events.reverseTriggers` | `ReverseChunk::getTriggerCount()` (all trigger modes). Absolute count; decreases when the engine is re-prepared. |
| `events.freezeEvents` | Rising edges of the Freeze parameter. |

Counters are `uint32` and cumulative since process start. The tracker converts them to per-second deltas; a counter that goes
backwards is treated as a reset (delta 0, rebased). Edge state is audio-thread private; the first processed chunk after
`prepare()` latches the current parameter values without counting (a restored "on" value never counts).

Crowd groups: the density follower is fed the sum of all eight group buses (plus playback route 0) before FX returns, which is
what the engine already calls the crowd signal. Groups muted/soloed out of the mix are naturally excluded.

## 4. LoudnessMeter (K-weighted approximation)

`dsp::LoudnessMeter` follows ITU-R BS.1770 stage 1 (high-shelf, +4 dB at ~1.68 kHz) and stage 2 (RLB high-pass, ~38 Hz) using
the standard bilinear-transform formulas for any sample rate (44.1-192 kHz). Per sample, the K-weighted power of both master
channels is averaged (L and R weights are 1.0) and accumulated in a **3 s sliding window** (short-term). Output:
`LUFS = -0.691 + 10 log10(meanSquare)`. It is an *approximation* of EBU R128 short-term loudness because there is no gating
(absolute/relative) and the window is advanced per 1/10 s sub-block rather than per sample; for steady program material it is
within about 0.5 LU of a reference meter (test: a 997 Hz sine at -20 dBFS peak in both channels reads -23.0 +/- 0.3 LUFS at
44.1/48/96/192 kHz). Values are floored at -120. Reports label it "approx. LUFS-S".

API: `prepare(sr)`, `reset()`, `push(const float* l, const float* r, int n) noexcept` (no allocation, finite for any input,
non-finite samples count as 0), `getShortTermLufs()`, `getMomentaryRmsDb()` (400 ms unweighted), all relaxed-atomic reads.

## 5. Sample, tracker and bounded memory

`Sample` (one per second, fields in this order in CSV/JSON):

| Field | Unit | Merge rule when decimating |
|---|---|---|
| `t` | s since window start (double) | time of first merged sample |
| `crowdDb` | dBFS RMS | power mean |
| `density` | 0..1 | mean |
| `masterDb` | dBFS RMS | power mean |
| `masterPeakDb` | dBFS sample peak | max |
| `lufs` | approx. LUFS-S | power mean |
| `clip`, `limiter`, `feedback`, `panic`, `fx`, `scene`, `xrun` | event counts in the interval | sum |
| `recording` | 0/1 | any |

`EnergyTracker::push(Input)` is called once per second with absolute time in seconds, instantaneous levels, cumulative
counters and up to 4 feedback candidates. It returns what happened (new peak, new feedback events).

Memory bound: the series holds at most `maxSamples` (default **43 200 = 12 h at 1 Hz**). When a flush would exceed the cap the
series is halved in place by merging adjacent pairs and the step doubles (1 s, 2 s, 4 s ...), so memory stays constant for any
show length and the whole show is always covered. The peak list is capped at 2 000 entries (lowest prominence dropped), the
event lists (scene recalls, feedback, markers) at 500 each. `getStepSeconds()` reports the current step.

Feedback events: a candidate (from `FeedbackDetector::getCandidates`) with severity >= 0.5 whose frequency is not within 1/6
octave of a candidate in the previous sample is a new event `{t, frequencyHz, severity}`. The service keeps the analysis worker
acquired while tracking is enabled so candidates are fresh.

xruns: the standalone device xrun count when `HostServices` exists, otherwise the engine `overruns` counter.

## 6. Applause / energy peak detector

Operates on the 1 Hz `crowdDb` series (floored at -120). Parameters:

| Parameter | Default | Range | Meaning |
|---|---|---|---|
| `prominenceDb` | 7.5 | 3..12 | Minimum rise of the smoothed level above the adaptive baseline. |
| `minSpacingSec` | 28.5 | 12..45 | Minimum time between accepted peaks. |
| `minLevelDb` | -60 | fixed | A peak must exceed this absolute level (ignores swells in silence). |
| `smooth` | 0.5 | fixed | `y += 0.5 (x - y)` (about 1 s lag). |
| `baselineRiseSec` | 60 | fixed | Baseline time constant when the level is above it. |
| `baselineFallSec` | 8 | fixed | Baseline time constant when the level is below it. |
| `confirmDropDb` | max(2, prominence/3) | derived | Fall from the swell maximum that confirms a peak. |

Sensitivity setting `s` in 0..1 (default 0.5, higher = more peaks): `prominenceDb = 12 - 9 s`, `minSpacingSec = 45 - 33 s`.

Algorithm per sample at time `t`:

1. `y` = smoothed level; `base` follows `y` with `alpha = 1 - exp(-1/tau)` where `tau` is `baselineRiseSec` if `y > base`
   else `baselineFallSec`. The first sample initialises `y = base = x`. While `y < minLevelDb` the baseline snaps to `y`
   (so a crowd arriving out of silence is not reported as a swell).
2. `excursion = y - base`. State Idle: if `excursion >= prominenceDb` and `y >= minLevelDb` enter Swell with
   `ymax = y, tmax = t, baseAtMax = base`.
3. State Swell: if `y > ymax` update `ymax, tmax, baseAtMax`. The swell ends (peak confirmed at time `t`) when
   `y <= ymax - confirmDropDb` or `excursion < prominenceDb / 2`.
4. A confirmed peak is `PeakEvent{ t = tmax, levelDb = ymax, prominenceDb = ymax - baseAtMax, confirmedAt = t }`. If
   `tmax - lastAcceptedT < minSpacingSec` it is discarded and `peaksSuppressed` increments, otherwise it is accepted.
5. Detection is deterministic and independent of block size or sample rate (input is the 1 Hz series).

Marker: when a peak is accepted, auto markers are enabled and the recorder is recording, one marker
`"Crowd peak +7.4 dB"` (prominence, one decimal) of type `Applause` is added at the recorder frame of `tmax`, i.e. the
current frame minus `(confirmedAt - tmax)` seconds (clamped to >= 0). Peaks are always recorded in the report whether or not
a marker was dropped. Auto markers are not repeated in the report's user-marker list.

## 7. Settings (ShowModel, non-parameter)

`ShowModel::report` (`ReportSettings`), serialised as child `Report` of the `Show` tree. A missing child (all states saved
before this feature) yields the defaults, so older saves load identically. No DAW-automatable parameter is added; parameter
IDs are untouched.

| Property | Type | Default | Range / notes |
|---|---|---|---|
| `enabled` | bool | true | Tracking on/off. Off stops sampling and releases the analysis worker. |
| `autoMarkers` | bool | true | Drop `Crowd peak` markers while recording. |
| `autoExport` | bool | true | Export on recorder stop and on standalone exit. |
| `peakSensitivity` | float | 0.5 | Clamped to 0..1, non-finite values reset to 0.5. |

## 8. Report window and export triggers

The tracker keeps the history since the app started (or `reset()`); a report covers a **window** of it, with `t = 0` at the
window start:

* While a take is recording, the window is `[take start, now]`; after it stops it stays `[take start, take stop]`.
* With no take yet, the window is the whole history.

Triggers (`ShowReportService::exportReport(reason)`):

1. **Recorder stop**: the recording state falls (after post-roll) and `autoExport` is on: export the finished take into the
   folder of the take's session JSON.
2. **On demand**: "Export now" on the Record page (always allowed, ignores `autoExport`). While recording it exports the take so
   far; otherwise the last take, or the whole history if there is none.
3. **App exit**: `ShowReportService::shutdown(exportIfDirty)` (called from the processor destructor): an open take is
   finalised with `stopRecordingNow()` and exported (standalone and plug-in). Only the standalone app passes
   `exportIfDirty = true`: then, if `autoExport` is on and the history changed since the last export and covers >= 30 s, it is
   exported even when nothing was recorded.

Folder: the session folder (parent of the take's session JSON). Without a session: `<record folder or ~/Music/CrowdMike>/Reports/<yyyy-MM-dd_HH-mm-ss>`.

File names: `show_report.html|csv|json`. If the folder already holds a `show_report.json` whose `session.file` differs from
the current take's session JSON name (another take shares the record folder), the base name becomes
`show_report_<session stem without "_session">` so takes never overwrite each other. Re-exporting the same take overwrites its
own files. Files are written with `File::replaceWithData` (temp file + rename); a failure is reported in `ExportResult::error`
and in the status bar, never thrown, and never affects recording or audio.

## 9. Report content

Summary (all derived from the window): duration, start/end (local time with UTC offset), show, venue, scene at start, average
crowd level (power mean over samples >= -70 dBFS; -120 when none), peak crowd level and when, peak count (+ suppressed),
top-5 moments (ranked by prominence, ties by earlier time: rank, timecode, wall-clock time, level, prominence), approx.
LUFS-S max and mean, max master sample peak, time recorded, clip/limiter/feedback/panic/xrun totals, FX usage (echo throws,
reverse triggers, freezes), scenes used (name, recalls, first time), feedback events (time, Hz, severity), user markers, and the
device block: driver, device, sample rate, buffer, estimated total latency (samples and ms), channel counts, app version.
For shows longer than 12 h the series is decimated; `peakCrowdDb` is then `max(series max, peak levels)`.

### 9.1 HTML (`show_report.html`)

Single self-contained file: inline CSS (light + `prefers-color-scheme: dark`, print friendly), no scripts, no external
resources. Contains `<title>`, header, summary table, one inline `<svg>` timeline (crowd level area/line, master level line,
numbered triangles for the top-5 peaks and plain triangles for other peaks, dashed lines for user markers, a band for the
recording span, ticks for scene recalls and a row of event ticks for clip/feedback/panic/xrun; every mark has a `<title>`),
top-5 table, FX/scene/event tables, device table and a footer stating the schema version and that levels are dBFS, not SPL.
Marks are distinguishable by shape and label, not colour only. The timeline is downsampled to at most 960 columns (max per
column). **Every user-supplied string (show, venue, scene, marker labels, device names, file names) is HTML-escaped** (`& < > " '`),
control characters are removed and invalid UTF-8 is replaced.

### 9.2 CSV (`show_report.csv`)

UTF-8, `\n` line endings, header
`time_s,crowd_db,density,master_db,master_peak_db,lufs_s,clip,limiter,feedback,panic,fx,scene,xrun,recording`,
one row per series sample (1 Hz up to 12 h, then every `stepSeconds`). Numbers only (no user strings), `.` decimal point
regardless of locale, dB with 2 decimals, density 3, counts integers. `time_s` has 1 decimal.

### 9.3 JSON (`show_report.json`) - schema version 1

```
{
  "schema": "crowdmike.show-report",
  "schemaVersion": 1,
  "generator": { "app": "CrowdMike", "version": "<string>" },
  "show": { "name": "", "venue": "", "scene": "", "start": "<ISO-8601 local>", "end": "<ISO-8601 local>",
            "durationSeconds": 0.0 },
  "session": { "file": "<session json file name or empty>", "take": 0 },
  "device": { "driver": "", "name": "", "sampleRate": 0, "bufferSize": 0, "latencySamples": 0, "latencyMs": 0.0,
              "inputs": 0, "outputs": 0 },
  "settings": { "peakSensitivity": 0.5, "peakProminenceDb": 7.5, "peakMinSpacingSeconds": 28.5, "autoMarkers": true },
  "summary": { "averageCrowdDb": 0.0, "peakCrowdDb": 0.0, "peakCrowdAtSeconds": 0.0, "peakCount": 0, "peaksSuppressed": 0,
               "maxShortTermLufs": 0.0, "meanShortTermLufs": 0.0, "maxMasterPeakDb": 0.0, "recordedSeconds": 0.0,
               "clipEvents": 0, "limiterEvents": 0, "feedbackEvents": 0, "panicEvents": 0, "xruns": 0, "sceneRecalls": 0,
               "fx": { "echoThrows": 0, "reverseTriggers": 0, "freezes": 0, "total": 0 } },
  "topMoments": [ { "rank": 1, "timeSeconds": 0.0, "timecode": "0:00:00", "clock": "21:03:05", "levelDb": 0.0,
                    "prominenceDb": 0.0 } ],
  "peaks": [ { "timeSeconds": 0.0, "levelDb": 0.0, "prominenceDb": 0.0, "marker": true } ],
  "scenes": [ { "name": "", "recalls": 0, "firstSeconds": 0.0 } ],
  "feedbackEvents": [ { "timeSeconds": 0.0, "frequencyHz": 0.0, "severity": 0.0 } ],
  "markers": [ { "timeSeconds": 0.0, "label": "" } ],
  "series": { "stepSeconds": 1, "columns": ["t","crowdDb","density","masterDb","masterPeakDb","lufs","clip","limiter",
              "feedback","panic","fx","scene","xrun","recording"], "rows": [ [0.0, -40.0, 0.2, ...] ] }
}
```

Compatibility rules: fields are only ever added; a reader must ignore unknown keys; `schemaVersion` increases only for a
breaking change. All numbers are finite (non-finite values are written as 0). Strings are escaped per RFC 8259.

## 10. UI

* **Live page**: an `EnergySparkline` at the right end of the flow-ribbon row: the last 120 s of crowd level (line) with peak
  ticks, text `PEAKS n` (peaks in the current window) and the latest level. Updates only while the page is visible
  (component id `live.energy`; child label ids `live.energy.peaks`).
* **Record page**: a "Show Report" section under the track list: `Open folder` (`report.openFolder`, opens the last report
  folder, or the record folder), `Export now` (`report.exportNow`), `Auto-export` toggle (`report.autoExport`), `Auto markers`
  toggle (`report.autoMarkers`), `Tracking` toggle (`report.tracking`, `ShowModel::report.enabled`: off stops sampling and auto markers, the status reads "Tracking off"), `Peak sensitivity` slider 0-100 % (`report.sensitivity`) and a status label
  (`report.status`: tracking time, peak count, last export result). All edits go to `ShowModel::report` through
  `EditorContext::showModelChanged()`.

## 11. Edge cases

* Silence (all samples -120): no peaks, average -120, report still valid.
* Zero samples: export is refused with `error = "No data to report yet"` (UI status says so); no files created.
* Non-finite or out-of-range inputs are clamped (levels to -120..+24 dB) before use.
* Recorder never started: auto markers do nothing; peaks still appear in the report.
* Clock/UTC offset unknown: offset 0 is used and the ISO string ends in `+00:00`.
* Time going backwards or a gap > 10 s between polls: the service resynchronises (fills at most 10 missing seconds with the last
  levels, no counter deltas for the fill).
* Disk full/permission error on export: `ExportResult::ok == false`, message surfaced; tracking continues.
* The processor destructor never throws; export errors on exit are swallowed.

## 12. Acceptance tests (all automated)

1. `LoudnessMeter`: reference sine levels, silence = -120, NaN/inf input finite, block sizes 1..2048, 44.1..192 kHz (DSP tests).
2. Tracker with synthetic swells: peaks at expected times (+/- 2 s), small bumps ignored, spacing suppression, silence gives no
   peaks, sensitivity changes the count.
3. Decimation: 100 000 s into `maxSamples = 1 000` stays <= 1 000 rows, step is a power of two, whole span covered, summary totals
   unchanged by decimation (counts).
4. Counter deltas incl. wrap/reset; feedback event dedupe.
5. Writers: golden HTML/CSV/JSON files for a fixed `ReportData`; JSON parses and has the documented keys; escaping tests with
   `<script>`, quotes, ampersands, control characters and invalid UTF-8; HTML has no `<script`, no `http`; locale independence.
6. Marker injection through a fake `MarkerSink` (label text, seconds-ago offset, not dropped when not recording or disabled).
7. Engine wiring: panic, echo throw, freeze, reverse trigger, limiter and clip counters increment exactly once per edge.
8. Service + processor: polls drive peaks, a real recording gets `Crowd peak` markers, stop exports three files into the session
   folder, a second take does not overwrite the first, on-demand export, shutdown export.
9. State: `Report` settings round-trip; a `Show` tree without `Report` loads defaults and every other field unchanged.
10. UI: panels exist on Live/Record pages, controls edit `ShowModel::report`, snapshot renders at 1100x650 and 2560x1440.
