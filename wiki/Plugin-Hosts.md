# Plugin Hosts

> The VST3 and CLAP plug-ins run the same engine as the standalone app. The host provides the audio channels and transport, and you automate parameters from the host.
> Neither format has been run in a DAW yet, so treat host behaviour as untested until you check it yourself.

## Formats

| Format | Identity | Notes |
|---|---|---|
| VST3 | Category Fx, Spatial | One main bus with channel layouts up to 16 channels. No auxiliary buses. |
| CLAP | Audio effect | Same engine. Bus layouts depend on what the host exposes. |
| AU | Not built | Future work (macOS host tests needed) |
| VST2 | Not built | The project does not ship a VST2 build |

The plug-in identity (manufacturer, plug-in code, and CLAP ID) is fixed and does not change with the version, so a host sees the same plug-in across releases.

## What the host controls

- **Audio I/O.** The host gives CrowdMike its input and output channels. There is no device page in the plug-in, and no hardware ownership.
- **Transport.** Host tempo and position feed the tempo engine when the Tempo Source is Host. Auto-record on transport follows the host transport.
- **Automation.** Parameters are exposed with stable IDs, about 3000 in total. Host gestures are supported, and values are normalised correctly.
- **State.** The host saves CrowdMike's state with the project. The full state (including routing and the show structure that is not a parameter) is in the plug-in state chunk.

Large routing matrices are not automated one cell at a time. They live in structured state, so use scenes or the Matrix page for routing changes.

## Latency

The output stage uses a lookahead limiter on every output. The plug-in reports one constant latency to the host, so the host can compensate. The standalone app shows an estimated end-to-end latency, but it cannot know the speaker-to-mic acoustic delay unless you enter it.

Panic and bypass keep the latency aligned, so switching them does not change the host's timing.

## Recording and playback in plug-ins

The recorder and clip playback are available in the plug-in. Recording respects the host transport and permissions. Use auto-record on transport if you want takes to follow the DAW's record and stop.

Virtual soundcheck plays recorded tracks at 1x speed.

## MIDI

Plug-ins do not request MIDI from the host. Map your controllers to parameters in the host, or use the standalone MIDI learn in a separate standalone session.

## Standalone-only features

These are not in the plug-in:

- Device selection, channel assignment, and device reconnect
- MIDI learn bindings
- Crash recovery and autosave
- The standalone OSC receiver (the plug-in does not run one)

The [[Roadmap and Known Gaps|Roadmap-and-Known-Gaps]] page has the current status of each.

## Checking a host

CrowdMike has not been validated in a DAW yet. Before you rely on a host, check:

- The plug-in loads and the editor opens.
- Each layout you use passes audio, with the correct channel count.
- Parameters appear, automate, and save with the project.
- Reopening the project restores the state.
- The host's latency compensation matches the reported value.

The project's release workflow can run pluginval (strictness 5) on the Linux VST3 and clap-validator on the CLAP as an optional step. The results are informational. macOS and Windows plug-ins are not checked this way. See [[CI and Release Process|CI-and-Release-Process]].

## Hosts in the test plan

The test plan lists FL Studio, REAPER, Ableton Live, Bitwig Studio, and Studio One (Windows), REAPER, Ableton Live, and Bitwig Studio (macOS), and REAPER and Bitwig Studio (Linux). None have been tested with CrowdMike yet. If you try one, start with a simple layout, then report what you find.
