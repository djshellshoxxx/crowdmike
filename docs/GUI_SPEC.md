# CrowdMike GUI Specification

## 1. GUI objectives

The interface must work in a dark venue, on a laptop screen, and under time pressure. It should expose signal flow visually and make unsafe states obvious.

Minimum supported logical size: 1100x650 (the window refuses to get smaller; below 1180 px wide the layout switches to compact automatically).
Reference sizes: 1280x720, 1600x900, 2560x1440 (all rendered and layout-checked by `ui_audit_layout_at_supported_sizes_and_modes`).
Resizable with scalable vector controls, a compact mode (no inspector, narrower rails), a high-contrast palette and a UI scale of 75 to 200 %.

## 2. Global layout

The main window is divided into five persistent regions.

### Top status bar

Displays:

- CrowdMike logo/name
- current scene
- sample rate
- block size
- measured/estimated total latency
- audio device/host state
- CPU load
- dropout/xrun counter
- record status
- panic mute
- global bypass

Critical states use text plus iconography, not color alone.

### Left navigation rail

Pages:

1. Live
2. Inputs
3. Groups
4. FX
5. Matrix
6. Record
7. Playback
8. Scenes
9. Settings
10. Diagnostics

### Center work area

Page-specific editor.

### Right inspector

Context-sensitive detailed parameters for the selected input, group, effect, output, clip, or scene.

### Bottom performance strip

Always-available:

- master crowd level
- master mute
- 4-8 assignable macro controls
- momentary FX buttons
- reverse trigger
- echo throw
- freeze/hold
- record marker
- panic

## 3. Live page

The Live page is the performance view.

Each input/group strip shows:

- name
- compact waveform or level history
- peak/RMS meter
- trim
- mute
- solo
- group destination
- delay value
- feedback-risk indicator
- compressor gain reduction
- record-arm state

Group/master area shows:

- crowd intensity meter
- stereo width indication
- combined waveform/history
- effect send activity
- limiter gain reduction
- output meter bank

A signal-flow ribbon across the top shows:
Input -> Align -> Clean -> Shape -> FX -> Master -> Output.

Clicking any stage jumps to its detailed editor.

## 4. Inputs page

Grid/list of input channels.

Per-channel controls:

- channel name
- hardware/host source assignment
- mono/stereo source mode
- trim: -60 to +24 dB
- polarity invert
- input mute/solo
- high-pass frequency
- low-pass frequency
- parametric notch enable/frequency/Q/depth
- alignment delay in samples/ms/metres
- de-esser summary
- compressor summary
- group assignments
- record arm
- input meter and clip latch

Expandable advanced section:

- full filter graph
- waveform
- spectrum
- correlation/polarity meter where stereo
- feedback history
- dynamics transfer curve

## 5. Groups page

Each group is a submix.

Controls:

- input membership
- group trim
- pan/balance
- mute/solo
- delay
- HPF/LPF
- compressor
- limiter
- effect sends
- width
- routing destinations

Typical default groups:

- Front Crowd
- Rear Crowd
- Balcony
- Stage/Room
- Broadcast Crowd
- Playback

## 6. FX page

Rack-style view with reorderable modules.

Default modules:

- Digital Delay/Echo
- Tape Delay
- Reverse Chunk
- Noise Exciter
- De-esser
- Compressor
- Limiter
- HPF/LPF
- Parametric/Notch EQ
- Width/Decorrelation
- Gate/Expander
- Stadium Reverb (FX 6, `fx.reverb.*`: size, decay, pre-delay incl. tempo sync, damping, low cut, diffusion, modulation, width, wet, freeze, tail kill)
- Crowd Multiplier (FX 7, `fx.multi.*`: voices, depth, rate, detune, scatter, tone, seed, level)

Every module has:

- bypass
- wet/dry
- input/output meters
- reset
- preset menu
- automation/macro assignment
- drag/reorder handle where topology permits

## 7. Matrix page

Matrix rows = sources.
Columns = destinations.

Possible sources:

- raw inputs
- processed inputs
- groups
- FX returns
- playback buses
- master crowd bus

Possible destinations:

- groups
- FX sends
- recorder tracks
- output buses

Cell behavior:

- off
- on at unity
- adjustable send level
- pre/post selection when valid

Visual protections:

- routes that would produce an invalid internal cycle are blocked
- direct live-mic-to-local-PA routes with risky gain can be flagged
- hovering a route highlights its complete path
- each destination shows summed headroom estimate

## 8. Record page

Features:

- track list with source assignment
- arm/mute/monitor
- file format
- bit depth
- destination folder
- free disk space
- elapsed/remaining estimate
- rolling waveform
- markers
- split file control
- record all button
- cue (monitor) pair and level, per-track MON switch (RECORDING_SPEC section 14)
- auto-record on host transport, pre/post-roll, disk guard thresholds, split on size (RECORDING_SPEC section 15)
- Show Report section under the track list: Export now, Open folder, auto-export, marker-on-peak settings and the last export result (SHOW_REPORT_SPEC section 10)

Recording status must remain visible globally.

## 9. Playback page

Clip slots and timeline/deck.

