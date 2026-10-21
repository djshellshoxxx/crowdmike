# Building from Source

> Configure with CMake and Ninja, fetch JUCE 9.0.3 and the CLAP extensions automatically, then build the test targets and the plug-ins.
> The commands below match what CI runs, so a green local build means CI should be happy too.

## Prerequisites

CMake 3.22 or newer is required. Ninja and ccache are used in CI and are recommended locally. The first configure downloads JUCE and clap-juce-extensions from GitHub, so you need network access then.

### Linux (Ubuntu, as in CI)

```bash
# Build tools, ccache, and the virtual X server used by the UI tests
sudo apt-get update
sudo apt-get install -y --no-install-recommends ninja-build ccache xvfb

# Audio, GUI, and web-view development packages JUCE needs
sudo apt-get install -y --no-install-recommends \
  libasound2-dev libjack-jackd2-dev ladspa-sdk libcurl4-openssl-dev \
  libfreetype6-dev libfontconfig1-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
  libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev libxi-dev
```

### macOS

Use Xcode command line tools, CMake, and Homebrew for the rest. CI runs on macOS 14 with arm64.

```bash
brew install ccache ninja
```

### Windows

CI uses MSVC for x64 with Chocolatey for the tools. Run the commands from a Developer PowerShell or a bash shell with the MSVC environment loaded.

```bash
choco install ccache ninja -y
```

## Configure

CI uses `shared/scripts/ci_configure.sh`, which sets Ninja, Release, a cached FetchContent directory, and ccache launchers. On your own machine you can run the same thing directly:

```bash
# Configure a Release build with Ninja, caching dependencies under ./.deps
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_BASE_DIR="$PWD/.deps" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

The plain form from the README also works: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`.

### Pinned dependencies

| Dependency | Version |
|---|---|
| JUCE | 9.0.3 (`GIT_TAG 9.0.3`) |
| clap-juce-extensions | A pinned commit in `CMakeLists.txt` |

Both are fetched by CMake's FetchContent. Do not change the pins in a feature PR. Dependency updates belong in their own change.

### macOS deployment target

`CMakeLists.txt` sets the macOS deployment target before `project()`. The default is 11.0, or 10.15 when `CMAKE_OSX_ARCHITECTURES` is `x86_64`. Release jobs pass the value explicitly.

## Build

```bash
# Test executables (the three that CI builds)
cmake --build build --target crowdmike_dsp_tests crowdmike_recording_tests crowdmike_tests

# Plug-ins and the standalone app
cmake --build build --target CrowdMike_All CrowdMike_CLAP
```

The README names `CrowdMike_All` for the Standalone and VST3 formats and `CrowdMike_CLAP` for the CLAP plug-in. Build `CrowdMike_Standalone` by itself if you only need the app.

## Run the tests

```bash
# Fast gate: everything except soak tests (use xvfb-run on a headless Linux machine)
xvfb-run -a ctest --test-dir build -C Release -LE soak --output-on-failure --timeout 900

# On macOS and Windows, run ctest without xvfb-run
ctest --test-dir build -C Release -LE soak --output-on-failure --timeout 900
```

For the soak tests, see [[Testing|Testing]].

## Speed up rebuilds

- Keep `.deps` and the ccache directory between builds. CI caches both.
- The setup action sets `CCACHE_DIR`, `CCACHE_MAXSIZE=600M`, and `CCACHE_COMPRESS=1`. Set the same in your shell if you want the same behaviour locally.
- Check the cache hit rate with `ccache --show-stats`. CI prints the same at the end of each run.
- If a build behaves strangely after a dependency change, clear the FetchContent directory and configure again.

## Troubleshooting the build

| Symptom | Likely cause |
|---|---|
| FetchContent cannot download JUCE | No network access, or the proxy blocks GitHub. Retry with access. |
| Missing X11 or ALSA headers on Linux | Install the Linux package list above |
| UI tests fail on Linux with no display | Run them under `xvfb-run` |
| Build is slow on every run | ccache is not picking up the cache directory |
| Windows link errors with the wrong `link.exe` | Load the MSVC environment first, so the MSVC linker is first on `PATH` |

## Notes

- Keep test runs deterministic: do not change assertions to make a test pass. See [[Testing|Testing]].
- The plug-in builds are not tested by the fast gate. Packaging happens in the release workflow. See [[CI and Release Process|CI-and-Release-Process]].
