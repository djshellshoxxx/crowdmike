# CrowdMike Linux development environment

Prepared on 2026-10-30 (America/Los_Angeles; 2026-10-31 UTC) in the selected managed
Debian 13.6 (`trixie`) amd64 environment.
The repository is `/workspace/crowdmike`. Tooling and development libraries are unpacked under
`/workspace/work/crowdmike-env/sysroot`; the base filesystem remains immutable.
No remote environment configuration was changed and no credentials are included in these files.

## Use the existing environment

```bash
source /workspace/work/crowdmike-env/activate.sh
cd /workspace/crowdmike
```

Activation selects the workspace CMake/Ninja/ccache binaries, runtime library path and pkg-config
search paths. It keeps ccache, application configuration and caches under the environment prefix.
The compiler and C++ standard library come from the Debian base.

| Component | Observed version |
|---|---|
| GCC / G++ | 14.2.0 |
| CMake | 3.31.6 |
| Ninja | 1.12.1 |
| ccache | 4.11.2 |
| pkg-config | 1.8.1 |
| JUCE | 9.0.3, pinned by `CMakeLists.txt` |
| CLAP JUCE extensions | `7adee3a1bd4684d4caa5601100e364abccff4b4d`, pinned by CMake |

Verify the prepared tooling without downloading packages or building:

```bash
bash shared/scripts/setup_linux_cloud.sh --check /workspace/work/crowdmike-env
```

The companion script passed Bash syntax checks and its read-only check mode against this prefix.
The `--install` path is supplied for recreation; it was not rerun during deliverable preparation.

## Configure, build and test

These are the settings used for the existing Release/Ninja build. Activate the environment before
the first configure. A build directory configured before activation may retain incorrect pkg-config
paths; use a fresh build directory when recreating the setup.

```bash
source /workspace/work/crowdmike-env/activate.sh
cd /workspace/crowdmike

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CROWDMIKE_ENV_ROOT/sysroot/usr" \
  -DCMAKE_INCLUDE_PATH="$CROWDMIKE_ENV_ROOT/sysroot/usr/include" \
  -DCMAKE_LIBRARY_PATH="$CROWDMIKE_ENV_ROOT/sysroot/usr/lib/x86_64-linux-gnu" \
  "-DCMAKE_C_FLAGS=-isystem $CROWDMIKE_ENV_ROOT/sysroot/usr/include" \
  "-DCMAKE_CXX_FLAGS=-fstack-usage -isystem $CROWDMIKE_ENV_ROOT/sysroot/usr/include" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

cmake --build build --parallel 2 --target \
  crowdmike_dsp_tests crowdmike_recording_tests crowdmike_tests \
  CrowdMike_Standalone CrowdMike_VST3 CrowdMike_CLAP

xvfb-run -a ctest --test-dir build -C Release -LE soak --output-on-failure --timeout 900
python3 -B -m unittest discover -s tests/scripts -v
```

`-fstack-usage` produces GCC stack-usage reports for the audit. CMake FetchContent uses
`build/_deps` in this local setup. Its initial fetch needs access to the pinned GitHub repositories.
The sysroot is an overlay on the same Debian base; it is not a cross-compilation sysroot.

Xvfb supplies a display for JUCE GUI tests and validators. `xauth` is already present in the base.
In this managed sandbox, Xvfb also needs permission to create local sockets; the agent ran commands
with the executor's additional network permission when required. A socket `EPERM` is an environment
permission problem and should not be counted as a passing or failing product test.

Build soak tests and run them separately when needed:

```bash
cmake --build build --parallel 2 --target crowdmike_soak_tests
xvfb-run -a ctest --test-dir build -C Release -L soak --output-on-failure --timeout 3000
```

## How the rootless setup works

The environment uses a private apt configuration at
`/workspace/work/crowdmike-env/apt/apt.conf`. Package indexes and downloaded archives stay under that
prefix. Apt reads the base `/var/lib/dpkg/status` and Debian archive keyring to resolve missing build
dependencies. It runs with `--download-only`; `dpkg-deb -x` then extracts the archives into the workspace.
There is no system package installation or package maintainer-script execution.

To recreate this overlay on the same Debian 13 amd64 base, with network access available:

```bash
bash shared/scripts/setup_linux_cloud.sh --install /workspace/work/crowdmike-env
source /workspace/work/crowdmike-env/activate.sh
```

The script sets `APT_CONFIG` before invoking `/usr/bin/apt-get`, so its private configuration applies
before apt loads global configuration parts and it bypasses shell wrappers around apt-get.
It accepts environment paths under `/workspace`, uses the inherited proxy/CA configuration,
and performs no product configure, build, installation or publishing. It does not change the home
directory or system directories. Its default mode is `--check`.

