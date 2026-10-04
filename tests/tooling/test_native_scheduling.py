# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Native process scheduling has a distinct bound from compiler-worker capacity."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from test_evidence import complete_junit


class NativeSchedulingTests(unittest.TestCase):
    """Actual CLI refusal and CTest weighted execution protect complete bounded scheduling."""

    def test_larger_native_process_count_is_refused_before_execution(self) -> None:
        """The real supervisor rejects the obsolete wider count before opening a build tree."""
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/run_native_suite.py"),
                "--build",
                "/absent-build",
                "--jobs",
                "3",
            ],
            capture_output=True,
            text=True,
            check=False,
            timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native test jobs must be in [1,2]", result.stderr)

    def test_weighted_architecture_case_runs_under_two_process_schedule(self) -> None:
        """Real CTest executes an over-capacity weighted case and reconciles complete results."""
        cmake = shutil.which("cmake")
        ctest = shutil.which("ctest")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ctest)
        with tempfile.TemporaryDirectory(prefix="native-process-control-") as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("""cmake_minimum_required(VERSION 4.4)
project(ProcessorsControl NONE)
enable_testing()
add_test(NAME weighted COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(weighted PROPERTIES PROCESSORS 4 TIMEOUT 5)
add_test(NAME ordinary COMMAND "${CMAKE_COMMAND}" -E true)
""")
            build = root / "build"
            subprocess.run(
                [str(cmake), "-S", str(root), "-B", str(build), "-G", "Ninja"],
                check=True,
                capture_output=True,
                timeout=30,
            )
            result = subprocess.run(
                [
                    str(ctest),
                    "--test-dir",
                    str(build),
                    "--parallel",
                    "2",
                    "--output-junit",
                    str(root / "result.xml"),
                    "--no-tests=error",
                ],
                capture_output=True,
                text=True,
                check=False,
                timeout=15,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            complete_junit(root / "result.xml", {"weighted", "ordinary"})
            listing = subprocess.check_output(
                [str(ctest), "--test-dir", str(build), "--show-only=json-v1"], text=True
            )
            self.assertEqual(
                {test["name"] for test in json.loads(listing)["tests"]}, {"weighted", "ordinary"}
            )
