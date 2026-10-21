# Groups and Master

> Groups are submixes of inputs, each with its own level, balance, delay, filters, EQ, compressor, width, and sends. The master bus sums the groups and FX returns, then applies its own EQ, compressor, width, and limiter before the output stage.
> Outputs take their mix from the matrix and add a trim, mute, polarity, delay, filters, and a limiter per channel.

## Groups (1 to 8)

Each group is a crowd zone such as Front Crowd, Rear Crowd, Balcony, or a Broadcast mix. Name them in a way that fits the venue.

Inputs join groups on the Inputs page (group assignments) or on the Matrix page. One input can feed more than one group.

| Control | Range | Default |
|---|---|---|
| Trim | -60 to +24 dB | 0 dB |
| Mute, Solo | off or on | off |
| Polarity invert | off or on | off |
| Balance | -1 (left) to +1 (right) | centre |
| Reinforcement delay | 0 to 500 ms | 0 ms |
| High pass | 20 Hz to 500 Hz | off, 80 Hz |
| Low pass | 2 kHz to 20 kHz | off, 18 kHz |
| EQ bands 1 to 4 | same layout as the input EQ | low shelf 120 Hz, bell 400 Hz, bell 2.5 kHz, high shelf 8 kHz |
| Compressor | same layout as the input compressor | off, threshold -18 dB, ratio 2.5:1 |
| Limiter | ceiling -12 to 0 dB, release 5 to 1000 ms, input gain 0 to 24 dB | off, ceiling -1 dB, release 100 ms |
| Width | 0 (mono) to 2 (wide) | 1 |
| FX sends 1 to 7 | -80 (off) to +6 dB | -80 dB |

The default group chain is EQ, then compressor, then width, then the sends. Group limiters are off by default.

### Feedback and groups

A group can feed a speaker near a microphone. Check the group's output and its route in the Matrix before you raise it. Feedback risk is shown on the Live page.

## Master crowd bus

The master bus is the sum of the crowd groups, the playback bus when routed to master, and the FX returns (after any performer ducking). Its chain is:

EQ (8 bands), then compressor, then width, then limiter.

| Control | Range | Default |
|---|---|---|
| Master level | -80 to +12 dB | 0 dB |
| Master mute | off or on | off |
| Master EQ | 8 bands, on by default | 60 Hz low shelf, 150 Hz, 400 Hz, 1 kHz, 2.5 kHz, 5 kHz, 10 kHz, 16 kHz high shelf |
| Compressor | same layout as the input compressor | off |
| Limiter | ceiling -12 to 0 dB, release 5 to 1000 ms | on, ceiling -1 dB |
| Width | on or off, 0 to 2 | off, 1 |
| Decorrelation | 0 to 1 | 0 |
| Haas offset | 0 to 20 ms | 0 ms |
| Mono compatibility | 0 to 1 | 0 |
| Low-frequency mono cutoff | 20 to 500 Hz | 120 Hz |

The Haas offset is capped at 20 ms. Low frequencies below the mono cutoff stay mono, which keeps bass centred. Check the mono compatibility setting before you rely on width for a broadcast feed.

## Output channels (1 to 16)

Each output channel is a physical or host output. Its controls come after the matrix:

| Control | Range | Default |
|---|---|---|
| Trim | -60 to +12 dB | 0 dB |
| Mute | off or on | off |
| Polarity invert | off or on | off |
| Delay | 0 to 500 ms | 0 ms |
| High pass | 20 to 500 Hz | off, 30 Hz |
| Low pass | 2 kHz to 20 kHz | off, 20 kHz |
| Limiter | off or on | on |

Use the output delay to line up a loudspeaker zone that sits further from the stage. Use the output trim for a last gain adjustment per zone.

### Global output settings

| Setting | Range | Default | Notes |
|---|---|---|---|
| Output limiter ceiling | -12 to 0 dB | -0.5 dB | Lookahead limiter on every output |
| Output limiter release | 5 to 1000 ms | 100 ms | |
| Unlock output limiters | off or on | off | The output limiters stay on for protected outputs unless you unlock them |
| Playback level | -80 to +12 dB | 0 dB | Level of clip playback into the routing |

Every output passes through the same lookahead limiter, so the plug-in reports one constant latency to the host. See [[Safety, Panic and Limiters|Safety-Panic-and-Limiters]].

## Typical layouts

- **One group, one stereo PA**: group 1 to master to outputs 1 and 2.
- **Front and rear crowd**: two groups, master to PA on outputs 1 and 2, with a broadcast group on outputs 3 and 4. The [[Routing and Wiring Templates|Routing-and-Wiring-Templates]] page has the presets that set this up for you.
- **Multi-zone venue**: each group gets its own output pair, delayed to suit its zone.
