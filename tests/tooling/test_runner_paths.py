# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Trusted verification paths use resolved identity and exact script positions."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from tools_path import ROOT

import run_fuzz_campaign
import run_native_suite
from fuzz_manifest import FuzzError, targets
from test_evidence import EvidenceError


class RunnerPathTests(unittest.TestCase):
    """Equivalent path spelling cannot hide real work or admit a substituted runner."""

    def test_fuzz_path_identity_and_runner_position(self) -> None:
        """Accept path aliases but refuse substituted runners and extra command arguments."""
        command = [
            sys.executable,
            str(ROOT / "tools/../tools/run_fuzzers.py"),
            "--work",
            str(ROOT / "fuzz-work/../fuzz-work"),
            "--target",
            "cli",
            "--seconds",
            "60",
            "--engine",
            "libfuzzer",
            "--binary",
            str(ROOT / "fuzz/../fuzz/de_fuzz_cli"),
        ]
        with patch("run_fuzz_campaign.configured_engine", return_value="libfuzzer"):
            run_fuzz_campaign.validate_command(command, ROOT, "cli", 60)
            substituted = [*command, str(ROOT / "tools/run_fuzzers.py")]
            substituted[1] = "/different/runner.py"
            overrides: list[list[str]] = []
            for flag, value in (
                ("--work", "/unrelated"),
                ("--seconds", "1800"),
                ("--binary", "/different/binary"),
                ("--engine", "afl"),
                ("--target", "decimal"),
            ):
                overrides.extend(([*command, flag, value], [*command, f"{flag}={value}"]))
            overrides.extend(([*command, "--unknown"], [*command, "--"], [*command, "extra"]))
            for altered in (substituted, command[:-1], *overrides):
                with self.assertRaises(FuzzError):
                    run_fuzz_campaign.validate_command(altered, ROOT, "cli", 60)

    def test_native_script_identity_and_runner_position(self) -> None:
        """Native discovery accepts aliases and refuses a script merely named as an argument."""
        scripts = sorted((ROOT / "tests/cli").glob("test_*.py"))
        tests: list[dict[str, Any]] = [
            {
                "name": script.stem,
                "command": [sys.executable, str(script.parent / "../cli" / script.name)],
            }
            for script in scripts
        ]
        tests += [
            {"name": f"fuzz-replay-{target.name}", "command": ["replay"]} for target in targets()
        ]
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            resource_binaries = {
                target: str(build / "tests" / (target + ".exe"))
                for target in ("de_fft_resources", "de_restoration_resources")
            }
            restoration = str(build / "alias/../tests/de_restoration_resources.exe")
            tests.extend(
                [
                    {
                        "name": "fft-native-resources",
                        "command": [str(build / "alias/../tests/de_fft_resources.exe")],
                    },
                    {"name": "restoration-native-controls", "command": [restoration, "--controls"]},
                    *(
                        {
                            "name": f"restoration-native-resources-{index}",
                            "command": [restoration, "--shard", str(index)],
                        }
                        for index in range(2)
                    ),
                ]
            )
            manifest = {
                "unit_binary": str(build / "unit"),
                "resource_binaries": resource_binaries,
                "static_tests": [test["name"] for test in tests],
            }
            (build / "native-tests.json").write_text(json.dumps(manifest))
            unit_command = [
                str(build / "alias/../unit"),
                "unit",
                "--reporter",
                "xml",
                "--abort",
                "--invisibles",
            ]
            tests.append({"name": "unit", "command": unit_command})
            listing = {"listings": {"tests": [{"name": "unit", "tags": []}]}}
            with patch(
                "run_native_suite.subprocess.run",
                return_value=subprocess.CompletedProcess([], 0, json.dumps(listing)),
            ):
                self.assertEqual(run_native_suite.registrations(build, tests), {"unit"})
                wrong_binary = [str(build / "different-unit"), *unit_command[1:]]
                for label, altered in (
                    ("missing-abort", [value for value in unit_command if value != "--abort"]),
                    (
                        "missing-invisibles",
                        [value for value in unit_command if value != "--invisibles"],
                    ),
                    ("wrong-unit-binary", wrong_binary),
                    ("extra-trailing-argument", [*unit_command, "extra"]),
                ):
                    tests[-1]["command"] = altered
                    with (
                        self.subTest(label=label),
                        self.assertRaisesRegex(
                            EvidenceError, "actual binary with fail-fast XML evidence"
                        ),
                    ):
                        run_native_suite.registrations(build, tests)
                tests[-1]["command"] = unit_command
                tests[-1]["name"] = "different-unit"
                with self.assertRaisesRegex(EvidenceError, "CTest differs"):
                    run_native_suite.registrations(build, tests)
                tests[-1]["name"] = "unit"
                self.assertEqual(run_native_suite.registrations(build, tests), {"unit"})
                tests[0]["command"] = [sys.executable, "/different/runner.py", str(scripts[0])]
                with self.assertRaises(EvidenceError):
                    run_native_suite.registrations(build, tests)

    def test_cmake_resource_inventory_keeps_actual_target_file_paths(self) -> None:
        """Real target-file metadata preserves explicit names without suffix guesses."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        source = (ROOT / "tests/CMakeLists.txt").read_text()
        marker = 'string(JSON de_native_inventory SET "${de_native_inventory}" "resource_binaries"'
        emission = source[source.index(marker) :]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = {
                "de_fft_resources": (root / "provider-fft.exe").as_posix(),
                "de_restoration_resources": (root / "provider-restoration.bin").as_posix(),
            }
            project = "cmake_minimum_required(VERSION 4.4)\nproject(Paths NONE)\n"
            for target, location in expected.items():
                project += f"add_executable({target} IMPORTED)\n"
                project += (
                    f'set_target_properties({target} PROPERTIES IMPORTED_LOCATION "{location}")\n'
                )
            project += 'set(de_native_inventory "{}")\n' + emission
            (root / "CMakeLists.txt").write_text(project)
            subprocess.run(
                [str(cmake), "-S", str(root), "-B", str(root / "build"), "-G", "Ninja"],
                check=True,
                capture_output=True,
                timeout=10,
            )
            actual = json.loads((root / "build/native-tests.json").read_text())
            self.assertEqual(actual["resource_binaries"], expected)