Per clip:

- waveform
- name
- duration
- loop
- one-shot
- gain
- fade in/out
- start/end trim
- reverse
- route
- hotkey/MIDI assignment
- sync mode where host tempo exists: tempo-sync toggle, grid (1/16 .. 2 bars) and loop length in beats (RECORDING_SPEC section 9.2)
- load state: resident, streamed (long clips are read ahead from disk, RECORDING_SPEC section 9.1), missing file, and an UNDERRUN indicator for 6 s after a streaming underrun

Virtual soundcheck mode can replace live inputs with recorded raw tracks.

## 10. Reverse visualisation

The reverse-chunk effect gets a dedicated circular or horizontal 4-second buffer display.

The display indicates:

- incoming capture head
- currently stored 4-second region
- playback head moving backward
- pending trigger
- crossfade regions
- wet/dry amount

Chunk duration is configurable, with 4.0 seconds as default.

## 11. Feedback view

Spectrum with detected stable peaks and persistence history.

For each candidate feedback frequency show:

- frequency
- estimated severity
- duration/persistence
- affected input/group/output if inferable
- suggested notch parameters
- operator button to apply a notch

Beta: no automatic notch insertion (the operator button is the only way a notch is applied). Automatic notches are out of scope for beta, see POST_BETA_SPEC.md section 4.

## 12. Accessibility and operation

- full keyboard navigation
- scalable UI
- high-contrast mode
- labels independent of color
- double-click numerical entry
- shift-drag for fine adjustment
- command/control-click reset to default
- right-click learn/assign menu
- optional tooltip help
- undo/redo for parameter and routing changes

## 13. Usability additions
- Live page: Ducker strip under the ribbon (DUCK toggle, key, target, exclude key, threshold/depth/attack/hold/release, key meter with threshold marker, GR meter). Group strips show limiter gain reduction; the master section shows FX send meters beside the returns.
- Inputs page: Duck key select in the top bar, "KEY" marker on the key row, and a non-modal Line Check panel (Start/Stop, Apply all, per-input Apply, Flip polarity, Undo, Quick start).
- Groups page: per-group limiter GR meter and FX send bus meters.

## 14. Beta additions per page (built)

| Where | Control | Binding |
|---|---|---|
| Status bar row 2 | Tempo widget: source, BPM read-out (`--.-` without tempo, `?` when confidence is low), TAP (momentary), beat LED (accent on the downbeat), confidence bar, trigger quantise | `tempo.source`, `tempo.quantise`, `tempo.tap` parameters; reads `TempoHub::snapshot()` |
| Status bar row 2 | Transient message line (scene recall, marker, errors) | `EditorContext::say` |
| Live | Ducker strip, energy sparkline, group limiter GR, FX send meters | `duck.*` parameters, meters |
| Inputs | Duck-key select, KEY marker, Line Check panel (non-modal) | `duck.key`, `LineCheckService` |
| FX | Stadium Reverb and Crowd Multiplier modules with the same bypass / reset / preset / macro affordances as the others; TAIL KILL on the reverb and echo modules | `fx.reverb.*`, `fx.multi.*` |
| Record | Show Report section, cue pair/level, MON switches | `ShowModel::report`, `ShowModel::recorder` |
| Playback | Tempo sync per clip, streaming and underrun indicators | clip settings, `PlaybackEngine::getStreamStats()` |
| Settings | Tempo / Beat Sync section, OSC cheat sheet incl. tempo addresses | tempo parameters, `ShowModel::osc` |
| Performance strip | TAIL KILL fades both echo throws and the stadium reverb tail while held; REC button shows the recorder state (record glyph, stop glyph) | `fx.echo.tailKill` + `fx.reverb.tailKill` |

Every visible control is bound to a parameter, a model field or an action with an observable effect; this is enforced by
`ui_audit_every_visible_control_is_wired_and_named` (buttons need a handler or a parameter attachment, sliders and combos
likewise, every interactive control needs an accessible title). Overlap, clipping and truncated captions are enforced at all
reference sizes and modes by `ui_audit_layout_at_supported_sizes_and_modes`.

## 15. Keyboard shortcuts

| Key | Action |
|---|---|
| F1 .. F10 | Live, Inputs, Groups, FX, Matrix, Record, Playback, Scenes, Settings, Diagnostics |
| F12 | Panic mute toggle |
| Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, Ctrl/Cmd+Y | Undo, redo, redo |
| Ctrl/Cmd+Left / Right | Previous / next scene (honours the recall scope) |
| Ctrl/Cmd+1 .. 9 (+Shift: 10 .. 18) | Recall scene slot |
| Ctrl/Cmd+R | Record start/stop |
| Ctrl/Cmd+M | Drop a marker |
| Ctrl/Cmd+I | Show/hide the inspector (not in compact mode) |
| E, V, K (hold) | Echo throw, Reverse, Tail kill (echo and stadium reverb) |
| Clip hotkeys | Assigned per clip on the Playback page; ignored while a text field has the focus |

Shift-drag = fine adjust, Ctrl/Cmd-click = reset to default, double-click = numeric entry, right-click = learn / assign / lock menu.
