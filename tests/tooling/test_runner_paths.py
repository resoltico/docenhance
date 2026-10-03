# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Trusted verification paths use resolved identity and exact script positions."""

from __future__ import annotations

import json
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
        """Accept traversal aliases but refuse a fake runner and missing flag value."""
        command = [
            sys.executable,
            str(ROOT / "tools/../tools/run_fuzzers.py"),
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
            for altered in (substituted, command[:-1]):
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
            manifest = {
                "unit_binary": str(build / "unit"),
                "static_tests": [test["name"] for test in tests],
            }
            (build / "native-tests.json").write_text(json.dumps(manifest))
            tests.append(
                {"name": "unit", "command": [manifest["unit_binary"], "--reporter", "xml"]}
            )
            listing = {"listings": {"tests": [{"name": "unit", "tags": []}]}}
            with patch(
                "run_native_suite.subprocess.run",
                return_value=subprocess.CompletedProcess([], 0, json.dumps(listing)),
            ):
                self.assertEqual(run_native_suite.registrations(build, tests), {"unit"})
                tests[0]["command"] = [sys.executable, "/different/runner.py", str(scripts[0])]
                with self.assertRaises(EvidenceError):
                    run_native_suite.registrations(build, tests)
