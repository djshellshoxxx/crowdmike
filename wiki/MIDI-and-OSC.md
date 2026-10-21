# MIDI and OSC

> In the standalone app, MIDI learn binds controller messages to parameters, scenes, recorder, clips, macros, panic, and tempo. OSC lets a tablet, a show controller, or another computer drive the same things over the network.
> Plug-ins use the host's automation instead of MIDI learn, and OSC is receive-only.

## Which tool for what

| Need | Use |
|---|---|
| Physical knobs, pads, and foot switches in the standalone | MIDI learn |
| Automation in a DAW | Host automation (plug-in or standalone) |
| A tablet, phone, or another computer controlling the show | OSC |
| Plug-in in a DAW that should respond to a controller | Route the controller through the host and automate CrowdMike's parameters |

## MIDI learn

Open the right-click menu on any control and choose **MIDI learn**, then move the controller. Or use the MIDI settings page to set the input device.

Each binding has:

- a **message type**: control change (CC), note on and off, or pitch bend,
- a **channel**: omni, or 1 to 16,
- a **number**: the CC or note number (not used for pitch bend),
- a **target**, from the list below,
- a **mode**: Absolute (follows the knob), Toggle (each press flips a bool), Momentary (held while pressed), or Trigger (one action per press).

### Binding targets

| Target | Acts on |
|---|---|
| Param | Any parameter, including tempo parameters |
| Scene select | A scene slot |
| Scene next, Scene previous | The scene list |
| Record start, Record stop, Record toggle | The recorder |
| Record marker | Adds a marker of a given type |
| Split file | Starts a new file |
| Clip trigger, Clip stop | A clip slot |
| Stop all clips | Stops every clip |
| Panic | Panic mute |
| Macro value | One of the eight macros |
| Tempo tap | Taps the tempo |
| Tempo resync | Resyncs the downbeat |
| Scene scope toggle | Turns one recall-scope bit on or off, or all of them (index 0 to 7 for each bit, 8 for all) |

MIDI realtime clock (start, continue, stop, and clock ticks) goes to the tempo engine when the tempo source is MIDI Clock. Those messages are not passed to bindings. See [[Beat Sync and Tempo|Beat-Sync-and-Tempo]].

## OSC

### Setting it up

On the Settings page, in the **OSC** section:

| Setting | Default | Notes |
|---|---|---|
| Enable | off | OSC is off until you turn it on |
| Port | 9000 | Between 1024 and 65535 |
| Localhost only | on | Accepts messages only from this computer |
| Bind address | empty | Empty means all interfaces (or localhost when "Localhost only" is on). Type an address to bind one interface. |

The status line shows the address and port it is listening on. The section also has an address cheat sheet.

Keep **Localhost only** on at a venue unless you need another machine to control the show, and then bind to the address of the control network.

### Address table

Replace `<n>` with the number shown.

| Address | Argument | Does |
|---|---|---|
| `/crowdmike/master/level` | dB | Master level |
| `/crowdmike/master/<alias>` | value | Other master parameters (see aliases) |
| `/crowdmike/input/<1..16>/<alias>` | value | Input parameter |
| `/crowdmike/group/<1..8>/<alias>` | value | Group parameter |
| `/crowdmike/output/<1..16>/<alias>` | value | Output parameter |
| `/crowdmike/fx/<name>/<suffix>` | value | Effect parameter, for example `/crowdmike/fx/delay/feedback` |
| `/crowdmike/fx/reverse/trigger` | (pulse) | Reverse trigger |
| `/crowdmike/global/<name>` | value | Global parameter, for example `/crowdmike/global/duckDepthDb` |
| `/crowdmike/param/<full.id> <value>` | actual value | Any parameter by its full ID |
| `/crowdmike/paramnorm/<full.id> <0..1>` | normalised | Any parameter, 0 to 1 across its range |
| `/crowdmike/scene/<1..32>/select` | (none) | Recall a scene |
| `/crowdmike/scene/next`, `/crowdmike/scene/prev` | (none) | Next or previous scene |
| `/crowdmike/scene/scope <1..255>` | mask | Set the recall scope |
| `/crowdmike/scene/scope/<name> [0\|1]` | toggle or set | One scope bit: `inputs`, `groups`, `fx`, `master`, `outputs`, `routing`, `names`, `macros`, or `all` |
| `/crowdmike/record/start`, `stop`, `toggle` | (none) | Recorder |
| `/crowdmike/record/marker [type]` | type 0 to 5 | Marker. Types: 0 Big Cheer, 1 Chant, 2 Applause, 3 Performer Moment, 4 Problem/Feedback, 5 Custom (default). |
| `/crowdmike/record/split` | (none) | Split the file |
| `/crowdmike/clip/<1..32>/trigger` | (none) | Trigger a clip |
| `/crowdmike/clip/<1..32>/stop` | (none) | Stop a clip |
| `/crowdmike/clips/stop` | (none) | Stop all clips |
| `/crowdmike/macro/<1..8> <0..1>` | value | Set a macro |
| `/crowdmike/panic [0\|1]` | 1 default | Panic on or off |
| `/crowdmike/tempo/tap` | (none) | Tap tempo |
| `/crowdmike/tempo/resync` (or `downbeat`) | (none) | Resync downbeat |
| `/crowdmike/tempo/bpm <20..300>` | BPM | Manual BPM |
| `/crowdmike/tempo/source <0..4>` | index | Tempo source |
| `/crowdmike/tempo/quantise <0..3>` | index | Trigger quantise |
| `/crowdmike/tempo/nudge <-10..10>` | BPM | Tempo nudge |
| `/crowdmike/tempo/autoSource <0..16>` | index | Auto tempo input (0 is master) |
| `/crowdmike/tempo/barBeats <0..5>` | index | Beats per bar (4, 3, 2, 5, 6, 7) |

Momentary parameters (`.trigger` and `.tailKill`) act on a value of 1 and ignore a release of 0. A bare message with no value pulses them. A bare message on a switch sets it to 1.

### Aliases

Short names for the common parameter suffixes:

| Alias | Full suffix | Used for |
|---|---|---|
| `trim` | `trimDb` | Input, group, and output trim |
| `level` | `levelDb` | Master level (`/crowdmike/master/level`) |
| `pol` | `polarity` | Polarity |
| `delay` | `delayMs` | Group or output delay. An input uses `align` for its alignment delay. |
| `align` | `alignDelayMs` | Input alignment delay |
| `hpf` | `highPassHz` (input), `hpHz` (group and output) | High-pass frequency |
| `lpf` | `lowPassHz` (input), `lpHz` (group and output) | Low-pass frequency |
| `ceiling` | `limiterCeilingDb` | Output limiter ceiling |

Any other name is used as written, so `/crowdmike/input/3/mute` works without an alias.

### Notes

- OSC is receive-only. The app does not send OSC back.
- An unknown or malformed address is rejected, and nothing else changes.
- Parameter IDs are fixed once released, so these addresses stay valid across updates.

## Plug-in MIDI

Plug-ins do not ask the host for MIDI. Use the host to automate parameters, or map a controller in the host to the parameter you want.

## Related

[[Scenes, Presets and Macros|Scenes-Presets-and-Macros]] for what the scene and macro addresses act on. [[Plugin Hosts|Plugin-Hosts]] for automation.
