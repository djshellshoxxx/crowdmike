# Performer Ducker

> The ducker listens to the performer's mic and pulls the crowd groups and FX returns down while they talk or sing, so the voice stays clear and feedback risk drops.
> It never changes what gets recorded from the raw or processed inputs, and it cannot unmute a panicked output.

## What it does

When the key input is active (the performer is speaking), the ducker lowers the buses you chose by a set amount. When the key goes quiet, the buses come back up smoothly.

It is voice-activated, deterministic, and only ever reduces gain. It is not a general side-chain compressor, so there is no ratio or knee. It does not process the key input, and it does not duck the direct input-to-output matrix, so a performer routed straight to the PA is never ducked.

## Setup

1. On the Live page, switch the ducker strip **On**.
2. Choose the **Key** input, normally the performer's mic or DI. On the Inputs page, the top bar also has a **Duck key** choice, and the key row is marked **KEY**.
3. Set the **Target**:
   - Crowd groups: only the groups are ducked.
   - FX returns: only the effect returns are ducked.
   - Both (default): groups and effect returns.
4. Set **Thr** so the performer's normal speech opens the ducker, but room noise does not.
5. Set **Depth** for how much to pull down. Start at about -6 dB.
6. Set **Atk**, **Hold**, and **Rel** for how fast it moves and how long it waits.
7. Watch the key-level bar against the threshold marker, and the gain-reduction bar, while the performer speaks.

## Controls

| Control | Range | Default | What it does |
|---|---|---|---|
| On | off or on | off | Turns the ducker on |
| Key | None, Input 1 to Input 16 | None | Which mic listens |
| Exclude key | off or on | on | Leaves the key's own group alone |
| Threshold | -80 to 0 dB | -35 dB | Level of the filtered key that opens the ducker |
| Depth | -30 to 0 dB | -6 dB | Reduction while the key is active |
| Attack | 1 to 200 ms | 10 ms | Time to travel the whole depth going down |
| Hold | 0 to 2000 ms | 250 ms | How long it stays down after the key drops |
| Release | 10 to 5000 ms | 400 ms | Time to travel back up the whole depth |
| Target | Crowd groups, FX returns, Both | Both | What gets ducked |
| Range clamp | -30 to 0 dB | -30 dB | Limits the reduction. A second control for macros. |
| Key HPF | 20 Hz to 2 kHz | 120 Hz | Filters the detector only |
| Key LPF | 1 kHz to 20 kHz | 6 kHz | Filters the detector only |
| Lookahead | 0 to 10 ms | 0 ms | Delays the crowd buses so the duck starts before the voice |

Depth 0 dB is legal. The ducker then does nothing audible, but the meters still show detection.

## Exclude key

Most of the time the performer's mic sits in its own group. With **Exclude key** on, the group that carries the key input is never ducked, so the performer is not ducked through their own group.

## What is and is not ducked

| Path | Ducked? |
|---|---|
| Crowd groups | Yes, when Target includes groups (except the key's own group with Exclude key) |
| FX returns | Yes, when Target includes FX returns |
| FX sends | No. The sends are taken before the duck. |
| Direct input-to-output matrix | No |
| Playback bus, playback routed to master | No |
| Raw and processed input recorder taps | No |
| Group and FX recorder taps | No. These record the signal before the duck. |
| Master and output recorder taps | Yes. They record what the PA gets. |

## Lookahead

Lookahead delays all group and FX-return copies by the same amount, targeted or not. This keeps the crowd buses in step with each other. The gain then falls before the voice reaches the ducked audio. Use it only for crowd and effect buses that mix acoustically with the performer. The delay is not added to the reported plug-in latency, so the direct path is not delayed.

## Panic, bypass, rehearsal

- **Panic** still mutes the outputs. The ducker keeps running in the background, so its meters are honest and its state is right the moment you release panic. It can only reduce gain, so it can never unmute a panicked output.
- **Bypass** turns the ducker off. It releases over its release time, and the dry path is never ducked.
- **Rehearsal** runs the ducker as normal, with outputs silent and recording unaffected.
- **Virtual soundcheck** feeds the key from the playback, so you can test the ducker with a recorded performer.

## Tips

- Set the threshold from the key-level bar. Speak at your normal level and aim the threshold a few dB below it.
- Use a short attack for fast speech and a longer release for singing, so the crowd does not pump between phrases.
- Keep the Range clamp at -30 dB unless a macro needs a limit.
- Ducking does not stop feedback on its own. It lowers the crowd while you talk, which helps, but the [[Feedback Assistant|Feedback-Assistant]] and gain staging still do the real work.

## Related

[[Groups and Master|Groups-and-Master]] for the buses the ducker acts on. [[Scenes, Presets and Macros|Scenes-Presets-and-Macros]] for saving the ducker setup with a scene.
