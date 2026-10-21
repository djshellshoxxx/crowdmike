# Concepts

> Audio goes from inputs, through per-input cleanup, into crowd groups, out to effect returns, then into the master bus and the output matrix.
> Creative delay, alignment delay, and feedback control are separate jobs, and delay alone does not stop feedback.

## Signal flow

```text
Inputs (mics, lines, host channels)
  |
  v
Input chain        trim > polarity > HPF/LPF > notch and EQ > align delay
                   > gate > de-esser > compressor
                   (phase all-pass, transient enhancer, and gain riding also sit here)
  |
  +--> Raw recorder tap (before processing, if armed)
  |
  v
Crowd groups (1 to 8)   trim, balance, HPF/LPF, EQ, compressor, width,
  |                     optional limiter (off by default), delay, FX sends
  +--> Group recorder tap
  |
  v
FX returns (7 parallel slots)   fed from input and group sends, returned to master and the matrix
  |
  v
Performer ducker   (optional) pulls groups and FX returns down while the performer speaks
  |
  v
Master crowd bus   EQ (8 bands) > compressor > width > limiter
  |
  +--> Master recorder tap
  |
  v
Output matrix     one row per source, one column per output
                  each output: trim, mute, polarity, delay, HPF/LPF, limiter
  |
  v
Physical or host outputs (PA, broadcast, monitor, recorder feed)
```

The on-screen flow ribbon on the Live page shows the same stages: Input, Align, Clean, Shape, FX, Master, Output. Click a stage to jump to its editor.

## Inputs, groups, master, outputs

- **Inputs** (up to 16) are the microphones and line sources. Each has its own trim, filters, dynamics, and sends. See [[Input Channel Strip|Input-Channel-Strip]].
- **Groups** (up to 8) are submixes. Put inputs in one or more groups, then treat the group as a crowd zone (front, rear, balcony). See [[Groups and Master|Groups-and-Master]].
- **FX** (7 slots) are send-and-return effects. A send takes a copy of a signal, the effect processes it, and the return brings back only the wet sound. There is no serial FX chain: a slot never feeds another slot. See [[Effects Guide|Effects-Guide]].
- **Master** is the sum of the crowd groups and the FX returns, with its own EQ, compressor, width, and limiter.
- **Outputs** (up to 16 physical or host channels) each take a mix of the buses from the matrix. The matrix is where you decide that the PA gets group 1 and the broadcast feed gets the master, for example. See [[Routing and Wiring Templates|Routing-and-Wiring-Templates]].

## Three kinds of delay

CrowdMike keeps three delay concepts apart in the model and the GUI.

| Kind | What it is for | Where it lives |
|---|---|---|
| Alignment delay | Time-aligning microphones or groups that sit at different distances | Per input (`alignDelayMs`) |
| Reinforcement delay | Intentional lag between the captured crowd and the reinforced crowd | Per group (`delayMs`) and per output |
| Creative delay | Repeats, feedback, and tone shaping as an effect | The digital delay, echo throw, and tape delay slots |

Set alignment first, then reinforcement, then creative effects.

## Feedback and delay: what is and is not guaranteed

When a live microphone feeds a speaker near it, you get feedback. CrowdMike is built around these facts:

- **Delay does not prevent feedback.** A delayed copy of the mic still closes the acoustic loop. Creative delay changes the sound, not the loop gain.
- **Feedback management** comes from gain discipline (input trims, output trims), high-pass and low-pass filters, parametric notches, the output limiters, route validation, and the risk indicator. The detector and risk score are advisory.
- **Route risk** is a score, not an acoustic guarantee. It combines the route, the gains, compressor makeup, delay feedback, and any persistent spectral peaks. See [[Feedback Assistant|Feedback-Assistant]].

## Polarity is not cancellation

Polarity inversion (the Ø button) is an exact 180 degree flip. Use it to:

- fix a microphone wired with reversed polarity,
- improve summing between two microphones that hear the same thing,
- create a deliberate comb-filter effect.

Do not use it as a way to silence a crowd. The sound of a crowd in a room changes with position, distance, frequency, and time, so cancellation does not hold across the room. The app labels the control "Polarity" for this reason.

## Real-time and safety basics

- Everything in the audio path runs in real time. Recording and file work happen on separate threads, so a slow disk does not stop the live sound. If the disk falls behind, the recorder marks a dropout and the live audio keeps going.
- Panic mutes the reinforcement outputs and leaves recording running. See [[Safety, Panic and Limiters|Safety-Panic-and-Limiters]].
- Plug-in latency is one constant value that the host is told about. The standalone shows an estimate of end-to-end latency, but it does not know the acoustic travel time from a speaker to a mic unless you enter it.
