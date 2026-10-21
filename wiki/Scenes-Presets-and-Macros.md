# Scenes, Presets and Macros

> Scenes are live snapshots of a show you recall in one step, with a ramp so the change is safe. Presets are reusable settings for one part of the chain. Macros are eight performance knobs that drive many parameters at once.
> Each one has its own job, so use scenes for the show, presets for building blocks, and macros for the moment.

## Scenes

The Scenes page holds 32 scene slots. Each scene stores the show's state for the parts you choose.

A scene can hold:

- input names, enables, gains, delays, polarity, filters, and dynamics,
- group membership and group settings,
- effect parameters and sends,
- output routing and trims,
- recorder routing,
- playback settings,
- macro assignments,
- MIDI and OSC assignments (as the product specification describes).

Some controls are never captured, so a scene does not reset your live state. These are the transient controls: tempo tap, tempo resync, reverb freeze, and reverb tail kill.

### Recalling a scene

| Action | How |
|---|---|
| Recall by slot | Click the slot on the Scenes page, or OSC `/crowdmike/scene/<n>/select`, or a MIDI binding |
| Next or previous | Ctrl+Right and Ctrl+Left, OSC `/crowdmike/scene/next` and `prev`, or the **Next Scene** and **Previous Scene** global parameters |
| Hotkeys | Ctrl+1 to Ctrl+9 for scenes 1 to 9. With Shift, Ctrl+Shift+1 to Ctrl+Shift+9 cover scenes 10 to 18. Slots 19 to 32 have no hotkey. |

Recall uses a short transition ramp by default, so gains and routes glide rather than jump. Scene changes never hard-switch a loud route. Only panic cuts a route hard.

### Recall scope

The recall scope decides which parts of the show a recall changes. It has eight bits:

| Bit | Covers |
|---|---|
| Inputs | Input channel settings |
| Groups | Group settings |
| FX | Effect settings and sends |
| Master | Master chain |
| Outputs | Output channel settings |
| Routing | Output matrix |
| Names | Channel names |
| Macros | Macro assignments |

You can set the scope on the Scenes page. It can also change remotely: OSC `/crowdmike/scene/scope <1..255>`, or `/crowdmike/scene/scope/<name> [0|1]` to toggle one part (use `all` for every part), or a MIDI binding that toggles a bit. A scope with no parts is refused. Every recall path uses the scope, including OSC and MIDI.

### Locks

A locked parameter keeps its current value through every recall. Right-click a control and choose lock against scene recall, or lock it from the Scenes page. Use locks for things that must not change during the show, such as a safety limit or a feedback notch you have just set.

### Other scene controls

- **Duplicate**, **clear**, **next**, and **previous** on the Scenes page.
- Each scene can have a name and notes.
- The Scenes page lists the MIDI, OSC, and hotkey for each slot.

## Presets

Presets save and load settings for one part of the chain, so you can reuse a good compressor or a room reverb in another show.

| Scope | Saves |
|---|---|
| Entire show | Everything |
| Input channel | One input's settings |
| Group | One group's settings |
| Effect | One effect's settings |
| Master chain | The master bus settings |
| Routing template | A routing setup |

Presets carry a version number, and the app migrates older ones when it loads them.

The factory presets include:

- Stadium Reverb: Arena Hall, Stadium Bowl, Small Club, Endless Wash
- Crowd Multiplier: Double The Crowd, Packed Terrace, Full Stadium

Use the preset browser to load one or to save your own.

## Macros

Macros are eight performance knobs. Their default names are:

1. Crowd Size
2. Excitement
3. Space
4. Echo
5. Reverse
6. Broadcast Density
7. Dark/Bright
8. Safety Margin

Each macro can drive several destinations at once. For each destination you choose:

- a minimum and maximum value,
- a curve,
- whether to invert,
- whether the mapping is bipolar (centre at zero).

Macros can sit on the bottom performance strip, so they are always close at hand. They can also be bound to MIDI (the MacroValue target), or driven from OSC with `/crowdmike/macro/<1..8> <0..1>`. Right-click a macro control to assign destinations.

Some controls, such as the ducker depth, can be driven by a macro. A second control, such as the ducker's range clamp, can then limit how far the macro goes. See [[Performer Ducker|Performer-Ducker]].

## Suggested use

- Build the show around scenes: one for soundcheck, one for the opener, one for the headliner, one for the encore.
- Set each scene's scope to what changes between them. A "routing only" scene leaves the effect settings alone.
- Use macros during a set for the moves you make often, such as Space or Echo, and keep Safety Margin in reach.
- Lock the parameters that must not move, then recall as often as you need to.

## Related

[[MIDI and OSC|MIDI-and-OSC]] for binding scenes and macros to a controller. [[State, Scenes and Migration|State-Scenes-and-Migration]] for the developer view of how scenes are stored.