Some development packages contain unversioned `.so` links whose runtime targets already exist in
the immutable base rather than the extracted overlay. Those links initially dangled. They were
relinked inside the overlay to the matching existing `/usr/lib/x86_64-linux-gnu` runtime files;
the script performs the same repair after extraction. No dangling symlinks remained in the prepared
prefix. This arrangement depends on the selected base image's runtime libraries.

The initial apt download fetched 42 archives (26.9 MB). Apt's wording about packages being installed
or upgraded describes its download plan; the host package database was not modified. `xauth` was
already installed. The explicit package request was:

```text
cmake ninja-build ccache pkg-config xvfb xauth
libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev
libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev libxi-dev
```

Cached archive versions, including resolved dependencies, are listed below. Keep these `.deb` files
to retain the exact snapshot; `--install` resolves the versions available at execution time in Debian's
repositories.

```text
ccache=4.11.2-2
cmake=3.31.6-2
cmake-data=3.31.6-2
libasound2-dev=1.2.14-1+deb13u1
libbz2-dev=1.0.8-6
libdrm-dev=2.4.124-2
libdrm-nouveau2=2.4.124-2
libdrm-radeon1=2.4.124-2
libfontconfig-dev=2.15.0-2.3
libfontconfig1-dev=2.15.0-2.3
libfreetype-dev=2.13.3+dfsg-1+deb13u1
libgl-dev=1.7.0-1+b2
libglu1-mesa-dev=9.0.2-1.1+b3
libglx-dev=1.7.0-1+b2
libhiredis1.1.0=1.2.0-6+b3
libjack-jackd2-dev=1.9.22~dfsg-4
libopengl-dev=1.7.0-1+b2
libpciaccess-dev=0.17-3+b3
libpng-dev=1.6.48-1+deb13u6
libpng16-16t64=1.6.48-1+deb13u6
librhash1=1.4.5-1
libuv1t64=1.50.0-2
libx11-dev=2:1.8.12-1
libxau-dev=1:1.0.11-1
libxcb1-dev=1.17.0-2+b1
libxcomposite-dev=1:0.4.6-1
libxcursor-dev=1:1.2.3-1
libxdmcp-dev=1:1.1.5-1
libxext-dev=2:1.3.4-1+b3
libxfixes-dev=1:6.0.0-2+b4
libxi-dev=2:1.8.2-1
libxinerama-dev=2:1.1.4-3+b4
libxrandr-dev=2:1.5.4-1+b3
libxrender-dev=1:0.9.12-1
mesa-common-dev=25.0.7-2+deb13u1
ninja-build=1.12.1-1
pkg-config=1.8.1-4
uuid-dev=2.41.5-0+deb13u1
x11proto-dev=2024.1-1
xorg-sgml-doctools=1:1.11-1.1
xtrans-dev=1.4.0-1
xvfb=2:21.1.16-1.3+deb13u4
```

Evidence is retained in `/workspace/work/crowdmike-env/apt-download.log`, `configure.log`,
`apt/archives/`, and the existing build's `CMakeCache.txt`.

## Validator setup and pending checks

Linux validator executables are prepared at:

```text
/workspace/work/beta-pipeline-check/pluginval/pluginval
/workspace/work/beta-pipeline-check/clap-validator/clap-validator
```

The Tracktion `v1.0.4` Linux ZIP and free-audio CLAP validator `0.4.1` Ubuntu ZIP were downloaded from
their upstream GitHub releases. The CLAP ZIP contains a nested `.tar.gz`, which must also be extracted.
Both executables resolve their runtime libraries; CLAP reports `0.4.1` and pluginval help starts under Xvfb.
Once the Linux targets have built, run:

```bash
source /workspace/work/crowdmike-env/activate.sh
cd /workspace/crowdmike
mkdir -p /workspace/work/beta-pipeline-check/logs
xvfb-run -a /workspace/work/beta-pipeline-check/pluginval/pluginval \
  --strictness-level 5 --output-dir /workspace/work/beta-pipeline-check/logs \
  --validate "$PWD/build/CrowdMike_artefacts/Release/VST3/CrowdMike.vst3"
xvfb-run -a /workspace/work/beta-pipeline-check/clap-validator/clap-validator \
  validate "$PWD/build/CrowdMike_artefacts/Release/CLAP/CrowdMike.clap"
```

Product build and validation outcomes are recorded separately in the M8 audit. Real audio hardware,
DAW-host compatibility, device disconnects and extended live use remain pending. Windows/macOS
execution, installers, signatures/notarization and old-system deployment targets remain pending on
their corresponding platforms. No beta was published as part of environment setup.
