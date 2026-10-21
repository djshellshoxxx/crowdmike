# Getting Started

> Download the current beta from the Releases page, install it for your OS, and open the standalone app once to pick an audio device.
> Builds are unsigned, so expect a security prompt on Windows and macOS the first time.

## Download

Beta builds are published as **pre-releases** on the GitHub Releases page, in the pre-release section. They are never marked "latest". Each beta is tagged `vX.Y.Z-beta.N`. The current version in the code is 0.3.0.

| File | For | Contents |
|---|---|---|
| `CrowdMike-<v>-Windows-x64-Setup.exe` | Windows 64-bit | Installer: Standalone, VST3, CLAP, shortcuts, uninstaller |
| `CrowdMike-<v>-Windows-x64-portable.zip` | Windows, no install | Same files, static runtime, no installer |
| `CrowdMike-<v>-macOS-arm64.zip` | Apple Silicon Macs | App, VST3, CLAP, `README-macOS.txt` (minimum macOS 11.0) |
| `CrowdMike-<v>-macOS-x86_64.zip` | Intel Macs | App, VST3, CLAP, `README-macOS.txt` (minimum macOS 10.15) |
| `CrowdMike-<v>-Linux-x86_64.tar.gz` | Linux 64-bit | Standalone, VST3, CLAP, `install.sh`, `README.txt` |
| `SHA256SUMS` | Everyone | Checksums of all of the above |

Check the download against `SHA256SUMS` before you install it.

## Windows

**Installer (recommended).** Run the Setup file and follow the prompts. It installs the standalone app with shortcuts, and installs the VST3 and CLAP plug-ins. It has an uninstaller.

**Portable.** Unzip anywhere and run `CrowdMike.exe`. Copy the plug-ins by hand:

- `CrowdMike.vst3` (a folder) goes to `C:\Program Files\Common Files\VST3`.
- `CrowdMike.clap` (a file) goes to `C:\Program Files\Common Files\CLAP`.

**SmartScreen.** The build is unsigned, so SmartScreen may show a warning. Choose **More info**, then **Run anyway**.

## macOS

Both zips contain `CrowdMike.app`, `CrowdMike.vst3`, `CrowdMike.clap`, and `README-macOS.txt`.

1. Drag `CrowdMike.app` to `/Applications`.
2. Copy `CrowdMike.vst3` to `~/Library/Audio/Plug-Ins/VST3` (or `/Library/Audio/Plug-Ins/VST3`).
3. Copy `CrowdMike.clap` to `~/Library/Audio/Plug-Ins/CLAP` (or `/Library/Audio/Plug-Ins/CLAP`).

**Gatekeeper.** The beta is not notarized and only ad-hoc signed, so Gatekeeper refuses it the first time. Either remove the quarantine flag in Terminal:

```bash
# Clear the quarantine flag on the app and both plug-ins
xattr -dr com.apple.quarantine /Applications/CrowdMike.app
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/CrowdMike.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/CrowdMike.clap
```

or right-click `CrowdMike.app`, choose **Open**, and confirm. If a host reports a plug-in as blocked, allow it under **System Settings > Privacy & Security**.

Grant microphone access when the standalone app asks. Without it the app cannot take audio inputs.

**Intel build.** The x86_64 zip is cross-compiled on an Apple Silicon CI runner and checked with `lipo`, but it has never been run in CI. Treat it as less tested than the arm64 build.

## Linux

Unpack the tarball and run the install script from inside the folder.

```bash
# Per-user install (binary to ~/.local/bin, plug-ins to ~/.vst3 and ~/.clap)
tar -xzf CrowdMike-<v>-Linux-x86_64.tar.gz
cd CrowdMike-<v>-Linux-x86_64
./install.sh

# System-wide install (needs sudo)
sudo ./install.sh --system

# Remove (add --system if you installed system-wide)
./install.sh --uninstall
```

Runtime needs ALSA or JACK, X11, FreeType, and Fontconfig, which most desktop distributions already have. The build comes from an Ubuntu image, so very old distributions may not start it.

## First launch (standalone)

1. Open CrowdMike. The Live page is the first page.
2. Go to **Settings > Audio I/O**.
3. Pick the audio device, sample rate, and buffer size.
4. Enable the input channels you use and assign each to a physical input.
5. Enable the output channels you use and assign them to your PA, monitor, or recorder outputs.
6. Check the status bar: device state, sample rate, buffer size, CPU, and the dropout (xrun) counter should all look calm.

Device settings stay on this machine. They are not part of a show's scenes, so a show file loaded on another computer shows unresolved bindings and falls back to a safe, disconnected state until you bind the devices again.

Next, run [[Quick Start and Line Check|Quick-Start-Line-Check]] to set levels.

## First launch (plug-in)

In a VST3 or CLAP host, add CrowdMike to an audio track or bus. The host owns the audio device, so there is no device page to set up. Match the host's channel layout to the number of microphones you feed in. See [[Plugin Hosts|Plugin-Hosts]].

## Before a live show

- Run a rehearsal with [[Safety, Panic and Limiters|Safety-Panic-and-Limiters]] in mind: know where the panic toggle is (F12) and that rehearsal mode keeps outputs silent.
- Keep the Diagnostics page open once to confirm there are no overruns.
- Save a scene or show file before the doors open.
- Do not rely on any beta build as your only safety. Keep a regular PA processor or limiter in the chain.
