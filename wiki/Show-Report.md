# Show Report

> The Show Report samples crowd level once a second, finds the big moments, and writes an HTML, CSV, and JSON report next to your recording.
> Levels are dBFS on the engine's crowd bus, not calibrated SPL, so use the numbers to compare moments in one show, not to meet a noise limit.

## What it records

Once a second, the engine's meters are sampled and stored. Nothing here runs on the audio thread.

- Crowd level (300 ms RMS of the crowd groups, before effects)
- Master level and approximate short-term loudness (LUFS-S)
- Master sample peak
- Events: output clip episodes, limiter episodes, panic presses, echo throws, reverse triggers, freezes, feedback candidates, scene recalls, and xruns

The series is kept for up to 12 hours at 1 Hz. Longer shows are merged in pairs, so memory stays bounded and the whole show is still covered.

## Applause and energy peaks

A peak detector looks at the crowd level against a slowly moving baseline. A peak is a rise of several dB above the baseline, followed by a fall that confirms it. Peaks are at least about 28 seconds apart by default, and a peak must be above -60 dBFS.

| Setting | Range | Default | Effect |
|---|---|---|---|
| Peak sensitivity | 0 to 100 percent | 50 percent | Higher finds more peaks |

At the default setting, the detector needs about 7.5 dB of rise and at least 28.5 seconds between peaks. Sensitivity changes both.

Each accepted peak appears in the report. With **Auto markers** on and a take recording, a marker `Crowd peak +7.4 dB` is also placed on the recording at the peak time. These markers use the normal marker store, so they show up in exported marker files.

## Turning it on and off

On the Record page, in the Show Report section:

| Control | What it does |
|---|---|
| Open folder | Opens the last report folder, or the record folder |
| Export now | Writes a report now (works even when auto-export is off) |
| Auto-export | Writes the report when a take stops and on standalone exit |
| Auto markers | Drops crowd-peak markers while recording |
| Peak sensitivity | The slider above |
| Status line | Tracking time, peak count, and the last export result |

The report's enabled setting is stored in the show and turns tracking off completely. Tracking keeps the feedback analysis running, so switch it off if you do not need the report.

## What the report covers

The report covers the take, from the start of recording to the stop. With no take, it covers everything since the app started (or since the last reset).

| Section | Contents |
|---|---|
| Summary | Duration, start and end (local time with UTC offset), show name, venue, scene at start, average and peak crowd level, peak count, approximate LUFS-S max and mean, max master peak, recorded time, totals for clips, limiter, feedback, panic, and xruns, FX use, and scenes used |
| Top moments | The five strongest peaks, with timecode, wall-clock time, level, and prominence |
| Timeline | Crowd level, master level, peaks, recording span, scene recalls, and event ticks |
| Feedback events | Time, frequency, and severity of each new feedback candidate |
| User markers | Your markers with their labels |
| Device | Driver, device name, sample rate, buffer size, estimated latency, channel counts, and app version |

## Files

Three files are written with the same base name: `show_report.html`, `show_report.csv`, and `show_report.json`.

- **HTML** is a single self-contained file with inline styles and an inline timeline. It has no scripts and no external resources, prints well, and uses dark styling when the system is set to dark. Marks are told apart by shape and label, not colour alone.
- **CSV** has one row per sample, with a header line. Numbers use a dot as the decimal point, whatever the locale.
- **JSON** is schema version 1. New fields are only added, never removed, so readers can ignore keys they do not know.

The files go into the take's session folder. Without a session, they go to `Reports/<date and time>` inside the record folder (default `~/Music/CrowdMike`). If another take already shares the folder, the base name includes the take, so takes never overwrite each other. Exporting the same take again overwrites its own files.

Files are written to a temporary name and then renamed, so a failed export never leaves half a file behind. A failed export shows an error and does not affect recording or audio.

## When exports happen

1. **Recorder stop**, when auto-export is on.
2. **Export now**, at any time.
3. **App exit** in the standalone app, when auto-export is on, the history has changed since the last export, and the history is at least 30 seconds long.

A report with no samples yet is refused, and the status says so.

## Notes

- The loudness figure is an approximation of EBU R128 short-term loudness. It has no gating, so expect it to be within about half a loudness unit on steady material.
- Levels are dBFS, not dB SPL. A microphone's distance and gain change the number, so compare within one show.
- All user-typed text (show, venue, scene, marker labels, device names) is escaped in the HTML, so odd characters cannot break the file.

## Related

[[Recording and Playback|Recording-and-Playback]] for takes and markers. [[Feedback Assistant|Feedback-Assistant]] for the feedback candidates that appear in the report.
