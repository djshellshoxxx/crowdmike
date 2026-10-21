# Roadmap and Known Gaps

> All eleven implementation phases are built. What is left is hardware and DAW validation, ASIO, a few playback and recorder items, and some future work.
> The list below comes from the 2026-10-14 feature audit, with notes where later specs say something different.

## Status at a glance

| Area | Status |
|---|---|
| Core DSP, routing, groups, master, output stage | Built and unit-tested |
| FX (seven slots), including Stadium Reverb and Crowd Multiplier | Built and unit-tested |
| Recorder, playback, virtual soundcheck, crowd bed | Built and unit-tested. Live device and disk behaviour not verified on real hardware. |
| Scenes, presets, macros, MIDI learn, OSC | Built. MIDI learn is standalone only. OSC is receive-only. |
| Feedback assistant | Built, advisory. Tuned on synthetic signals. |
| Show Report | Built and tested |
| Standalone device management and reconnect | Built, needs real hardware to confirm |
| Plug-ins (VST3, CLAP) | Built. Not yet tested in a DAW. |
| GUI, ten pages, shortcuts, high-contrast, compact | Built, snapshot-tested |
| Windows, macOS, and Linux packages | Built by CI. Not launched by CI. |

## Known gaps (from the audit)

### P1: needed before a real show

| Item | What it needs |
|---|---|
| DAW and validator runs (pluginval at level 5, REAPER, Bitwig, FL Studio, clap-validator) | pluginval step in CI, and no crashes or parameter-order problems |
| ASIO on Windows | A Steinberg ASIO SDK licence. Open the device in the selector and map 8 by 8. Right now Windows uses WASAPI or DirectSound. |
| Real-device tests: an 8 in, 8 out interface, device removal, and reconnect | A manual test plan |

### P2: important, not blocking

| Item | Notes |
|---|---|
| Streaming read-ahead for very long beds | See the note below. The spec describes streaming, but the audit lists it as partial. |
| Clip tempo sync and track monitor | The audit lists them as stored but inert. See the note below. |
| MIDI and OSC scene recall should honour the recall scope | See the note below. Later specs say this is done. |
| Group limiter gain reduction and per-send input meters | The audit lists these as not exposed. `ENGINE_SPEC.md` section 13 lists both as added to `EngineMeters`, so check the code. |
| Soak and memory-growth tests over hours, and a maximum-channel stress test on CI hardware | A nightly job on the target hardware |

### P3: future

| Item | Notes |
|---|---|
| AU format and auxiliary plug-in buses | Needs macOS host tests |
| True-peak limiter | Future work in the spec |
| Automatic dynamic notches | Future work in the spec. The feedback assistant does not insert notches on its own. |
| Code signing for Windows and macOS | Certificates, and notarisation for macOS |

## Where the specs and the audit disagree

The audit is dated 2026-10-14. Some later specs and the code describe features the audit still lists as incomplete. Check the code and the spec before you treat any of these as open.

| Feature | Audit says | Later spec or code says |
|---|---|---|
| FLAC recording | P3 future | `RECORDING_SPEC.md` section 2 describes 24-bit FLAC as supported, and the recorder has an FLAC writer path |
| Streaming playback | Partial, with no read-ahead from disk | `RECORDING_SPEC.md` section 9.1 describes a streaming read-ahead, and the recorder extras module has the stream pool |
| Clip tempo sync | Stored but inert | `RECORDING_SPEC.md` section 9.2 says sync acts on the tempo grid |
| Scene recall scope on MIDI and OSC | Gap | `BEAT_SYNC_SPEC.md` section 12 says every recall path uses the scope |
| Group limiter and FX send meters | Not exposed | `ENGINE_SPEC.md` section 13 lists them as added to `EngineMeters` |

## Not verified

These are not checked by the build environment or by CI. Treat them as open until someone checks them on a real machine.

- Real multichannel audio interfaces and ASIO
- Any DAW host (FL Studio, REAPER, Ableton, Bitwig)
- pluginval, the Steinberg validator, and clap-validator on macOS and Windows
- macOS and Linux desktop runs of the standalone
- Hours-long soak runs on real hardware
- Acoustic tests in a room
- The Intel macOS build, which is cross-compiled and never run
- The Windows installer's install and uninstall

## Known limits of the current beta

- Packages are unsigned and not notarized.
- The plug-ins are not validated in a DAW.
- The feedback assistant is advisory and tuned on synthetic signals.
- Loudness readings in the Show Report are an approximation.
- Plug-in buses: one main bus, no auxiliary buses.
- Device bindings are machine-local. Shows load unbound on another computer.

## Contributing to the roadmap

Pick an item from the P1 or P2 list, read its spec, and follow [[Contributing|Contributing]]. For a validation item, a clear report with the hardware, the host, and the steps is as useful as a code change.
