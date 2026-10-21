# Beat Sync and Tempo

> Tempo-synced effects and triggers read one tempo, chosen with the Tempo Source: the host, a tap, a typed BPM, MIDI clock, or automatic detection from a mic.
> Triggers can snap to the beat, so an echo throw or reverse lands on the grid.

## Tempo sources

| Source | Where the BPM comes from | Notes |
|---|---|---|
| Host (default) | The DAW or host playhead | Existing shows behave exactly as before. In standalone there is no host, so it reports no tempo. |
| Tap | Your taps on the TAP button, the tap parameter, or OSC | Free-running beat clock, always "playing" |
| Manual | The Manual BPM you type (20 to 300) | Always available, free-running |
| MIDI Clock | Incoming MIDI clock (24 ticks per quarter note) | Follows Start, Continue, and Stop messages |
| Auto | Detected from an input (Master or Input 1 to 16) | Locks to the detected beat and holds the lock while confidence is low |

Set the source on the Settings page, in the **Tempo / Beat Sync** section, or with the source selector in the status bar.

## Status bar

The second row of the status bar shows:

- the source selector,
- the BPM read-out (`--.-` when there is no tempo, with a `?` when the confidence is low),
- the **TAP** button,
- a beat LED that lights during the first fifth of each beat, with the accent colour on the downbeat,
- a confidence bar,
- the trigger quantise selector.

## Tap tempo

- Tap at least twice. A tempo needs two taps.
- A gap of more than 2.5 seconds starts a new sequence, and the old tempo stays until a new one is known.
- Taps closer than 0.2 seconds count as bounce and are ignored.
- With enough taps, outliers more than 25 percent away from the median are rejected.
- A tap also snaps the beat grid to the nearest whole beat.

## Nudge and resync

- **Nudge** adds -10 to +10 BPM to the Tap, Manual, and Auto tempos. Use it to ride a drifting band. The result stays between 20 and 300 BPM.
- **Resync** (the Resync Downbeat button, OSC, or a MIDI binding) sets the current position to beat 1 at the next block. Use it when the band lands on a new bar.

Host and MIDI clock sources ignore resync, because they have their own position.

## Beats per bar

Set the beats per bar to 4, 3, 2, 5, 6, or 7 on the Settings page. This sets how long a "1 bar" trigger grid is.

## Auto tempo

Auto tempo listens to one input (or the master) and estimates the tempo and beat phase.

- It looks at roughly the last 8 seconds of onsets and re-estimates every half second.
- It favours tempos between 60 and 200 BPM, and avoids jumping between half and double time.
- A new tempo has to win on consecutive estimates before the lock moves, so small drifts are followed smoothly.
- If the input goes silent or aperiodic, the tempo is held rather than reset, and the status shows low confidence.

Auto tempo works best with a steady kick, click, or clear pulse. A crowd with no regular pulse gives low confidence, and then the held tempo is the one in use.

## Trigger quantise

Trigger quantise makes an effect start on a grid line instead of immediately.

| Setting | Grid |
|---|---|
| Off | Triggers happen at once (the default) |
| 1/4 | Next beat |
| 1/2 | Next two beats |
| 1 bar | Next bar, using the beats per bar setting |

Quantise applies to:

- echo throw trigger,
- reverse chunk trigger rises,
- clip triggers on the Playback page.

A trigger waits for the grid line sample by sample, so it lands exactly on the line. Requests made while one is waiting combine into one. A pending trigger never waits longer than two bars and one beat. If the transport is not playing, or there is no tempo, triggers happen at once.

Clips also have their own sync settings on the Playback page: a sync grid of 1/16 to 2 bars, and an optional loop length in beats.

## MIDI and OSC

| Address | Effect |
|---|---|
| `/crowdmike/tempo/tap` | Tap |
| `/crowdmike/tempo/resync` (or `/crowdmike/tempo/downbeat`) | Resync downbeat |
| `/crowdmike/tempo/bpm <20..300>` | Manual BPM |
| `/crowdmike/tempo/source <0..4>` | Source (Host, Tap, Manual, MIDI Clock, Auto) |
| `/crowdmike/tempo/quantise <0..3>` | Trigger quantise |
| `/crowdmike/tempo/nudge <-10..10>` | Nudge |
| `/crowdmike/tempo/autoSource <0..16>` | Auto input (0 = master) |
| `/crowdmike/tempo/barBeats <0..5>` | Beats per bar (index into 4, 3, 2, 5, 6, 7) |

MIDI bindings can use the Tap and Resync targets, and any tempo parameter can be bound like any other parameter. See [[MIDI and OSC|MIDI-and-OSC]].

## Tips

- Use Host when a DAW is driving the show. Use Tap or Manual for standalone shows.
- Set the Manual BPM before the set, so there is a sensible tempo if the others drop out.
- Quantise your echo throws to the bar when the band is on a steady beat. Leave it off for a freer feel.
