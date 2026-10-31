"""Release regressions: python3 -B -m unittest discover -s tests/scripts -v."""

import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import textwrap
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]


def workflow_shell(step_name):
    """Read a literal shell step so the archive test runs the shipped commands."""
    lines = (ROOT / ".github/workflows/release-beta.yml").read_text().splitlines()
    start = lines.index(f"      - name: {step_name}")
    script = []
    in_run = False
    for line in lines[start + 1:]:
        if line.startswith("      - "):
            break
        if line == "        run: |":
            in_run = True
        elif in_run and line.startswith("          "):
            script.append(line[10:])
    return "\n".join(script)


class BetaReleaseTests(unittest.TestCase):
    def test_ci_gate_requires_success_from_a_direct_commit_checkout(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            gh = tmp / "gh"
            gh.write_text('#!/bin/sh\ncat "$RUNS_FIXTURE"\n')
            gh.chmod(0o755)
            fixture = tmp / "runs.txt"
            for runs, expected in [
                ("", 1),
                ("1 completed success pull_request\n", 1),
                ("1 completed success pull_request\n2 completed failure push\n", 1),
                ("1 in_progress - push\n", 1),
                ("1 completed success push\n", 0),
                ("1 completed success workflow_dispatch\n", 0),
                ("1 completed failure push\n2 completed success workflow_dispatch\n", 0),
            ]:
                with self.subTest(runs=runs):
                    fixture.write_text(runs)
                    result = subprocess.run(
                        ["bash", "-euo", "pipefail", "-c", workflow_shell("Require a successful ci.yml run for the exact commit")],
                        cwd=tmp, env={**os.environ, "PATH": f"{tmp}:{os.environ['PATH']}",
                                      "RUNS_FIXTURE": str(fixture), "SHA": "abc123", "REPO": "test/crowdmike"},
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_orphan_release_tag_must_match_the_package_commit(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "dist").mkdir()
            (tmp / "dist/package.zip").touch()
            (tmp / "bin").mkdir()
            gh = tmp / "bin/gh"
            gh.write_text("#!/bin/sh\n[ \"$1 $2\" = 'release view' ] && exit 1\nexit 99\n")
            gh.chmod(0o755)

            def git(*args):
                return subprocess.check_output(
                    ["git", "-c", "user.name=Release Test", "-c", "user.email=test@example.invalid", *args],
                    cwd=tmp, text=True,
                ).strip()

            git("init", "--quiet")
            git("commit", "--quiet", "--allow-empty", "-m", "first")
            first = git("rev-parse", "HEAD")
            git("commit", "--quiet", "--allow-empty", "-m", "second")
            current = git("rev-parse", "HEAD")
            tag = "v0.3.0-beta.1"
            for kind, target, expected in [("absent", None, 0), ("lightweight", current, 0),
                                           ("annotated", current, 0), ("mismatch", first, 1)]:
                with self.subTest(tag=kind):
                    if target:
                        git("tag", *(["-a", "-m", "beta"] if kind == "annotated" else []), tag, target)
                    result = subprocess.run(
                        ["bash", "-euo", "pipefail", "-c", workflow_shell("Reconcile with an existing release of the same version")],
                        cwd=tmp, env={**os.environ, "PATH": f"{tmp / 'bin'}:{os.environ['PATH']}",
                                      "SHA": current, "TAG": tag, "PLATFORMS": "all", "REPO": "test/crowdmike"},
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                    if target:
                        self.assertEqual(git("rev-parse", f"{tag}^{{commit}}"), target)
                        git("tag", "-d", tag)

    def test_fetch_validators_unwraps_nested_clap_release_archive(self):
        # Upstream 0.4.1 publishes an Ubuntu ZIP containing a .tar.gz, not a binary.
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fixtures = tmp / "fixtures"
            fixtures.mkdir()
            (tmp / "tools").mkdir()
            (tmp / "bin").mkdir()
            with zipfile.ZipFile(fixtures / "pluginval_Linux.zip", "w") as archive:
                archive.writestr("pluginval", "#!/bin/sh\nexit 0\n")
            tar_bytes = io.BytesIO()
            with tarfile.open(fileobj=tar_bytes, mode="w:gz") as archive:
                binary = b"#!/bin/sh\nexit 0\n"
                info = tarfile.TarInfo("clap-validator")
                info.size = len(binary)
                archive.addfile(info, io.BytesIO(binary))
            with zipfile.ZipFile(fixtures / "clap-validator-0.4.1-ubuntu-22.04.zip", "w") as archive:
                archive.writestr("clap-validator-0.3.2-ubuntu-22.04.tar.gz", tar_bytes.getvalue())
            gh = tmp / "bin/gh"
            gh.write_text(textwrap.dedent("""\
                #!/usr/bin/env bash
                set -euo pipefail
                shift 2
                while (( $# )); do
                  case "$1" in
                    --repo) shift 2 ;;
                    --pattern) pattern="$2"; shift 2 ;;
                    --dir) dest="$2"; shift 2 ;;
                    *) exit 2 ;;
                  esac
                done
                for file in "$FIXTURES"/*.zip; do
                  [[ "${file##*/}" == $pattern ]] && cp "$file" "$dest/"
                done
                exit 0
                """))
            gh.chmod(0o755)
            result = subprocess.run(
                ["bash", "-euo", "pipefail", "-c", workflow_shell("Fetch validators (latest upstream releases)")],
                cwd=tmp, env={**os.environ, "PATH": f"{tmp / 'bin'}:{os.environ['PATH']}",
                              "FIXTURES": str(fixtures)}, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(os.access(tmp / "tools/pluginval/pluginval", os.X_OK))
            self.assertTrue(os.access(tmp / "tools/clap-validator/clap-validator", os.X_OK))

    def test_notes_report_each_validator_result_including_setup_skips(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "shared").symlink_to(ROOT / "shared", target_is_directory=True)
            (tmp / "dist").mkdir()
            (tmp / "dist/CrowdMike-0.3.0-beta.1-Linux-x86_64.tar.gz").touch()
            for pluginval, clapval, expected_pluginval, expected_clapval in [
                ("success", "success", "passed", "passed"),
                ("failure", "success", "failed", "passed"),
                ("skipped", "skipped", "did not run", "did not run"),
                ("success", "skipped", "passed", "did not run"),
                ("cancelled", "skipped", "was cancelled", "did not run"),
            ]:
                with self.subTest(pluginval=pluginval, clapval=clapval):
                    result = subprocess.run(
                        ["bash", "-euo", "pipefail", "-c", workflow_shell("Checksums and notes")],
                        cwd=tmp, env={**os.environ, "VERSION": "0.3.0-beta.1", "SHA": "abc123",
                                      "REPO": "test/crowdmike", "GITHUB_SERVER_URL": "https://github.com",
                                      "GITHUB_RUN_ID": "1", "TAG": "v0.3.0-beta.1", "PLUGINVAL_RESULT": pluginval,
                                      "CLAPVAL_RESULT": clapval, "GIT_DIR": str(ROOT / ".git"),
                                      "GIT_WORK_TREE": str(ROOT)}, capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn(f"pluginval (strictness 5): **{expected_pluginval}**", result.stdout)
                    self.assertIn(f"clap-validator: **{expected_clapval}**", result.stdout)
                    if pluginval == "skipped" and clapval == "skipped":
                        self.assertNotIn("ran on the Linux build", result.stdout)


if __name__ == "__main__":
    unittest.main()
