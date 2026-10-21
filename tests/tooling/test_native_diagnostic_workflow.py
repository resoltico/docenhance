# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual native upload policies refuse raw evidence and optional diagnostic delivery."""

from __future__ import annotations

import copy
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import workflow_integrity as integrity
from workflow_config import workflow

CONFIGURATIONS = (("ci.yml", "native"), ("ci.yml", "sanitize"), ("weekly.yml", "sanitize"))


class NativeDiagnosticWorkflowTests(unittest.TestCase):
    """Native and sanitizer jobs retain only their declared bounded evidence."""

    def test_current_native_uploads_match_actual_policy(self) -> None:
        """Every current workflow independently satisfies the complete semantic policy."""
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        for filename in ("ci.yml", "weekly.yml"):
            policy = (
                integrity.quality_workflow if filename == "ci.yml" else integrity.weekly_workflow
            )
            with self.subTest(filename=filename):
                policy(workflow(ROOT / ".github/workflows" / filename), major)

    def test_native_upload_mutations_refuse_missing_raw_and_optional_evidence(self) -> None:
        """All three actual jobs reject altered archive identity, bounds, fields and order."""
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        for filename, job_name in CONFIGURATIONS:
            original = workflow(ROOT / ".github/workflows" / filename)
            policy = (
                integrity.quality_workflow if filename == "ci.yml" else integrity.weekly_workflow
            )
            steps = original["jobs"][job_name]["steps"]
            position = next(
                i
                for i, step in enumerate(steps)
                if step.get("name") == "Retain complete native diagnostics"
            )
            for section, key, value in (
                ("", "name", "Ignore native evidence"),
                ("", "if", "success()"),
                ("", "continue-on-error", "true"),
                ("", "uses", "actions/upload-artifact@" + "0" * 40),
                ("with", "if-no-files-found", "warn"),
                ("with", "compression-level", "1"),
                ("with", "retention-days", "1"),
                ("with", "name", "unbound-diagnostic"),
                ("with", "path", "out/**"),
                ("with", "path", "out/release/app/native-*/ctest-output*"),
            ):
                changed = copy.deepcopy(original)
                step = changed["jobs"][job_name]["steps"][position]
                target = step[section] if section else step
                target[key] = value
                with (
                    self.subTest(file=filename, job=job_name, key=key, value=value),
                    self.assertRaisesRegex(ValueError, "Required native diagnostic upload"),
                ):
                    policy(changed, major)
            for alteration in ("missing", "duplicate", "before-build"):
                changed = copy.deepcopy(original)
                altered = changed["jobs"][job_name]["steps"]
                step = altered.pop(position)
                if alteration == "duplicate":
                    altered.extend((step, copy.deepcopy(step)))
                elif alteration == "before-build":
                    altered.insert(0, step)
                with (
                    self.subTest(file=filename, job=job_name, alteration=alteration),
                    self.assertRaisesRegex(ValueError, "Required native diagnostic upload"),
                ):
                    policy(changed, major)

    def test_manual_diagnostic_input_and_exact_fuzz_route(self) -> None:
        """Opt-in is typed/default-false; every unrelated required job remains unconditional."""
        original = workflow(ROOT / ".github/workflows/ci.yml")
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        integrity.quality_workflow(original, major)
        for key, value in (
            ("default", "true"),
            ("default", None),
            ("type", "string"),
            ("required", "true"),
        ):
            changed = copy.deepcopy(original)
            declaration = changed["on"]["workflow_dispatch"]["inputs"]["native_diagnostic"]
            if value is None:
                declaration.pop(key)
            else:
                declaration[key] = value
            with (
                self.subTest(input=key, value=value),
                self.assertRaisesRegex(ValueError, "default-false boolean"),
            ):
                integrity.quality_workflow(changed, major)
        null_condition = copy.deepcopy(original)
        null_condition["jobs"]["native"]["if"] = None
        with self.assertRaisesRegex(ValueError, "Required job"):
            integrity.quality_workflow(null_condition, major)
        for job, condition in (
            ("fuzz", None),
            ("fuzz", "false"),
            ("fuzz", "!inputs.native_diagnostic"),
            ("native", "!inputs.native_diagnostic"),
            ("sanitize", "false"),
            ("structural", "false"),
        ):
            changed = copy.deepcopy(original)
            if condition is None:
                changed["jobs"][job].pop("if")
            else:
                changed["jobs"][job]["if"] = condition
            with (
                self.subTest(job=job, condition=condition),
                self.assertRaisesRegex(ValueError, "Required job"),
            ):
                integrity.quality_workflow(changed, major)

    def test_diagnostic_cannot_turn_skipped_fuzz_into_gate_success(self) -> None:
        """Even diagnostic dispatch preserves the actual fatal fuzz-success assertion."""
        original = workflow(ROOT / ".github/workflows/ci.yml")
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        for alteration in ("omit-fuzz", "allow-skipped", "optional", "success-only"):
            changed = copy.deepcopy(original)
            gate = changed["jobs"]["gate"]
            if alteration == "omit-fuzz":
                gate["needs"].remove("fuzz")
            elif alteration == "allow-skipped":
                gate["steps"][0]["run"] = gate["steps"][0]["run"].replace(
                    'test "$FUZZ_RESULT" = success', 'test "$FUZZ_RESULT" != failure'
                )
            elif alteration == "optional":
                gate["continue-on-error"] = "true"
            else:
                gate["if"] = "success()"
            with (
                self.subTest(alteration=alteration),
                self.assertRaisesRegex(ValueError, "Aggregate"),
            ):
                integrity.quality_workflow(changed, major)

    def test_keep_going_environment_is_exact_manual_native_opt_in(self) -> None:
        """Both actual native build steps forward only the typed manual diagnostic decision."""
        original = workflow(ROOT / ".github/workflows/ci.yml")
        major = str(
            json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["version"]
        ).split(".")[0]
        integrity.quality_workflow(original, major)
        for command in ("./tools/ci_windows.ps1", "cmake --workflow --preset release"):
            for value in (None, "true", "false", "1", "${{ inputs.native_diagnostic }}"):
                changed = copy.deepcopy(original)
                step = next(
                    s for s in changed["jobs"]["native"]["steps"] if s.get("run") == command
                )
                if value is None:
                    step.pop("env")
                else:
                    step["env"]["DE_NATIVE_DIAGNOSTIC"] = value
                with (
                    self.subTest(command=command, value=value),
                    self.assertRaisesRegex(ValueError, "Required native diagnostic environment"),
                ):
                    integrity.quality_workflow(changed, major)

    def test_actual_native_build_command_keeps_only_literal_true_going(self) -> None:
        """Configure the production command fragment; no compiler or dependency build runs."""
        source = (ROOT / "cmake/Superbuild.cmake").read_text()
        start = source.index('set(de_native_build_suffix "")')
        end = source.index("add_custom_target(de_verify_configuration", start)
        fragment = source[start:end]
        references = set(re.findall(r"\$\{([A-Za-z_]\w*)\}", fragment))
        owned = {
            "de_native_build_suffix",
            "de_inner",
            "CMAKE_COMMAND",
            "PROJECT_SOURCE_DIR",
            "PROJECT_BINARY_DIR",
            "DE_BUILD_JOBS",
        }
        context = "".join(f'set({name} "")\n' for name in sorted(references - owned))
        cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ninja)
        for windows, value, accepted in (
            (True, None, True),
            (True, "", True),
            (True, "false", True),
            (True, "true", True),
            (True, "TRUE", False),
            (True, "1", False),
            (False, "true", True),
            (False, "nonsense", False),
        ):
            with (
                self.subTest(windows=windows, value=value),
                tempfile.TemporaryDirectory() as directory,
            ):
                root = Path(directory)
                text = (
                    "cmake_minimum_required(VERSION 4.4)\nproject(NativeCommand NONE)\n"
                    "include(ExternalProject)\n"
                    + context
                    + f'set(PROJECT_SOURCE_DIR "{ROOT.as_posix()}")\n'
                    f"set(WIN32 {'TRUE' if windows else 'FALSE'})\n"
                    "set(DE_BUILD_JOBS 3)\n"
                    + fragment
                    + "get_property(native_command TARGET de_native PROPERTY _EP_BUILD_COMMAND)\n"
                    + 'file(WRITE "${CMAKE_BINARY_DIR}/native-command.txt" "${native_command}")\n'
                )
                (root / "CMakeLists.txt").write_text(text)
                environment = os.environ.copy()
                environment.pop("DE_NATIVE_DIAGNOSTIC", None)
                if value is not None:
                    environment["DE_NATIVE_DIAGNOSTIC"] = value
                built = subprocess.run(
                    [
                        str(cmake),
                        "--warn-uninitialized",
                        "-Werror=dev",
                        "-S",
                        str(root),
                        "-B",
                        str(root / "build"),
                        "-G",
                        "Ninja",
                    ],
                    env=environment,
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=15,
                )
                if not accepted:
                    self.assertNotEqual(built.returncode, 0)
                    self.assertIn(
                        "DE_NATIVE_DIAGNOSTIC must be true, false or empty",
                        built.stdout + built.stderr,
                    )
                    continue
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                tokens = (root / "build/native-command.txt").read_text().split(";")
                tail = tokens[tokens.index("--parallel") :]
                expected = ["--parallel", "3"] + (["--", "-k", "0"] if value == "true" else [])
                self.assertEqual(tail, expected)

    def test_keep_going_ready_edge_does_not_erase_failure(self) -> None:
        """A tiny independent Ninja edge completes in diagnostic mode, still with failure."""
        ninja = shutil.which("ninja")
        self.assertIsNotNone(ninja)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "command.py"
            script.write_text(
                "import sys\nfrom pathlib import Path\n"
                "with Path('events').open('a') as stream: stream.write(sys.argv[1]+'\\n')\n"
                "if sys.argv[1] == 'fail': raise SystemExit(1)\n"
                "Path('marker').write_text('attained')\n"
            )
            quote = subprocess.list2cmdline if os.name == "nt" else shlex.join
            fail = quote([sys.executable, str(script), "fail"]).replace("$", "$$")
            mark = quote([sys.executable, str(script), "mark"]).replace("$", "$$")
            (root / "build.ninja").write_text(
                f"rule fail\n  command = {fail}\nrule mark\n  command = {mark}\n"
                "build failure: fail\nbuild marker: mark\ndefault failure marker\n"
            )
            for suffix in ([], ["-k", "0"]):
                (root / "marker").unlink(missing_ok=True)
                (root / "events").unlink(missing_ok=True)
                result = subprocess.run(
                    [
                        str(ninja),
                        "-f",
                        str(root / "build.ninja"),
                        "-j",
                        "1",
                        *suffix,
                        "failure",
                        "marker",
                    ],
                    cwd=root,
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=5,
                )
                self.assertNotEqual(result.returncode, 0)
                if suffix:
                    self.assertEqual((root / "marker").read_text(), "attained")
                    self.assertCountEqual(
                        (root / "events").read_text().splitlines(), ["fail", "mark"]
                    )
