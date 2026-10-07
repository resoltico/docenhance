# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Native process scheduling has a distinct bound from compiler-worker capacity."""

from __future__ import annotations

import json
import os
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

    def test_environment_cannot_increase_or_invalidate_capacity(self) -> None:
        """The real workflow entry refuses bad environment capacity before accessing its build."""
        for value in ("0", "3", "invalid"):
            with self.subTest(value=value):
                result = subprocess.run(
                    [
                        sys.executable,
                        str(ROOT / "tools/run_native_suite.py"),
                        "--build",
                        "/absent-build",
                    ],
                    env=os.environ | {"DE_NATIVE_TEST_JOBS": value},
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertNotIn("FileNotFoundError", result.stderr)

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

    def test_weighted_architecture_case_runs_under_bounded_schedule(self) -> None:
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
            for jobs in (1, 2):
                result = subprocess.run(
                    [
                        str(ctest),
                        "--test-dir",
                        str(build),
                        "--parallel",
                        str(jobs),
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

    def test_two_slot_contracts_prevent_measured_process_overlap(self) -> None:
        """Real CTest isolates a weighted contract; removing that weight must cause failure."""
        cmake, ctest = shutil.which("cmake"), shutil.which("ctest")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ctest)
        with tempfile.TemporaryDirectory(prefix="native-exclusive-control-") as directory:
            root = Path(directory)
            child = root / "exclusive.py"
            child.write_text(
                "import pathlib, sys, threading\n"
                "active = pathlib.Path(sys.argv[1])\n"
                "try:\n    active.mkdir()\n"
                "except FileExistsError:\n    print('overlap detected'); sys.exit(1)\n"
                "try:\n    threading.Event().wait(1)\n"
                "finally:\n    active.rmdir()\n"
            )
            for weight, success in ((2, True), (1, False)):
                source = root / f"source-{weight}"
                source.mkdir()
                (source / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 4.4)\nproject(Exclusive NONE)\n"
                    "enable_testing()\n"
                    f'add_test(NAME cli-contract COMMAND "{Path(sys.executable).as_posix()}" '
                    f'"{child.as_posix()}" "{(root / "active").as_posix()}")\n'
                    f'add_test(NAME ordinary COMMAND "{Path(sys.executable).as_posix()}" '
                    f'"{child.as_posix()}" "{(root / "active").as_posix()}")\n'
                    f"set_tests_properties(cli-contract PROPERTIES PROCESSORS {weight})\n"
                    "set_tests_properties(cli-contract ordinary PROPERTIES TIMEOUT 5)\n"
                )
                build = root / f"build-{weight}"
                subprocess.run(
                    [str(cmake), "-S", str(source), "-B", str(build), "-G", "Ninja"],
                    capture_output=True,
                    check=True,
                    timeout=30,
                )
                result = subprocess.run(
                    [
                        str(ctest),
                        "--test-dir",
                        str(build),
                        "--parallel",
                        "2",
                        "--output-on-failure",
                        "--output-junit",
                        str(root / "weighted.xml"),
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=15,
                )
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                if success:
                    complete_junit(root / "weighted.xml", {"cli-contract", "ordinary"})
                else:
                    self.assertIn("overlap detected", result.stdout)

    def test_ctest_refuses_blocked_child_with_its_case_deadline(self) -> None:
        """An entered, blocked child times out instead of becoming passing evidence."""
        cmake = shutil.which("cmake")
        ctest = shutil.which("ctest")
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(ctest)
        with tempfile.TemporaryDirectory(prefix="native-timeout-control-") as directory:
            root = Path(directory)
            marker = root / "entered"
            child = root / "blocked.py"
            child.write_text(
                "import pathlib, sys, threading\n"
                "pathlib.Path(sys.argv[1]).write_text('entered')\n"
                "threading.Event().wait()\n"
            )
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 4.4)\n"
                "project(TimeoutControl NONE)\nenable_testing()\n"
                f'add_test(NAME blocked COMMAND "{Path(sys.executable).as_posix()}" '
                f'"{child.as_posix()}" "{marker.as_posix()}")\n'
                "set_tests_properties(blocked PROPERTIES TIMEOUT 5)\n"
            )
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
                    "--output-on-failure",
                    "--output-junit",
                    str(root / "result.xml"),
                    "--no-tests=error",
                ],
                capture_output=True,
                text=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(marker.read_text(), "entered")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Timeout", result.stdout)
            with self.assertRaisesRegex(ValueError, "did not pass"):
                complete_junit(root / "result.xml", {"blocked"})
