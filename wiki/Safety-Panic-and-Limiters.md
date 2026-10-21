# Safety, Panic and Limiters

> Panic (F12) mutes the crowd outputs in about 5 ms and keeps recording running. Limiters on every output stop applause peaks from hurting people or speakers.
> These are safety nets, not guarantees, so keep your PA's own protection in place and set levels before you open the room.

## Panic

Panic is the emergency mute for the crowd outputs.

| How to trigger | Notes |
|---|---|
| F12 | Toggles panic on and off |
| Panic MIDI binding | Any controller button |
| OSC `/crowdmike/panic` | With `1` to engage, `0` to release. A bare message engages it. |

What panic does:

- Ramps the reinforcement outputs to silence in about 5 ms. The ramp is short enough to stop a developing feedback event, without a hard click.
- Stops clip and bed playback to the reinforcement outputs.
- Keeps recording running. The recorder taps are taken before the panic gain, so the record is unaffected.
- Keeps the meters live, so you can see the room while muted.
- Keeps the ducker and other processing running in the background, so the state is right the moment you release panic.

Panic is checked every audio block. The processor tests hold the response to under 11 ms.

Panic events are logged in the [[Show Report|Show-Report]] and counted in the summary.

Use panic first, then work out the cause. It is the fastest way to stop a squeal.

## Bypass

Global bypass sends the dry input to the output with the processing removed. It crossfades in about 10 ms, and the latency stays aligned, so the host's timing does not change. The performer's dry path is never ducked while bypassed.

## Rehearsal mode

Rehearsal mode keeps every output silent, but the engine, recorder, and meters keep working. Use it to set up, to check routing and levels with meters, or to record without sending sound to the room. The "Crowd capture only" routing template turns rehearsal mode on.

## Limiters

CrowdMike has several limiters, and each has a job.

| Limiter | Where | Default | What it does |
|---|---|---|---|
| Output limiter | Every output channel | On, ceiling -0.5 dBFS, release 100 ms | Lookahead limiter that protects the output. It is the last stage of the output chain. |
| Master limiter | Master crowd bus | On, ceiling -1 dB | Protects the master mix before it reaches the outputs |
| Group limiter | Each crowd group | Off, ceiling -1 dB | Optional per-group limiting |

The output limiters use lookahead, so they react before a peak arrives. That lookahead is the one constant latency the plug-in reports to the host.

### Protected outputs

The final output limiter cannot be turned off by accident. To remove it on a protected output, use the global **Unlock output limiters** setting first. Keep it locked unless you have a reason to turn a limiter off.

### Hard clamp

The output stage ends with a hard clamp. It is a last-resort guard against values the limiters could not catch, not a sound-shaping tool. It should rarely engage. If you find it working, reduce the level feeding the outputs.

## Other safety features

| Feature | What it protects against |
|---|---|
| Noise enhancer cap | The noise enhancer is capped at -24 dBFS peak, so it cannot blast the room |
| Stadium Reverb freeze | Holds a tail at constant level, without growing |
| Tail kill | Drops a tail quickly when it gets out of hand. Hold K for the reverb, which drops more than 60 dB in about 80 ms. The echo has its own momentary tail kill parameter. |
| Route validation | Blocks routes that would form a loop |
| Route ramps | Changes to routes glide, so no click |
| Scene recall ramp | Scene changes glide. Only panic cuts a route hard. |
| Ducker | Only reduces gain. It can never unmute a panicked output. |
| Virtual soundcheck lock | Stops an accidental blast from recorded tracks. See [[Recording and Playback|Recording-and-Playback]]. |
| Feedback-risk indicator | Advisory warning on each route that carries a live mic to a speaker |
| Safety Margin macro | Macro 8 by default. Map it to the controls you want to pull back quickly, such as output trims. |

Clip hotkeys cannot use function keys or Ctrl shortcuts, so a clip hotkey can never take over panic or the page keys.

## Emergency checklist

1. Press F12 (or the panic controller). Check that the outputs are silent.
2. Watch the Live page meters and the feedback-risk indicators.
3. Find the source: a mic too hot, a speaker too close, a tail that will not stop.
4. Fix the cause: lower the trim, move the mic, apply a notch, or kill the tail.
5. Release panic when the room is quiet, and bring the level back up gradually.
6. Note the time. Panic events appear in the Show Report and any markers you added.

## Before a show

- Set levels with Line Check, then check the feedback-risk indicators.
- Know where F12, Ctrl+R, and Ctrl+M are, and test them.
- Keep the output limiters on and locked.
- Keep the PA's own limiter in use.
- Do a rehearsal with panic pressed once, so you know how the room sounds when it stops.

## Limits

These features reduce risk. They are not a guarantee against feedback or damage. The beta has not been tested in a real room with real loudspeakers yet, so keep your own limits in place.
