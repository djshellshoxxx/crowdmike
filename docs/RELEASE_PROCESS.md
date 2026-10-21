# CrowdMike Release Process (beta)

The build/release pipeline is split so that nothing re-runs the same tests.

| Workflow | Trigger | Does | Does not |
|---|---|---|---|
| `ci.yml` | PR, push to `main`, manual | Builds and runs `crowdmike_dsp_tests`, `crowdmike_recording_tests`, `crowdmike_tests` on Linux (xvfb), macOS 14 (arm64) and Windows with `ctest -LE soak`. Caches FetchContent (`.deps`) and ccache. Cancels superseded runs. | Package anything. Run soak tests. |
| `release-beta.yml` | manual (`workflow_dispatch`) or tag `beta-*` | Gate, per-platform packaging, optional validators, publish as GitHub **pre-release**. | Re-run tests. |
| `nightly.yml` | 04:23 UTC daily, manual | Builds `crowdmike_soak_tests`, runs `ctest -L soak` on Linux, macOS, Windows. | Run in PRs. |

Shared pieces: `.github/actions/setup-build` (OS dependencies, ccache, caches), `shared/scripts/ci_configure.sh` (Ninja
configure), `shared/scripts/package_beta.sh` (staging/zip/codesign per platform), `shared/scripts/make_beta_notes.py`
(release notes, reuses `generate_release_notes.py` for the changelog), `installer/` (Inno Setup script, READMEs,
`linux/install.sh`).

## Version

One number, three places that must agree: `project(CrowdMike VERSION X.Y.Z ...)` in `CMakeLists.txt`,
`kAppVersion` in `Source/PluginProcessor.h`, `CROWDMIKE_VERSION_STRING` default in `Source/Core/StateIO.cpp`
(and the default in `installer/CrowdMike.iss`). Currently **0.3.0**. Plug-in IDs (`PLUGIN_CODE CrM1`, bundle id,
CLAP id) never change with the version. The release workflow refuses a release whose numeric part
(`0.3.0` of `0.3.0-beta.1`) differs from `CMakeLists.txt`.

## Cut a beta

1. Merge to `main` and wait for `ci` to go green on the merge commit (the release gate checks that exact SHA).
2. Actions > `release-beta` > Run workflow on `main`:
   - `version`: `0.3.0-beta.1` (increment `beta.N` for each beta; the git tag becomes `v0.3.0-beta.1`)
   - `platforms`: `all`
   - `run_validators`: tick to also run pluginval (strictness 5) on the Linux VST3 and clap-validator on the CLAP.
3. The run: `prepare` (validates version) > `gate` (fails fast if `ci.yml` has no successful run for the SHA; it does
   not re-run tests) > `package-windows`, `package-macos` (arm64 and x64 jobs), `package-linux` in parallel >
   `validate` (optional) > `publish`.
4. Result: pre-release `v0.3.0-beta.1` (shown in the Releases "pre-release" section, never marked latest) with:

   | File | Content |
   |---|---|
   | `CrowdMike-<v>-Windows-x64-Setup.exe` | Inno Setup installer: Standalone + VST3 + CLAP |
   | `CrowdMike-<v>-Windows-x64-portable.zip` | same files, static CRT, no installer |
   | `CrowdMike-<v>-macOS-arm64.zip` | Apple Silicon: app, VST3, CLAP, README-macOS.txt (deployment target 11.0) |
   | `CrowdMike-<v>-macOS-x86_64.zip` | Intel: app, VST3, CLAP, README-macOS.txt (deployment target 10.15) |
   | `CrowdMike-<v>-Linux-x86_64.tar.gz` | standalone, VST3, CLAP, `install.sh`, README.txt |
   | `SHA256SUMS` | checksums of all of the above |

   Pushing a tag `beta-0.3.0-beta.1` does the same with `platforms=all` and validators on.

The old automatic `windows-build-<sha>` pre-release on every push to `main` no longer exists. The manual equivalent is
`release-beta` with `platforms=windows` (needs a green `ci` for the SHA and a version).

## Re-run

- Whole release again (same version, same commit): re-run the workflow with the same inputs. The release is edited in
  place, assets are replaced (`--clobber`), `SHA256SUMS` and notes are regenerated.
- One platform only: run `release-beta` with the same `version` and `platforms=windows|macos|linux` on the same
  commit. Only that platform is built; its assets are replaced, assets of the other platforms are kept, and
  `SHA256SUMS` is recomputed over everything. If the existing release was built from a different commit, a
  partial re-run fails on purpose (mixed-commit releases are refused); use `platforms=all` to replace the whole release
  (the old release and tag are deleted and recreated for the new SHA).
- GitHub's "Re-run failed jobs" on a failed run also works: successful package jobs are not repeated.

## What is NOT verified (state this in every beta)

- **Unsigned.** Windows binaries have no Authenticode signature (SmartScreen warns). Linux has no signatures.
- **No notarization.** macOS builds are only ad-hoc signed (needed for arm64 to load). Gatekeeper blocks them on first
  open; users must run `xattr -dr com.apple.quarantine` (documented in `README-macOS.txt` inside the zip).
- **macOS Intel is cross-compiled** on an Apple Silicon runner (`CMAKE_OSX_ARCHITECTURES=x86_64`), architecture-checked
  with `lipo` but never executed in CI. The `ci` test run covers arm64 only.
- Packages are compiled and archived, **not launched**: no automated start-up of the standalone, no plug-in load in a
  real DAW. Validators (when enabled) are informational and run on Linux only; macOS/Windows plug-ins are not
  validated.
- Windows installer/uninstaller behaviour is untested in CI. Deployment targets (11.0 / 10.15) are build settings; they
  are not tested on old systems.
- Real audio interfaces, host matrix (TESTING_SPEC section 8) and long live use are manual.
- The nightly soak tests run engine/recorder logic with simulated devices; real hardware disconnects are not covered.

## Troubleshooting

- `gate` fails: no successful `ci.yml` run for that SHA. Check the Actions tab, fix or wait, re-run `release-beta`.
- `prepare` fails on version: bump `CMakeLists.txt` (and the other places above) in a PR first.
- macOS x64 job fails the `lipo` check: a dependency built for the wrong architecture; check the configure log for
  `CMAKE_OSX_ARCHITECTURES`.
- Stale caches: bump `hashFiles` input (edit `CMakeLists.txt`) or delete caches in the Actions UI.
