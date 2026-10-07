# CrowdMike GUI Specification

## 1. GUI objectives

The interface must work in a dark venue, on a laptop screen, and under time pressure. It should expose signal flow visually and make unsafe states obvious.

Minimum supported logical size: 1280x720.
Preferred size: 1600x900 or larger.
Resizable with scalable vector controls and a compact mode.

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
- sync mode where host tempo exists

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

No automatic notch insertion in the first implementation unless enabled explicitly.

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
