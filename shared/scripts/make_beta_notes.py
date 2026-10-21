#!/usr/bin/env python3
"""Build the GitHub release notes for a CrowdMike beta pre-release.

Combines a fixed header (what is in the release, how it was verified, what was not) with the commit changelog
produced by shared/scripts/generate_release_notes.py.

usage: make_beta_notes.py --version 0.3.0-beta.1 --sha <sha> --assets-dir dist [--since v0.3.0-beta.0]
                          [--repo owner/name] [--run-url URL] [--validators-ran]
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# (substring of file name, label)
ASSET_LABELS = [
    ("Windows-x64-Setup.exe", "Windows x64 installer (Standalone + VST3 + CLAP)"),
    ("Windows-x64-portable.zip", "Windows x64 portable zip (Standalone + VST3 + CLAP)"),
    ("macOS-arm64.zip", "macOS Apple Silicon (arm64) - Standalone, VST3, CLAP"),
    ("macOS-x86_64.zip", "macOS Intel (x86_64) - Standalone, VST3, CLAP"),
    ("Linux-x86_64.tar.gz", "Linux x86_64 - Standalone, VST3, CLAP, install.sh"),
]


def changelog(version: str, since: str | None, root: Path) -> str:
    cmd = [sys.executable, str(HERE / "generate_release_notes.py"), "--project-root", str(root), "--version", version]
    if since:
        cmd += ["--since", since]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, check=False, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"(changelog unavailable: {exc})"
    text = out.stdout.strip()
    return text if out.returncode == 0 and text else "(changelog unavailable)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--sha", required=True)
    ap.add_argument("--assets-dir", required=True)
    ap.add_argument("--since", default=None, help="previous release tag for the changelog range")
    ap.add_argument("--repo", default="")
    ap.add_argument("--run-url", default="")
    ap.add_argument("--validators-ran", action="store_true")
    ap.add_argument("--project-root", default=".")
    args = ap.parse_args()

    assets = sorted(p.name for p in Path(args.assets_dir).iterdir() if p.is_file() and p.name != "SHA256SUMS")
    rows = []
    for needle, label in ASSET_LABELS:
        for name in assets:
            if name.endswith(needle):
                rows.append(f"| {label} | `{name}` |")
    missing = [label for needle, label in ASSET_LABELS if not any(a.endswith(needle) for a in assets)]

    out = [f"# CrowdMike {args.version} (beta)", ""]
    out.append("**Pre-release for testing.** Not for live shows without a rehearsal on your own hardware.")
    out.append("")
    out.append(f"Built from commit `{args.sha[:10]}`" + (f" ([workflow run]({args.run_url}))." if args.run_url else "."))
    out += ["", "## Downloads", "", "| Platform | File |", "|---|---|", *rows, "",
            "Verify downloads with `SHA256SUMS` (`sha256sum -c SHA256SUMS` on Linux, `shasum -a 256 -c SHA256SUMS` on macOS).", ""]
    if missing:
        out += ["Not part of this release build: " + "; ".join(missing) + ".", ""]
    out += [
        "## What was and was not verified", "",
        "- Unit, engine and recording tests passed on Linux, macOS (Apple Silicon) and Windows for this exact commit (CI gate).",
        "- Plug-in binaries were compiled and packaged only; they were not launched on every platform by the pipeline.",
        "- macOS Intel (x86_64) is cross-compiled on an Apple Silicon runner and has not been run on Intel hardware.",
        ("- pluginval (strictness 5) and clap-validator ran on the Linux build; results are informational and in the workflow run."
         if args.validators_ran else "- Plug-in validators (pluginval, clap-validator) were not run for this release."),
        "- Real audio interfaces, DAW hosts and long live use are not covered by the pipeline.",
        "- Windows builds are unsigned (SmartScreen may warn). macOS builds are ad-hoc signed only and **not notarized**: "
        "Gatekeeper blocks them until you remove the quarantine flag (see README-macOS.txt in the zip). Linux builds are unsigned.",
        "", "## Changes", "", changelog(args.version, args.since, Path(args.project_root).resolve()), ""]
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
