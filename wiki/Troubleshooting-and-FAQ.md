# Troubleshooting and FAQ

> Most problems are one of four things: the app will not open, there is no sound, the room is ringing, or the disk or the audio device cannot keep up.
> Start with the Diagnostics page, and attach its report when you ask for help.

## Install and launch

### macOS says the app is damaged or cannot be opened

The beta is not notarized and only ad-hoc signed, so Gatekeeper blocks it the first time. Clear the quarantine flag, or right-click the app, choose Open, and confirm. See [[Getting Started|Getting-Started]].

### Windows SmartScreen blocks the installer

The build is unsigned. Choose **More info**, then **Run anyway**.

### Linux: the standalone will not start

Check that ALSA or JACK, X11, FreeType, and Fontconfig are installed. Very old distributions may not run the build, because it is built against an Ubuntu image.

### A plug-in does not show up in the host

- Put the VST3 folder and the CLAP file in the locations listed in [[Getting Started|Getting-Started]], then rescan the host's plug-in list.
- On macOS, remove the quarantine flag from the plug-in, and allow it in System Settings under Privacy & Security if the host reports it as blocked.
- Use a host that supports the layout you need. See [[Plugin Hosts|Plugin-Hosts]].

## Sound

### No sound from the standalone outputs

Work through the chain from input to output:

1. The status bar shows the device and the correct sample rate, and no device error.
2. The input is enabled, assigned to a real input, and shows a level on its meter. Run [[Line Check|Quick-Start-Line-Check]] if you are not sure.
3. The input or group is not muted, and nothing is soloed that should not be.
4. The output is enabled, assigned, and not muted. Its trim is not at the bottom.
5. The master level is not down, and master mute is off.
6. Panic (F12) is not on. Rehearsal mode is not on, because rehearsal keeps outputs silent.
7. The matrix has a route from a source to that output.

### No sound from the plug-in

Check the host's routing first: the track must receive the plug-in's output, and the channel layout must match what you feed in. Then check the same chain as above, starting at the matrix.

### The levels are too low or too high

Use Line Check to propose trims. Then check the output trims and the master level. The output limiter has a ceiling of -0.5 dBFS by default, so a loud signal will sit against the ceiling.

### Clipping

The Clipping verdict on Line Check means a sample reached full scale. Lower the input trim. The limiters protect the outputs, but a clipped input has already lost information.

### The room rings or squeals

1. Press **F12** for panic, then bring the level back down.
2. Lower the output trim or master level for the zone that rings.
3. Check the feedback-risk indicator on the Live page.
4. Use the [[Feedback Assistant|Feedback-Assistant]] to find the frequency and apply a notch.
5. Use the high-pass on the mic and the Performer Ducker when the performer talks.

Delay does not stop feedback. See [[Concepts|Concepts]].

## Performance and dropouts

### Crackle, clicks, or dropouts

- Increase the buffer size in the audio settings, then test again.
- Close other audio and heavy applications.
- Check the Diagnostics page. Overruns mean the audio callback ran out of time. Recorder dropouts mean the disk could not keep up.
- Reduce the number of recorded tracks.

### The status bar shows an xrun or overrun count that keeps rising

The audio device or the buffer size is the likely cause. Try a larger buffer, a different device, or a different driver setting, then watch the count.

### Clip shows UNDERRUN

A streamed clip ran out of data because the disk was slow. Put long clips on a fast drive, and avoid many streamed voices at once. See [[Recording and Playback|Recording-and-Playback]].

## Recording

### LOW DISK or DISK FULL: STOPPED

Free space in the record folder is low. The take stops cleanly below 100 MB by default. Free space or change the folder before you start the next take.

### Recorder reports a dropout

The writer could not keep up with the incoming audio. Live sound was not affected. Use a faster disk, record fewer tracks, or move the record folder off a network drive.

### My take did not start

The app refuses a take when free space is already below the stop threshold. Check the free space readout on the Record page.

### Where are my files?

Takes go in the record folder, by default `~/Music/CrowdMike`. Each take has its own session JSON file. The Show Report files are in the session folder.

## Tempo and timing

### The BPM read-out shows `--.-`

There is no tempo. Choose a source. Use Tap or Manual in standalone, or Host in a DAW with a playing transport.

### The BPM has a `?` after it

The tempo source has low confidence. Tap the tempo, or set a Manual BPM.

### Triggers feel late

Trigger quantise is on. A trigger waits for the next grid line, so set quantise to Off for an instant trigger. See [[Beat Sync and Tempo|Beat-Sync-and-Tempo]].

## Control

### OSC commands do nothing

- Is OSC enabled in Settings > OSC?
- Is the port right (default 9000)?
- Is "Localhost only" on, and is the sender on the same machine? If not, turn it off and bind to the right interface.
- Is a firewall blocking UDP on that port?

### A MIDI controller does not bind

- Is the device enabled in Settings > MIDI?
- Is the binding set to the right message type and number?
- Is the channel set to omni, or to the channel the controller sends on?
- MIDI learn works in the standalone app. Plug-ins use host automation.

### A parameter will not change when I recall a scene

The parameter is probably locked against recall, or the recall scope leaves that part of the show alone. Check the lock state and the scope on the Scenes page.

## Show Report

### The report is not exported

Check that the report is enabled and that auto-export is on, or press **Export now**. A report with no samples yet is refused, and the status says so.

## Show files and other computers

Device bindings (which physical input or output a channel uses) stay on the computer that made them. A show loaded on another computer shows unresolved bindings until you bind the devices again.

## Reporting a problem

Open the Diagnostics page and copy the diagnostics report to the clipboard, then paste it into your report with a description of what you were doing. The bundled macOS README asks for the same thing.

## FAQ

### Is CrowdMike a replacement for my PA processor?

No. It is a crowd processor and router. It does not replace a dedicated loudspeaker processor or do room calibration.

### Can it remove the crowd noise from the room?

No. Polarity and delay cannot cancel a diffuse crowd across a room, and CrowdMike does not claim to.

### Does it record in FLAC?

Yes, 24-bit FLAC is in the recorder. The feature audit still lists it as future work, so check the release notes for your build.

### Does it stream to the internet?

No built-in stream is described in the docs. Route an output to your streaming software.

### Is there an AU version?

No. It is listed as future work.
