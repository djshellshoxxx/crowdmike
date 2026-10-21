CrowdMike @VERSION@ (beta) - macOS @ARCH@

Contents
  CrowdMike.app    standalone application
  CrowdMike.vst3   VST3 plug-in
  CrowdMike.clap   CLAP plug-in

Install
  Standalone : drag CrowdMike.app to /Applications
  VST3       : copy CrowdMike.vst3 to ~/Library/Audio/Plug-Ins/VST3  (or /Library/Audio/Plug-Ins/VST3)
  CLAP       : copy CrowdMike.clap to ~/Library/Audio/Plug-Ins/CLAP  (or /Library/Audio/Plug-Ins/CLAP)

This beta is NOT notarized and only ad-hoc signed. macOS Gatekeeper will refuse to open it the first time.
After copying the files, remove the quarantine flag (Terminal):

  xattr -dr com.apple.quarantine /Applications/CrowdMike.app
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/CrowdMike.vst3
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/CrowdMike.clap

Or right-click CrowdMike.app, choose Open, then confirm. For plug-ins, allow them under
System Settings > Privacy & Security if your host reports them as blocked.

Minimum macOS: @MINOS@.
Grant microphone access when the standalone application asks (audio inputs).
When reporting a problem, attach the report from the Diagnostics page.
