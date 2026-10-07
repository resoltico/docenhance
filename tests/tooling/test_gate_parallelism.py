# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real worker execution preserves reference linking and strict tooling evidence."""

from __future__ import annotations

import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from contextlib import suppress
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_reference_suite
import parallel


class ReferenceCompilationTests(unittest.TestCase):
    """Unique objects retain complete input/flags and actual compiler failures."""

    def test_serial_and_parallel_compile_link_the_same_complete_sources(self) -> None:
        """Compile two same-basename sources; omitting either makes actual linking fail."""
        compiler = shutil.which("clang++" if sys.platform == "win32" else "c++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            sources = []
            for name, body in (
                ("left", "int answer(); int main() { return answer() != 42; }"),
                ("right", "int answer() { return REFERENCE_EXPECTED; }"),
            ):
                target = directory / name / "source.cpp"
                target.parent.mkdir()
                target.write_text(body)
                sources.append(target)
            for jobs in (1, 2):
                build = directory / f"build-{jobs}"
                build.mkdir()
                binary = check_reference_suite.build_reference(
                    str(compiler), ["-DREFERENCE_EXPECTED=42"], sources, build, jobs
                )
                subprocess.run([str(binary)], check=True, timeout=10)
                self.assertEqual(len(list(build.glob("*.o"))), len(sources))
            sources[1].write_text("#error controlled-reference-failure\n")
            broken = directory / "broken"
            broken.mkdir()
            with self.assertRaises(subprocess.CalledProcessError):
                check_reference_suite.build_reference(str(compiler), [], sources, broken, 2)
            self.assertFalse(list(broken.glob("reference-tests*")))

    def test_compilation_really_overlaps_before_single_complete_link(self) -> None:
        """An entered worker waits for its peer; serial dispatch cannot release the barrier."""
        meeting = threading.Barrier(2)
        commands: list[list[str]] = []

        def execute(command: list[str], *, check: bool) -> None:
            self.assertTrue(check)
            commands.append(command)
            if "-c" in command:
                meeting.wait(timeout=10)

        with (
            tempfile.TemporaryDirectory() as temporary,
            patch("check_reference_suite.subprocess.run", side_effect=execute),
        ):
            directory = Path(temporary)
            check_reference_suite.build_reference(
                "compiler",
                ["-fno-fast-math"],
                [Path("one.cpp"), Path("two.cpp")],
                directory,
                2,
            )
        self.assertEqual(len(commands), 3)
        self.assertNotIn("-c", commands[-1])
        self.assertEqual(sum(argument.endswith(".o") for argument in commands[-1]), 2)
        self.assertTrue(all("-fno-fast-math" in command for command in commands))

    def test_invalid_jobs_are_refused_before_compilation(self) -> None:
        """Bad worker requests cannot start compiler or CMake subprocesses."""
        for jobs in (0, parallel.MAX_JOBS + 1):
            result = subprocess.run(
                [sys.executable, str(ROOT / "tools/check_reference_suite.py"), "--jobs", str(jobs)],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("reference jobs must be", result.stderr)


class ToolingWorkerTests(unittest.TestCase):
    """Fresh child interpreters overlap modules and retain all failure modes."""

    def run_fixture(self, directory: Path, jobs: int = 2) -> subprocess.CompletedProcess[str]:
        """Exercise the real strict entry point and retain every child diagnostic."""
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools/run_tooling_tests.py"),
                "--directory",
                str(directory),
                "--jobs",
                str(jobs),
            ],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )

    def test_actual_isolated_modules_overlap_and_execute_exact_inventory(self) -> None:
        """A blocking loopback handshake releases only after both workers enter; no polling."""
        for jobs, expected in ((2, 0), (1, 1)):
            with self.subTest(jobs=jobs), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                with socket.socket() as listener:
                    listener.bind(("127.0.0.1", 0))
                    listener.listen(2)
                    listener.settimeout(15)
                    address = listener.getsockname()
                    entered: list[socket.socket] = []

                    def release(peers: list[socket.socket] = entered) -> None:
                        for _ in range(2):
                            connection, _address = listener.accept()
                            peers.append(connection)
                        for connection in peers:
                            with connection, suppress(OSError):
                                connection.sendall(b"x")

                    coordinator = threading.Thread(target=release, daemon=True)
                    coordinator.start()
                    body = (
                        "import unittest, socket\nclass Case(unittest.TestCase):\n"
                        " def test_enter(self):\n"
                        f"  with socket.create_connection({address!r}, timeout=5) as peer:\n"
                        "   self.assertEqual(peer.recv(1), b'x')\n"
                    )
                    for name in ("first", "second"):
                        (directory / f"test_{name}.py").write_text(body)
                    result = self.run_fixture(directory, jobs)
                    coordinator.join(timeout=20)
                    self.assertFalse(coordinator.is_alive())
                    self.assertEqual(len(entered), 2)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                self.assertIn(f"2 cases in 2 modules; workers={jobs}", result.stdout)
                self.assertLess(
                    result.stdout.index("module test_first"),
                    result.stdout.index("module test_second"),
                )

    def test_consecutive_modules_cannot_share_interpreter_state(self) -> None:
        """Even one worker replaces its interpreter after a module mutates a shared builtin."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "test_first.py").write_text(
                "import unittest, builtins\nclass Case(unittest.TestCase):\n"
                " def test_set(self): builtins.controlled_worker_state = 42\n"
            )
            (directory / "test_second.py").write_text(
                "import unittest, builtins\nclass Case(unittest.TestCase):\n"
                " def test_absent(self):\n"
                "  self.assertFalse(hasattr(builtins, 'controlled_worker_state'))\n"
            )
            result = self.run_fixture(directory, jobs=1)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("2 cases in 2 modules; workers=1", result.stdout)

    def test_import_failure_skip_failure_and_unexpected_success_are_rejected(self) -> None:
        """Exit status and exact worker outcomes reject incomplete or decorative success."""
        bodies = (
            "raise RuntimeError('controlled-import-error')\n",
            (
                "import unittest\nclass Case(unittest.TestCase):\n"
                " @unittest.expectedFailure\n def test_case(self): self.fail('controlled')\n"
            ),
            (
                "import unittest\nclass Case(unittest.TestCase):\n"
                " @unittest.skip('controlled')\n def test_case(self): pass\n"
            ),
            (
                "import unittest\nclass Case(unittest.TestCase):\n"
                " def test_case(self): self.fail('controlled')\n"
            ),
            (
                "import unittest\nclass Case(unittest.TestCase):\n"
                " @unittest.expectedFailure\n def test_case(self): pass\n"
            ),
            "",
        )
        for body in bodies:
            with self.subTest(body=body), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                (directory / "test_probe.py").write_text(body)
                self.assertNotEqual(self.run_fixture(directory).returncode, 0)

    def test_invalid_jobs_are_refused_before_discovery(self) -> None:
        """No build or test work starts on a wider process request."""
        result = self.run_fixture(Path("/absent-directory"), jobs=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tooling jobs must be in [1,2]", result.stderr)


if __name__ == "__main__":
    unittest.main()
