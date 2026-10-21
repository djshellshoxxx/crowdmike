# Interface Tour

> The window has a status bar on top, a nav rail of ten pages on the left, the page in the middle, an optional inspector on the right, and a performance strip along the bottom.
> Every core live action is reachable without a modal dialog, and the keyboard shortcuts below cover the rest.

## Layout

| Region | What it holds |
|---|---|
| Status bar (top) | Scene name, sample rate, buffer size, latency, device and host state, CPU, dropout count, record status, panic and bypass indicators. The second row carries tempo: source, BPM, TAP, beat LED, confidence, quantise. |
| Nav rail (left) | The ten pages. Keyboard: F1 to F10. |
| Page (centre) | The editor or view for the chosen page |
| Inspector (right) | Detailed settings for the selected input, group, effect, output, clip, or scene. Toggle with Ctrl+I. |
| Performance strip (bottom) | Master level and mute, macro controls, momentary FX buttons, reverse and echo triggers, freeze, record marker, panic |

Critical states use text and an icon, not colour alone. There is a high-contrast mode, a compact mode, and a UI scale setting. Fonts do not scale automatically with the window, so use the UI scale setting if text looks small.

## Pages

| Key | Page | Use it for |
|---|---|---|
| F1 | Live | Performance view. Input and group strips with meters, trims, mutes, feedback risk, and compressor gain reduction. The ducker strip, the flow ribbon, the energy sparkline, and the master section sit here too. |
| F2 | Inputs | Channel strips for all inputs. Trim, polarity, filters, notch, EQ, phase, alignment, dynamics, sends, and the advanced section. Holds the Line Check card and the duck key choice. |
| F3 | Groups | Crowd submixes: membership, trim, balance, delay, filters, EQ, compressor, limiter, width, and sends |
| F4 | FX | Rack view of the effects. Each module has bypass, wet/dry, meters, reset, and presets. |
| F5 | Matrix | The routing grid. Rows are sources, columns are outputs or destinations. |
| F6 | Record | Track list with arm, mute, and monitor, plus file format, folder, free space, markers, split, and the Show Report panel |
| F7 | Playback | Clip slots, waveform, loop, one-shot, gain, trim, reverse, route, hotkeys, and virtual soundcheck |
| F8 | Scenes | Scene slots, recall scope, ramp, locks, and notes |
| F9 | Settings | Audio I/O, MIDI, OSC, Tempo and Beat Sync, and the keyboard shortcut list |
| F10 | Diagnostics | Feedback view and system health (CPU, overruns, recorder dropouts, routing). Can copy a diagnostics report to the clipboard. |

## Keyboard shortcuts

From the in-app shortcut list (Settings page):

| Keys | Action |
|---|---|
| F1 to F10 | Go to Live, Inputs, Groups, FX, Matrix, Record, Playback, Scenes, Settings, Diagnostics |
| F12 | Panic mute, toggle |
| Ctrl+Z, Ctrl+Y (or Ctrl+Shift+Z) | Undo, redo |
| Ctrl+Left, Ctrl+Right | Previous and next scene |
| Ctrl+1 to Ctrl+9 | Recall scenes 1 to 9. Shift adds 9, so Ctrl+Shift+1 is scene 10, up to 18. |
| Ctrl+R | Record start and stop |
| Ctrl+M | Drop a marker |
| Ctrl+I | Show or hide the inspector |
| Hold E | Echo throw (momentary) |
| Hold V | Reverse trigger (momentary) |
| Hold K | Stadium reverb tail kill (momentary) |
| Tab, Shift+Tab | Move keyboard focus. Arrow keys adjust the focused control. |
| Shift-drag | Fine adjustment |
| Ctrl-click | Reset the control to its default |
| Double-click | Type a value, or rename a strip |
| Right-click | MIDI learn, macro assign, or lock against scene recall |

Clip hotkeys are assigned on the Playback page. Function keys and Ctrl shortcuts keep their global meaning, so pick a plain key for a clip.

## Tips for working live

- Keep the Live page open during a set. It has the things you need to act on first.
- Learn F12 and Ctrl+R before you need them.
- Use Diagnostics when something feels wrong. It shows overruns and dropouts in one place.
- Double-click a slider to type an exact value. Ctrl-click resets it.
