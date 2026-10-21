# Quick Start and Line Check

> Quick start picks a wiring template for your channel count and turns on a sensible high-pass filter. Line Check listens for 10 seconds and proposes trims, filters, and polarity fixes that you apply or undo.
> Nothing changes until you press Apply.

## Quick start

Quick start is the fast way to get a first working setup. Press **Quick start...** in the Line Check card on the Inputs page.

It does two things:

1. Applies the wiring template that matches your input and output count (table below).
2. Switches on the high-pass filter at 100 Hz on every active input whose high-pass was off. Trims are not touched.

It is one undo step. The routing part is not in the undo history, so use the Routing page if you want to change it back.

| Condition | Template chosen |
|---|---|
| 1 input | 1 mic > stereo PA |
| 2 inputs | 2 mic stereo crowd > stereo PA |
| 3 or 4 inputs | Four mic room |
| 8 or more inputs, and at least 8 outputs | Venue multi-zone |
| 5 or more inputs, and at least 4 outputs | Front/rear crowd > PA + broadcast |
| Anything else (for example 5 or more inputs with fewer than 4 outputs) | Four mic room |

See [[Routing and Wiring Templates|Routing-and-Wiring-Templates]] for what each template sets.

## Line Check

Line Check measures every active input and tells you what it sees. Open it with the **Line Check** button in the top bar of the Inputs page. The card opens at the bottom of the page and works alongside the other Inputs controls, so you can keep mixing while it listens.

### Steps

1. Press **Start 10 s listen**.
2. Talk, clap, or let the crowd make noise for the full 10 seconds. You can press **Cancel** at any time. Stopping early is allowed once at least 2 seconds have been measured.
3. Read the results table.
4. Press **Apply** on each row you agree with, or **Apply all**.
5. Press **Undo** if you change your mind. One Undo reverts the whole apply.
6. Press **Re-run** after you move a mic, change a gain stage, or swap a cable.

Set the **target level** box if you want a level other than the default -20 dBFS (range -30 to -10 dBFS).

### What the columns mean

| Column | Meaning |
|---|---|
| `#` and name | Input number and its name |
| Verdict | The main finding, shown as text and a glyph (not colour alone) |
| Programme dBFS | Loudness of the loudest 30 percent of 100 ms frames |
| Peak dBFS | Highest sample seen |
| Noise floor dBFS | Quiet-frame level, a rough idea of the hiss or hum floor |
| Flags | Extra notes such as "DC offset", "Hum 50 Hz", or "Polarity vs 1" |
| Trim now to proposed | Current trim and the proposed trim |
| HPF suggestion | Suggested high-pass on or off, and frequency |
| Apply, Flip polarity | Per-input actions. Flip polarity only appears when a mismatch is flagged |

### Verdicts

| Verdict | Rule of thumb |
|---|---|
| Dead | Essentially no signal. No trim is proposed. A constant DC with no sound is Dead, plus a DC flag. |
| Clipping | At least one sample reached full scale. A clipping input is never raised. |
| Hot | Peak at or above about -3 dBFS, or average level above -10 dBFS |
| Low | Average level more than 14 dB below the -20 dBFS target |
| OK | Between about -34 and -10 dBFS average, with peaks below -3 dBFS |

### Flags

- **DC offset**: the average is 0.01 or more (about -40 dBFS). Check the wiring or the preamp.
- **Hum suspected**: strong 50 Hz or 60 Hz energy in the quiet frames. The suggested high-pass goes to 150 Hz for hum, and 100 Hz otherwise. A high-pass cuts the fundamental but does not fix the hum. Check grounding and direct-injection boxes.
- **Polarity mismatch vs input N**: two neighbouring inputs carry the same sound with opposite sign. This can be a wiring fault, or two mics that really do face each other. The app only suggests a flip. You decide.

## How the trim is proposed

The proposal aims the average level at the target and stops the loudest peak at about -1 dBFS. Trims are limited to +/-24 dB in the proposal. Two rules keep it safe:

- If an input clips, the proposal is never higher than the current trim.
- The proposal is an absolute value, because the probe reads the signal before trim. Running Line Check twice with different trims gives the same proposal.

Dead inputs get no proposal.

## Tips

- Run Line Check with the real audience or a representative sound, not a single tap on the mic.
- Fix hum and polarity before you touch the trims. Level proposals on a humming input are not meaningful.
- After Apply, check the Live page meters and the feedback-risk indicator before you open the PA up.

## Related

[[Input Channel Strip|Input-Channel-Strip]] for the trim, polarity, and filter controls Line Check changes. [[Feedback Assistant|Feedback-Assistant]] for what to do when the room starts to ring.
