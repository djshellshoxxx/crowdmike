# Routing and Wiring Templates

> The Matrix page decides where each source goes. Rows are sources (inputs, groups, FX returns, playback, master), and columns are outputs, with optional group and recorder destinations.
> Nine wiring templates set the matrix, groups, and sends for common venue layouts in one undo step.

## Routing model

Sources:

- input channels (raw and processed),
- crowd groups (1 to 8),
- FX returns (7 slots),
- playback buses,
- the master crowd bus.

Destinations:

- crowd groups,
- FX sends,
- recorder taps,
- master buses,
- physical or host outputs.

The routing graph cannot form a feedback loop by accident. The validator rejects cycles, and every effect is a send-and-return that ends at the master and the matrix, so no effect can feed another one.

## Output matrix

The output matrix maps each bus row to an output channel. The rows are:

| Rows | Source |
|---|---|
| Groups 1 to 8, left and right | Crowd groups |
| FX 1 to 7 returns, left and right | Effect returns |
| Playback, left and right | Clip and bed playback |
| Master, left and right | Master crowd bus |

Routes are ramped, so changing a route does not click. Scene recall ramps routes too, and never switches a loud route hard unless panic is on.

Each cell can be off, on at unity, or set to a level. Some cells can be pre or post, where that makes sense. Keyboard use: arrow keys move, Space toggles the cell, + and - nudge it, 0 clears it, U sets unity, and P toggles pre/post.

## Templates

Each template rewrites the matrix, the group setup, and a few sends. Apply one from the Routing page or from Quick Start.

| # | Template | What it sets up |
|---|---|---|
| 0 | Direct pass-through | Processed inputs go straight to the outputs. No groups or effects. This was the default in the first release. |
| 1 | 1 mic > stereo PA | One mic, centre-panned into the Front Crowd group, master to outputs 1 and 2 |
| 2 | 2 mic stereo crowd > stereo PA | Inputs 1 and 2 panned hard left and right into the Front Crowd group, master to outputs 1 and 2 |
| 3 | 4 mic room > stereo crowd bus | Four mics spread left to right across the Front Crowd group, master to outputs 1 and 2 |
| 4 | Front/rear crowd > PA + broadcast | Inputs 1 and 2 at the front, 3 and 4 at the rear. The master feeds PA outputs 1 and 2, and a Broadcast group (all inputs, lower level) feeds outputs 3 and 4. |
| 5 | Crowd capture only | All inputs grouped and recorded, no reinforcement outputs, rehearsal mode on |
| 6 | Virtual soundcheck | The stereo crowd template with Virtual Soundcheck switched on. Load a session on the Playback page first. |
| 7 | DJ performance FX | Stereo crowd into the PA, with delay, echo, and reverse sends open on the Front Crowd group |
| 8 | Venue multi-zone | Inputs in pairs feed four groups, and each group goes to its own output pair |

Templates change routing and sends. They do not change trims. Use Line Check for levels. Quick Start also turns on a 100 Hz high-pass on each active input.

A template applies its parameter changes (pans, sends, rehearsal, and the Quick Start high-pass) as one undo step. The matrix routing itself is not part of the undo history.

## Three kinds of delay in routing

- **Alignment delay** lines up mics and groups (per input).
- **Reinforcement delay** is a deliberate lag between the crowd you capture and the crowd you send out (per group and per output). It can reduce how strongly the PA's sound comes back into the mics, but it is not a guarantee against feedback.
- **Creative delay** is the echo, tape, and digital delays as effects.

Keep them separate in your head when you set up a room.

## Feedback-aware routing

When a live mic routes to a speaker close to it, CrowdMike computes a risk indicator from the route, the input trim, bus and output gain, compressor makeup, delay feedback, and persistent spectral peaks. It is advisory only. It is not an acoustic guarantee, and it does not know where your speakers are unless you tell it.

## Channel naming

Each input, group, and output has a stable internal ID and a name you choose. The IDs stay the same when you rename things, so scenes keep working. You can also add a physical label and a zone.

## Polarity and phase in routing

Polarity is an exact 180 degree inversion per input, group, or output. It fixes reversed wiring and can make a deliberate comb-filter effect. The phase all-pass on inputs is a separate, continuous tool. See [[Concepts|Concepts]].

## Tips

- Start from a template, then change one thing at a time.
- Check the matrix for any cell that routes a live mic to a speaker near it before you raise the output.
- Name outputs for their destination (PA left, PA right, Broadcast), so the matrix reads at a glance.
