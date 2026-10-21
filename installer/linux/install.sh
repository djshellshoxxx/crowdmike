#!/usr/bin/env bash
# Installs CrowdMike (standalone, VST3, CLAP) from this folder.
#   ./install.sh            per-user install (~/.local/bin, ~/.vst3, ~/.clap)
#   sudo ./install.sh --system   system-wide (/usr/local/bin, /usr/lib/vst3, /usr/lib/clap)
#   ./install.sh --uninstall [--system]
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system=0
uninstall=0
for a in "$@"; do
  case "$a" in
    --system) system=1 ;;
    --uninstall) uninstall=1 ;;
    -h|--help) sed -n '2,6p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

if [[ $system -eq 1 ]]; then
  bin_dir=/usr/local/bin; vst3_dir=/usr/lib/vst3; clap_dir=/usr/lib/clap
else
  bin_dir="$HOME/.local/bin"; vst3_dir="$HOME/.vst3"; clap_dir="$HOME/.clap"
fi

if [[ $uninstall -eq 1 ]]; then
  rm -f "$bin_dir/CrowdMike"
  rm -rf "$vst3_dir/CrowdMike.vst3" "$clap_dir/CrowdMike.clap"
  echo "CrowdMike removed."
  exit 0
fi

mkdir -p "$bin_dir" "$vst3_dir" "$clap_dir"
[[ -f "$here/CrowdMike" ]] && install -m 0755 "$here/CrowdMike" "$bin_dir/CrowdMike"
if [[ -d "$here/CrowdMike.vst3" ]]; then
  rm -rf "$vst3_dir/CrowdMike.vst3"
  cp -r "$here/CrowdMike.vst3" "$vst3_dir/"
fi
[[ -f "$here/CrowdMike.clap" ]] && install -m 0755 "$here/CrowdMike.clap" "$clap_dir/CrowdMike.clap"

echo "Installed:"
echo "  standalone  $bin_dir/CrowdMike"
echo "  VST3        $vst3_dir/CrowdMike.vst3"
echo "  CLAP        $clap_dir/CrowdMike.clap"
case ":$PATH:" in *":$bin_dir:"*) ;; *) echo "Note: $bin_dir is not on your PATH." ;; esac
