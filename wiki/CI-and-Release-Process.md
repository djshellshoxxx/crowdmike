# CI and Release Process

> Three workflows: `ci` tests every change, `release-beta` packages a beta from a commit that already passed CI, and `nightly` runs the soak tests.
> Nothing reruns the tests at release time. The release gate checks that `ci` passed on the exact commit being released.

## Workflows

| Workflow | Trigger | Does | Does not |
|---|---|---|---|
| `ci.yml` | Pull request, push to `main`, manual | Builds and runs `crowdmike_dsp_tests`, `crowdmike_recording_tests`, and `crowdmike_tests` on Linux (under xvfb), macOS 14 (arm64), and Windows, with `ctest -LE soak`. Caches FetchContent and ccache. Cancels superseded runs. | Package anything. Run soak tests. |
| `release-beta.yml` | Manual (`workflow_dispatch`) or a `beta-*` tag | Gate, per-platform packaging, optional validators, publishing as a GitHub pre-release | Rerun the tests |
| `nightly.yml` | 04:23 UTC daily, or manual | Builds `crowdmike_soak_tests` and runs `ctest -L soak` on Linux, macOS, and Windows | Run in pull requests |

Shared pieces:

- `.github/actions/setup-build`: OS dependencies, ccache, and caches
- `shared/scripts/ci_configure.sh`: Ninja configure
- `shared/scripts/package_beta.sh`: staging, zip, and codesign per platform
- `shared/scripts/make_beta_notes.py`: release notes, which reuse `generate_release_notes.py` for the change log
- `installer/`: the Inno Setup script, the READMEs, and `linux/install.sh`

## Version numbers

One version number lives in three places, and they must agree:

1. `project(CrowdMike VERSION X.Y.Z ...)` in `CMakeLists.txt`
2. `kAppVersion` in `Source/PluginProcessor.h`
3. `CROWDMIKE_VERSION_STRING` default in `Source/Core/StateIO.cpp` (and the default in `installer/CrowdMike.iss`)

The current number is **0.3.0**. The release workflow refuses a release whose numeric part (for example `0.3.0` in `0.3.0-beta.1`) does not match `CMakeLists.txt`.

Plug-in identity never changes with the version. That means the plug-in code `CrM1`, the bundle ID, and the CLAP ID stay the same.

## Cutting a beta

1. Merge to `main`, and wait for `ci` to go green on the merge commit. The release gate checks that exact SHA.
2. In Actions, open `release-beta` and run it on `main` with these inputs:
   - `version`: for example `0.3.0-beta.1`. Increase `beta.N` for each beta. The tag becomes `v0.3.0-beta.1`.
   - `platforms`: `all`
   - `run_validators`: tick to also run pluginval (strictness 5) on the Linux VST3 and clap-validator on the CLAP
3. The run goes through the jobs in this order:
   - `prepare`: validates the version
   - `gate`: fails fast if `ci.yml` has no successful run for the SHA. It does not rerun tests.
   - `package-windows`, `package-macos` (arm64 and x64 jobs), and `package-linux`, in parallel
   - `validate` (optional)
   - `publish`
4. The result is a pre-release `v0.3.0-beta.1`, shown in the pre-release section and never marked latest.

Pushing a `beta-0.3.0-beta.1` tag does the same thing, with `platforms=all` and validators on.

| File | Content |
|---|---|
| `CrowdMike-<v>-Windows-x64-Setup.exe` | Inno Setup installer: Standalone, VST3, and CLAP |
| `CrowdMike-<v>-Windows-x64-portable.zip` | The same files with a static runtime, no installer |
| `CrowdMike-<v>-macOS-arm64.zip` | Apple Silicon: app, VST3, CLAP, `README-macOS.txt` (deployment target 11.0) |
| `CrowdMike-<v>-macOS-x86_64.zip` | Intel: app, VST3, CLAP, `README-macOS.txt` (deployment target 10.15) |
| `CrowdMike-<v>-Linux-x86_64.tar.gz` | Standalone, VST3, CLAP, `install.sh`, `README.txt` |
| `SHA256SUMS` | Checksums of all of the above |

The old automatic `windows-build-<sha>` pre-release on every push to `main` no longer exists. The manual equivalent is `release-beta` with `platforms=windows`.

## Re-running a release

- **Whole release again** (same version, same commit): run the workflow again with the same inputs. The release is edited in place, assets are replaced, and `SHA256SUMS` and the notes are regenerated.
- **One platform only**: run `release-beta` with the same `version` and `platforms=windows`, `macos`, or `linux`, on the same commit. Only that platform is rebuilt. Its assets are replaced, the other platforms' assets are kept, and `SHA256SUMS` is recomputed over everything.
- If the existing release was built from a different commit, a partial re-run fails on purpose. Mixed-commit releases are refused. Use `platforms=all` to replace the whole release.
- GitHub's "Re-run failed jobs" also works on a failed run. Package jobs that succeeded are not repeated.

## What is not verified

Every beta should say this in its notes.

- **Unsigned.** Windows binaries have no Authenticode signature, so SmartScreen warns. Linux builds have no signatures.
- **Not notarized.** macOS builds are ad-hoc signed only. Gatekeeper blocks them on first open. Users run `xattr -dr com.apple.quarantine`, as described in `README-macOS.txt` in the zip.
- **Intel macOS is cross-compiled.** The x86_64 build is made on an Apple Silicon runner with `CMAKE_OSX_ARCHITECTURES=x86_64` and checked with `lipo`. It is never run in CI. The `ci` test run covers arm64 only.
- **Packages are compiled and archived, not launched.** Nothing starts the standalone in CI, and no plug-in is loaded in a real DAW. Validators (when enabled) are informational and run on Linux only.
- **The installer is not tested in CI.** Windows install and uninstall behaviour is untested. The deployment targets are build settings, not tested on old systems.
- **Hardware and host tests are manual.** Real audio interfaces, the host matrix, and long live use are not covered.
- **The nightly soak tests use simulated devices.** Real hardware disconnects are not covered.

## Troubleshooting the pipeline

| Symptom | Fix |
|---|---|
| `gate` fails | There is no successful `ci.yml` run for that SHA. Check the Actions tab, fix the problem or wait, then rerun `release-beta`. |
| `prepare` fails on the version | Bump the version in `CMakeLists.txt` and the other two places in a pull request first. |
| macOS x64 job fails the `lipo` check | A dependency was built for the wrong architecture. Check the configure log for `CMAKE_OSX_ARCHITECTURES`. |
| Stale caches | Change the `hashFiles` input (for example by editing `CMakeLists.txt`), or delete the caches in the Actions UI. |

## Related

[[Building from Source|Building-from-Source]] for the same commands you can run locally. [[Testing|Testing]] for the soak tests and what they check.
