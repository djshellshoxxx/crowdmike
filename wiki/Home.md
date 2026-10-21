# CrowdMike Wiki

> CrowdMike captures a crowd with several microphones, processes and delays it, and routes it to PA, broadcast, recording, or monitor outputs.
> This wiki covers using it live (the **Users** pages) and working on the code (the **Developers** pages).

## What it is

CrowdMike is a live crowd reinforcement, transformation, recording, and playback processor for performers, DJs, venues, broadcast, and live production. You feed it several audience microphones and other audio inputs. It delays and processes them, mixes them into one or more crowd buses, and sends those buses to multiple outputs. The aim is to make audience ambience controllable, bigger, wider, more energetic, and recordable without building a DAW session.

It ships in three formats built from one shared engine:

| Format | What you get | Host I/O |
|---|---|---|
| Standalone | Full device management, recorder, playback deck, scenes, MIDI and OSC control | You pick the audio device and channels |
| VST3 | Audio effect plug-in with host automation and state saving | The host provides buses and channels |
| CLAP | Same engine and features as VST3 where the host exposes equivalent buses | The host provides buses and channels |

## Status: beta

CrowdMike is a **beta**. The packages are compiled and archived by CI, but they have not been launched by automated tests, and the DSP has not been run through a DAW or real audio interfaces yet. Read [[Getting Started|Getting-Started]] for the specifics before using it at a show.

- Builds are **unsigned**. Windows SmartScreen warns, and macOS Gatekeeper blocks the first launch.
- ASIO is not included. Windows uses WASAPI or DirectSound.
- Real multichannel interfaces, DAW hosts, and acoustic tests are still manual and outstanding.

Details are on [[Roadmap and Known Gaps|Roadmap-and-Known-Gaps]].

## Where to start

**If you are running a show**

1. [[Getting Started|Getting-Started]]: download, install, first launch.
2. [[Quick Start and Line Check|Quick-Start-Line-Check]]: set levels and a wiring template in a few minutes.
3. [[Concepts|Concepts]]: how the signal flows, so the controls make sense.
4. [[Safety, Panic and Limiters|Safety-Panic-and-Limiters]]: know your emergency controls before you need them.

**If you are building or changing the code**

1. [[Developer Guide|Developer-Guide]] and [[Building from Source|Building-from-Source]].
2. [[Architecture|Architecture]] and [[Parameter System|Parameter-System]].
3. [[Contributing|Contributing]] for the repo rules.

## Users

- [[Getting Started|Getting-Started]]
- [[Quick Start and Line Check|Quick-Start-Line-Check]]
- [[Concepts|Concepts]]
- [[Interface Tour|Interface-Tour]]
- [[Input Channel Strip|Input-Channel-Strip]]
- [[Groups and Master|Groups-and-Master]]
- [[Effects Guide|Effects-Guide]]
- [[Performer Ducker|Performer-Ducker]]
- [[Beat Sync and Tempo|Beat-Sync-and-Tempo]]
- [[Routing and Wiring Templates|Routing-and-Wiring-Templates]]
- [[Feedback Assistant|Feedback-Assistant]]
- [[Recording and Playback|Recording-and-Playback]]
- [[Show Report|Show-Report]]
- [[Scenes, Presets and Macros|Scenes-Presets-and-Macros]]
- [[MIDI and OSC|MIDI-and-OSC]]
- [[Plugin Hosts|Plugin-Hosts]]
- [[Troubleshooting and FAQ|Troubleshooting-and-FAQ]]
- [[Safety, Panic and Limiters|Safety-Panic-and-Limiters]]

## Developers

- [[Developer Guide|Developer-Guide]]
- [[Building from Source|Building-from-Source]]
- [[Architecture|Architecture]]
- [[Parameter System|Parameter-System]]
- [[Adding a DSP Effect|Adding-a-DSP-Effect]]
- [[State, Scenes and Migration|State-Scenes-and-Migration]]
- [[Testing|Testing]]
- [[CI and Release Process|CI-and-Release-Process]]
- [[Contributing|Contributing]]
- [[Spec Index|Spec-Index]]
- [[Roadmap and Known Gaps|Roadmap-and-Known-Gaps]]

## Source of truth

These pages summarise the repository. When they disagree with the specifications, the specifications win, and the specifications are in [[Spec Index|Spec-Index]]. The repository is at [github.com/djshellshoxxx/crowdmike](https://github.com/djshellshoxxx/crowdmike).
