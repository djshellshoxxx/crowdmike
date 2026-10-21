#!/usr/bin/env bash
# Stage and archive the CrowdMike plug-in build outputs for one platform.
#
# usage: package_beta.sh <platform> <version> <build-dir> <out-dir>
#   platform : windows-x64 | macos-arm64 | macos-x64 | linux-x64
#   version  : release version string, e.g. 0.3.0-beta.1
#
# Expects the Standalone, VST3 and CLAP targets to be built in <build-dir>. Produces in <out-dir>:
#   windows-x64 : CrowdMike-<v>-Windows-x64-portable.zip  and an unpacked "stage" folder for the Inno Setup step
#   macos-*     : CrowdMike-<v>-macOS-<arm64|x86_64>.zip   (ad-hoc codesigned)
#   linux-x64   : CrowdMike-<v>-Linux-x86_64.tar.gz
set -euo pipefail

platform="${1:?platform}"; version="${2:?version}"; build_dir="${3:?build dir}"; out_dir="${4:?out dir}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
stage_root="$out_dir/stage-$platform"
rm -rf "$stage_root"; mkdir -p "$stage_root"

# Git for Windows ships a real find; System32\find.exe could shadow it on a Windows runner.
FIND=find; [[ -x /usr/bin/find ]] && FIND=/usr/bin/find

die() { echo "error: $*" >&2; exit 1; }
# find_one <type f|d> <name> : locate a build output anywhere under the build dir
find_one() {
  local hit
  hit="$("$FIND" "$build_dir" -type "$1" -name "$2" -not -path '*/CMakeFiles/*' -print -quit)"
  [[ -n "$hit" ]] || die "$2 not found under $build_dir"
  echo "$hit"
}
fill_readme() {  # fill_readme <template> <dest> [arch] [min macOS]
  sed -e "s/@VERSION@/$version/g" -e "s/@ARCH@/${3:-}/g" -e "s/@MINOS@/${4:-}/g" "$1" > "$2"
}

case "$platform" in
  windows-x64)
    s="$stage_root/CrowdMike"; mkdir -p "$s"
    cp "$(find_one f CrowdMike.exe)" "$s/"
    cp -r "$(find_one d CrowdMike.vst3)" "$s/"
    cp "$(find_one f CrowdMike.clap)" "$s/"
    cp "$root/README.md" "$s/"
    fill_readme "$root/installer/README-Windows-portable.txt" "$s/README-portable.txt"
    rm -rf "$out_dir/stage"; cp -r "$s" "$out_dir/stage"      # unpacked copy for Inno Setup
    zip_path="$out_dir/CrowdMike-$version-Windows-x64-portable.zip"
    rm -f "$zip_path"
    (cd "$stage_root" && 7z a -tzip "$zip_path" CrowdMike >/dev/null)
    ;;

  macos-arm64|macos-x64)
    if [[ "$platform" == "macos-arm64" ]]; then arch=arm64; minos="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
    else arch=x86_64; minos="${MACOSX_DEPLOYMENT_TARGET:-10.15}"; fi
    name="CrowdMike-$version-macOS-$arch"
    s="$stage_root/$name"; mkdir -p "$s"
    cp -R "$(find_one d CrowdMike.app)" "$s/"
    cp -R "$(find_one d CrowdMike.vst3)" "$s/"
    cp -R "$(find_one d CrowdMike.clap)" "$s/"
    for b in "$s/CrowdMike.app/Contents/MacOS/CrowdMike" "$s/CrowdMike.vst3/Contents/MacOS/CrowdMike" \
             "$s/CrowdMike.clap/Contents/MacOS/CrowdMike"; do
      [[ -f "$b" ]] || die "missing binary $b"
      archs="$(lipo -archs "$b")"
      [[ "$archs" == "$arch" ]] || die "$b has architectures '$archs', expected '$arch'"
    done
    # Ad-hoc signature: arm64 binaries must carry one to load at all. It does NOT satisfy Gatekeeper.
    for item in CrowdMike.app CrowdMike.vst3 CrowdMike.clap; do
      codesign --force --deep --sign - "$s/$item"
      codesign --verify --deep --strict "$s/$item"
    done
    fill_readme "$root/installer/README-macOS.txt" "$s/README-macOS.txt" "$arch" "$minos"
    # ditto keeps bundle structure, symlinks and signatures intact
    (cd "$stage_root" && ditto -c -k --keepParent "$name" "$out_dir/$name.zip")
    ;;

  linux-x64)
    name="CrowdMike-$version-Linux-x86_64"
    s="$stage_root/$name"; mkdir -p "$s"
    standalone="$build_dir/CrowdMike_artefacts/Release/Standalone/CrowdMike"
    [[ -f "$standalone" ]] || die "standalone binary not found at $standalone"
    cp "$standalone" "$s/CrowdMike"
    cp -r "$(find_one d CrowdMike.vst3)" "$s/"
    cp "$(find_one f CrowdMike.clap)" "$s/"
    chmod +x "$s/CrowdMike" "$s/CrowdMike.clap"
    install -m 0755 "$root/installer/linux/install.sh" "$s/install.sh"
    fill_readme "$root/installer/README-Linux.txt" "$s/README.txt"
    tar -C "$stage_root" -czf "$out_dir/$name.tar.gz" "$name"
    ;;

  *) die "unknown platform '$platform'" ;;
esac

rm -rf "$stage_root"
echo "Packaged $platform:"
ls -l "$out_dir"
