# CrowdMike Input and Output Specification

## 1. Standalone audio I/O

Use JUCE audio device management for:

- ASIO on Windows where available
- CoreAudio on macOS
- ALSA/JACK or current JUCE-supported Linux backends

The standalone app exposes:

- device selector
- sample rate
- buffer size
- input channel enable/assignment
- output channel enable/assignment
- device status
- xrun/dropout count

Because multichannel standalone layouts can depend on JUCE wrapper/device behavior, CrowdMike should own and test explicit device-channel mapping rather than assuming a default stereo layout is sufficient.

## 2. Plug-in buses

Plugin mode exposes a flexible primary bus plus optional auxiliary buses.

Target initial layouts:

- mono -> mono
- mono -> stereo
- stereo -> stereo
- 4 in -> stereo
- 4 in -> 4 out
- 8 in -> stereo
- 8 in -> 8 out

Additional layouts may be enabled after DAW compatibility testing.

JUCE bus access must use bus-aware APIs rather than assuming flat channel indices.

## 3. Channel safety

At every process callback:

- determine actual active input/output channel counts
- never read channels the active layout does not provide
- explicitly clear output channels not written by the engine
- support variable callback block lengths up to prepared maximum

## 4. Input controls

Per input:

- source selection
- enable
- mono/stereo interpretation
- trim
- polarity
- HPF
- LPF
- notch EQ
- alignment delay
- gate/expander
- de-esser
- compressor
- group sends
- FX sends
- record tap
- label/zone

## 5. Output controls

Per output bus/channel:

- source mix assignment
- trim
- mute
- polarity
- delay
- HPF/LPF protection filters
- limiter
- meter
- label/zone

## 6. Sample rates

Required validation targets:

- 44.1 kHz
- 48 kHz
- 88.2 kHz
- 96 kHz

Stretch target:

- 176.4 kHz
- 192 kHz

All time-based DSP uses sample-rate-independent parameterization.

## 7. Buffer sizes

Test at:

- 16
- 32
- 64
- 128
- 256
- 512
- 1024
- 2048 samples

No algorithm may assume fixed block length.

## 8. I/O persistence

Standalone device configuration is machine-local and should not blindly travel with portable show scenes.

Separate:

- portable scene routing
- local physical device binding

On another machine, unresolved bindings are clearly marked and fall back to safe disconnected state.
