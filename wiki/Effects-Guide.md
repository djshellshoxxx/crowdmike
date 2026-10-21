# Effects Guide

> CrowdMike has seven send-and-return effects that run side by side: digital delay, echo throw, tape delay, reverse chunk, noise enhancer, Stadium Reverb, and Crowd Multiplier.
> Each input and group has its own send level into each effect, and each effect's return level goes to the master and the output matrix.

## How the effects are wired

- Effects are **parallel**. Every slot takes its input from the send buses and sums into the master. No effect feeds another effect.
- A send takes a copy of the signal. The dry path keeps running, so the crowd stays intelligible.
- Each effect has a **Return** level (-80 to +6 dB) that is applied after the effect.
- The FX page can reorder the modules on screen. That changes the display only. The processing slots and their IDs never change.

## Slot table

| Slot | Effect | Parameter prefix | Notes |
|---|---|---|---|
| 1 | Digital delay | `fx.delay.` | Single, ping-pong, or multi-tap, with tempo sync |
| 2 | Echo throw | `fx.echo.` | Momentary throw with a capture buffer |
| 3 | Tape delay | `fx.tape.` | Wow, flutter, drive, and age on a delay |
| 4 | Reverse chunk | `fx.reverse.` | Plays captured audio backwards |
| 5 | Noise enhancer | `fx.noise.` | Shaped noise that follows the crowd |
| 6 | Stadium Reverb | `fx.reverb.` | Long, dense arena tail. Wet only. |
| 7 | Crowd Multiplier | `fx.multi.` | 2 to 16 micro-delay voices that thicken a small crowd |

Send levels from inputs and groups are `fxSend1` to `fxSend7`. Off is -80 dB.

## Common controls

Most effects share these:

- **Bypass**: click-free. Bypassed effects cost almost no CPU.
- **Return**: the output level of the effect.
- **Wet/dry** where the effect has a mix (not on Stadium Reverb).
- **Presets**: factory and user presets from the effect's preset menu.

## 1. Digital delay

| Control | Range | Default |
|---|---|---|
| Mode | Single, Ping-pong, Multi-tap | Single |
| Tempo sync | off or on | off |
| Division (when synced) | 1/32 to 1/1, including triplets and dotted values | 1/4 |
| Time | 1 to 2000 ms | 350 ms |
| Feedback | 0 to 0.95 | 0.35 |
| Wet/dry | 0 to 100 percent | 100 percent |
| Feedback high pass / low pass | 20 Hz to 2 kHz / 1 kHz to 20 kHz | 200 Hz / 6 kHz |
| Stereo offset | 0 to 50 ms | 0 ms |
| Diffusion | 0 to 1 | 0 |
| Taps (multi-tap) | 2 to 6 | 4 |

Feedback is kept below unity for every setting, so the delay does not run away. Delay repeats feed back into the room, so keep the feedback filters on.

## 2. Echo throw

A performance effect built on a delay. Hold or trigger it, and the send opens for the capture length, then repeats decay.

| Control | Range | Default |
|---|---|---|
| Time | 20 to 2000 ms | 450 ms |
| Capture length | 50 to 4000 ms | 1000 ms |
| Repeats | 1 to 16 | 4 |
| Decay | 0 to 0.95 | 0.5 |
| Send amount | 0 to 100 percent | 100 percent |
| High pass / low pass | 20 Hz to 2 kHz / 1 kHz to 20 kHz | 150 Hz / 7 kHz |
| Trigger | momentary | (E key, MIDI, OSC) |
| Tail kill | momentary | cuts the tail quickly |

Hold **E** for echo throw from the keyboard. With beat sync, the trigger can wait for the next grid line. See [[Beat Sync and Tempo|Beat-Sync-and-Tempo]].

## 3. Tape delay

A delay with a tape character: a modulated read, saturation, and a head tone.

| Control | Range | Default |
|---|---|---|
| Time | 20 to 2000 ms | 380 ms |
| Feedback | 0 to 0.92 | 0.4 |
| Drive | 0 to 1 | 0.3 |
| Age | 0 to 1 | 0.3 |
| Wow | 0 to 1 | 0.2 |
| Flutter | 0 to 1 | 0.2 |
| Low cut | 20 Hz to 1 kHz | 150 Hz |
| High cut | 1 kHz to 20 kHz | 5 kHz |
| Noise | 0 to 1 | 0.1 |
| Wet/dry | 0 to 100 percent | 100 percent |
| Head offset | 0 to 1 | 0.5 |

It is not a copy of any particular tape unit.

## 4. Reverse chunk

Keeps a rolling buffer of the send and plays a chunk backwards.

