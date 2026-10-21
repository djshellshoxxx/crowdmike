#!/usr/bin/env bash
# Configure CrowdMike for CI: Ninja, Release, cached FetchContent directory, ccache launchers.
# usage: ci_configure.sh <build-dir> [extra cmake arguments...]
# Environment: GITHUB_WORKSPACE (defaults to the current directory).
set -euo pipefail

build_dir="${1:?usage: ci_configure.sh <build-dir> [cmake args...]}"
shift

root="${GITHUB_WORKSPACE:-$PWD}"
args=(
  -S "$root" -B "$build_dir" -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DFETCHCONTENT_BASE_DIR="$root/.deps"
  -DCMAKE_C_COMPILER_LAUNCHER=ccache
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
)
if [[ "$(uname -s)" == "Darwin" ]]; then
  args+=(-DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache -DCMAKE_OBJC_COMPILER_LAUNCHER=ccache)
fi

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # Windows runner: use MSVC (cl) and keep Git's /usr/bin/link from shadowing the MSVC linker.
    args+=(-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl)
    rm -f /usr/bin/link.exe 2>/dev/null || true
    ;;
esac

ccache --zero-stats >/dev/null 2>&1 || true
cmake "${args[@]}" "$@"
