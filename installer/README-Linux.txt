CrowdMike @VERSION@ (beta) - Linux x86_64

Contents
  CrowdMike        standalone application
  CrowdMike.vst3/  VST3 plug-in
  CrowdMike.clap   CLAP plug-in
  install.sh       installer script

Install (per user):   ./install.sh
Install (system):     sudo ./install.sh --system
Remove:               ./install.sh --uninstall   (add --system if installed system-wide)

Runtime requirements: ALSA (or JACK), X11, FreeType and Fontconfig (present on mainstream desktop distributions).
Built on Ubuntu (glibc of the build image); very old distributions may not start it.
This beta is unsigned and was validated on Linux with an automated test suite only, not in every DAW.