| Control | Range | Default |
|---|---|---|
| Mode | Triggered, Continuous, Repeat | Triggered |
| Chunk length | 0.25 to 8 s | 4 s |
| Trigger mode | Manual, Repeating, Transient, Beat-sync | Manual |
| Beat division (beat-sync) | 1 beat, 2 beats, 1 bar, 2 bars, 4 bars | 1 bar |
| Crossfade | 5 to 500 ms | 50 ms |
| Retrigger | Restart, Ignore, Queue | Restart |
| Repeat decay | 0 to 1 | 0.6 |
| Transient sensitivity | 0 to 1 | 0.6 |
| Stereo link | off or on | on |
| Source | Post-group sends, Pre-group inputs | Post-group sends |

- **Triggered**: freezes the most recent chunk and plays it backwards while the live sound continues.
- **Continuous**: alternating buffers give a continuous reversed sound with crossfades.
- **Repeat**: a triggered chunk loops backwards with decay.

Hold **V** for a reverse trigger from the keyboard. The FX page shows the buffer: write head, play head, stored region, and any pending trigger.

## 5. Noise enhancer

Adds controlled broadband excitement under the crowd. It is not raw static.

| Control | Range | Default |
|---|---|---|
| Amount | 0 to 1 | 0.1 |
| Colour | White, Pink, Brown, Bright | Pink |
| Tilt | -1 to +1 | 0 |
| High pass / low pass | 20 Hz to 2 kHz / 1 kHz to 20 kHz | 300 Hz / 12 kHz |
| Crowd follow | 0 to 1 | 0.7 |
| Transient follow | 0 to 1 | 0 |
| Attack / release | 1 to 500 ms / 10 to 2000 ms | 20 ms / 300 ms |
| Width | 0 to 1 | 0.8 |
| Seed | 1 to 9999 | 1 |
| Return | -80 to +6 dB | -12 dB |

The noise is shaped, followed by the crowd envelope, decorrelated across the stereo field, and capped at -24 dBFS peak. Turn up the crowd follow to keep the noise under the people, not over them.

## 6. Stadium Reverb

A large-room reverb that places the crowd in an arena. It is wet only, with no dry mix control. Use the send level to set how much you hear.

| Control | Range | Default |
|---|---|---|
| On | off or on | off |
| Size | 0 to 1 | 0.6 |
| Decay (RT60) | 0.2 to 20 s | 2.5 s |
| Pre-delay | 0 to 250 ms, or tempo-synced | 20 ms |
| HF damping | 1 to 20 kHz | 8 kHz |
| Low cut | 20 Hz to 1 kHz | 120 Hz |
| Diffusion | 0 to 1 | 0.7 |
| Mod depth / rate | 0 to 1 / 0.05 to 5 Hz | 0.3 / 0.6 Hz |
| Width | 0 (mono) to 1 | 1 |
| Wet level | -60 to +12 dB | -3 dB |
| Freeze | off or on | off |
| Tail kill | momentary | (K key, MIDI, OSC) |

- **Freeze** holds the tail at a constant level. The global Freeze also holds it.
- **Tail kill** drops the tail more than 60 dB within about 80 ms and clears its state.
- The effect stays stable for any setting. When it is off, its cost drops to clearing a buffer.

Factory presets: Arena Hall, Stadium Bowl, Small Club, Endless Wash.

## 7. Crowd Multiplier

Makes a few mics sound like many people. It uses 2 to 16 decorrelated micro-delay voices, with random slow modulation and slight detune. It does not sound like a pitched chorus.

| Control | Range | Default |
|---|---|---|
| On | off or on | off |
| Voices | 2 to 16 | 8 |
| Depth | 0 to 20 ms | 4 ms |
| Rate | 0.05 to 8 Hz | 0.8 Hz |
| Detune | 0 to 50 cents | 6 cents |
| Scatter | 0 to 1 | 0.7 |
| Tone | 0 to 1 | 0.5 |
| Seed | 1 to 9999 | 1 |
| Level | -60 to +12 dB | 0 dB |

- The same seed gives the same result every time. Change the seed to get a different crowd.
- Each voice is normalised, so adding voices does not blow up the level. The output stays near the level of the input for crowd-like material.
- The mono fold-down stays close to the channel level, so nothing cancels in mono.

Factory presets: Double The Crowd, Packed Terrace, Full Stadium.

## Global effect controls

| Control | Effect |
|---|---|
| Freeze / Hold | Holds the reverb tail and other freezable effects |
| Crowd Density To FX Sends | 0 to 1. Lets the crowd energy estimate push the FX sends up. Default 0 (off). |

## Tips

- Sends come from inputs and groups only, so send the group that carries the crowd, not a mix you have already built.
- Start each effect at its default, then move one control at a time.
- Use the Return level for the balance, and the send level to decide how much of each input goes in.
- If a performer's mic sits in a group, the ducker can leave that group alone (Exclude Key). See [[Performer Ducker|Performer-Ducker]].
- Tie the effects to the performance with [[Scenes, Presets and Macros|Scenes-Presets-and-Macros]] or the bottom strip, so you can recall a whole setup in one press.
