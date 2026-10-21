# Feedback Assistant

> The feedback assistant finds narrow, persistent peaks in the monitored signal, ranks them, and suggests a notch for each one.
> It is advisory. It only changes audio when you press apply, and it is not yet validated on real rooms.

## What it looks at

The detector runs an FFT (4096 points, Hann window, 50 percent overlap) over the monitoring signal and tracks peaks over time. A peak is only reported when it is narrow and stays put. Music and crowd roar move around, so they rarely qualify.

It analyses in the background. The analysis thread runs while you have the Diagnostics page or the advanced section of the Inputs page open, and while the Show Report is tracking. Otherwise it is not running, which saves CPU.

## The feedback view

The Diagnostics page shows a spectrum with the stable peaks marked, and a list of candidates. For each candidate:

| Field | Meaning |
|---|---|
| Frequency | Centre of the peak in Hz |
| Severity | 0 to 1. Higher means louder relative to the surroundings. |
| Persistence | How long the peak has stayed stable, in seconds |
| Input or group | Where it is coming from, where that can be inferred |
| Suggested notch | A suggested Q and depth for the notch |
| Apply | Sets a notch with those values |

Candidates are ranked, and only peaks that persist are listed, so a single loud transient does not appear as feedback.

## Applying a notch

Press **apply** on a candidate to set a notch with the suggested frequency, Q, and depth. The notch is an input notch (or a notch band in an EQ) with these ranges:

| Setting | Range |
|---|---|
| Frequency | 100 Hz to 10 kHz |
| Q | 2 to 60 (narrow) |
| Depth | 0 to -24 dB |

The assistant never inserts a notch on its own. Automatic dynamic notches are on the list of future work. After you apply a notch, check which input or band changed on the Inputs page, and listen to see whether the ring is gone.

## A workflow that works

1. Bring the PA up to a level where the room starts to ring, and no further.
2. Open Diagnostics and watch the candidates.
3. Apply the strongest candidate with a persistent peak. Give the notch a second or two to settle.
4. Check the Live page feedback-risk indicator on that input. It should drop.
5. Repeat for the next candidate, and stop when the candidates are weak or flicker.
6. Close the gap with trims and the high-pass before you add more notches. Notches only deal with a few frequencies.

## Limits

- The assistant has been tuned on synthetic signals. Its behaviour in a real room with real microphones is still to be checked.
- It does not know where your speakers and mics are, so it cannot tell you for certain which path is ringing.
- A detected peak is not always feedback. A loud, steady tone on stage, such as a keyboard or a PA hum, looks similar.
- Peaks can be below the detector's threshold while still being audible. Ears and the Live page meters are the final check.

## Feedback and the show report

Feedback candidates with a severity of 0.5 or more are logged as events in the [[Show Report|Show-Report]], with their time and frequency. This gives you a record of when the room rang during a show.

## Related

[[Concepts|Concepts]] for why delay does not prevent feedback. [[Input Channel Strip|Input-Channel-Strip]] for the notch and EQ controls. [[Safety, Panic and Limiters|Safety-Panic-and-Limiters]] for what to do when it goes wrong quickly.
