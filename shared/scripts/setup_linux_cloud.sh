#!/usr/bin/env bash
# Debian 13 amd64 overlay for CrowdMike. Defaults to a read-only check.
# Usage: bash shared/scripts/setup_linux_cloud.sh [--check|--install] [/workspace/path/to/env]
set -euo pipefail

cm_mode="${1:---check}"
[[ $# -eq 0 ]] || shift
if [[ "$cm_mode" == --help ]]; then
  sed -n '2,3p' "${BASH_SOURCE[0]}"
  exit 0
fi
[[ "$cm_mode" == --check || "$cm_mode" == --install ]] || { echo "Use --check or --install." >&2; exit 2; }
[[ $# -le 1 ]] || { echo "Expected at most one environment path." >&2; exit 2; }
cm_prefix="$(realpath -m -- "${1:-/workspace/work/crowdmike-env}")"
[[ "$cm_prefix" == /workspace/* && "$cm_prefix" =~ ^[A-Za-z0-9_./-]+$ ]] || {
  echo "The environment path must be under /workspace, without spaces." >&2; exit 2;
}

if [[ "$cm_mode" == --install ]]; then
  [[ "$(dpkg --print-architecture)" == amd64 ]] || { echo "This setup targets Debian amd64." >&2; exit 1; }
  [[ "$(. /etc/os-release; printf '%s:%s' "$ID" "$VERSION_ID")" == debian:13 ]] || {
    echo "This package overlay targets Debian 13." >&2; exit 1;
  }
  mkdir -p "$cm_prefix"/{apt/empty,apt/lists/partial,apt/archives/partial,apt/log,sysroot,ccache,cache,config}
  cat > "$cm_prefix/apt/sources.list" <<'SOURCES'
deb https://deb.debian.org/debian trixie main
deb https://deb.debian.org/debian-security trixie-security main
SOURCES
  cat > "$cm_prefix/apt/apt.conf" <<CONFIG
Dir::Etc::Parts "$cm_prefix/apt/empty";
Dir::Etc::main "/dev/null";
Dir::Etc::sourcelist "$cm_prefix/apt/sources.list";
Dir::Etc::sourceparts "$cm_prefix/apt/empty";
Dir::Etc::trusted "/usr/share/keyrings/debian-archive-keyring.gpg";
Dir::Etc::trustedparts "$cm_prefix/apt/empty";
Dir::State::status "/var/lib/dpkg/status";
Dir::State::lists "$cm_prefix/apt/lists";
Dir::State::extended_states "$cm_prefix/apt/extended_states";
Dir::Cache::archives "$cm_prefix/apt/archives";
Dir::Cache::pkgcache "";
Dir::Cache::srcpkgcache "";
Dir::Log "$cm_prefix/apt/log";
Debug::NoLocking "true";
APT::Sandbox::User "$(id -un)";
Acquire::Retries "2";
CONFIG
  cm_packages=(cmake ninja-build ccache pkg-config xvfb xauth libasound2-dev libjack-jackd2-dev
    libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev
    libxinerama-dev libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev libxi-dev)
  APT_CONFIG="$cm_prefix/apt/apt.conf" /usr/bin/apt-get update
  APT_CONFIG="$cm_prefix/apt/apt.conf" /usr/bin/apt-get --download-only --no-install-recommends -y install "${cm_packages[@]}"
  shopt -s nullglob
  cm_archives=("$cm_prefix/apt/archives/"*.deb)
  [[ ${#cm_archives[@]} -gt 0 ]] || { echo "No cached Debian archives found." >&2; exit 1; }
  for cm_archive in "${cm_archives[@]}"; do dpkg-deb -x "$cm_archive" "$cm_prefix/sysroot"; done

  # Dev packages often link to a runtime already present in the immutable base.
  python3 -B - "$cm_prefix/sysroot/usr/lib/x86_64-linux-gnu" <<'PY'
from pathlib import Path
import sys
for link in Path(sys.argv[1]).glob('*.so*'):
    if not link.is_symlink() or link.exists():
        continue
    name = link.readlink().name
    runtime = next((directory / name for directory in (Path('/usr/lib/x86_64-linux-gnu'),
                   Path('/lib/x86_64-linux-gnu')) if (directory / name).exists()), None)
    if runtime is None:
        raise SystemExit(f'Missing runtime for {link}')
    link.unlink()
    link.symlink_to(runtime.resolve())
PY
  cat > "$cm_prefix/activate.sh" <<'ACTIVATE'
export CROWDMIKE_ENV_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$CROWDMIKE_ENV_ROOT/sysroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$CROWDMIKE_ENV_ROOT/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$CROWDMIKE_ENV_ROOT/sysroot"
export PKG_CONFIG_LIBDIR="$CROWDMIKE_ENV_ROOT/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$CROWDMIKE_ENV_ROOT/sysroot/usr/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
export CCACHE_DIR="$CROWDMIKE_ENV_ROOT/ccache"
export XDG_CACHE_HOME="$CROWDMIKE_ENV_ROOT/cache"
export XDG_CONFIG_HOME="$CROWDMIKE_ENV_ROOT/config"
ACTIVATE
fi

[[ -r "$cm_prefix/activate.sh" ]] || { echo "Missing activate.sh; use --install first." >&2; exit 1; }
source "$cm_prefix/activate.sh"
cmake --version | head -n 1
ninja --version
ccache --version | head -n 1
pkg-config --exists alsa jack freetype2 fontconfig x11 xext xcursor xinerama xrandr xrender xi gl
command -v Xvfb >/dev/null
command -v xvfb-run >/dev/null
command -v xauth >/dev/null
printf 'Environment checks passed: %s\nActivate with: source %s/activate.sh\n' "$cm_prefix" "$cm_prefix"
