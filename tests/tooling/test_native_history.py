# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real sparse extents and owned child failures protect finite native history admission."""

from __future__ import annotations

import contextlib
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from typing import TYPE_CHECKING
from unittest import mock

if TYPE_CHECKING:
    from collections.abc import Iterator

from tools_path import prepare_tool_imports

import native_history
import process_execution
import run_native_suite
import run_tooling_tests
from cache_lock import exclusive_cache
from native_output import RAW_LIMIT

prepare_tool_imports()


class NativeHistoryTests(unittest.TestCase):
    """History guards refuse retained growth without deleting a source or prior result."""

    def history(self, build: Path) -> native_history.NativeHistory:
        """Give each tiny control one original finite five-second deadline."""
        return native_history.NativeHistory(build, time.monotonic() + 5)

    def sparse(self, path: Path, size: int) -> None:
        """Use actual logical file extents without allocating hundreds of MiB of payload."""
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("wb") as stream:
            stream.truncate(size)

    def test_healthy_history_counts_sparse_hardlinks_once_and_keeps_raw(self) -> None:
        """One raw report and its hardlink remain present while inode bytes deduplicate."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            raw = build / "native-prior/ctest.xml"
            self.sparse(raw, RAW_LIMIT)
            os.link(raw, raw.with_name("copy.xml"))
            self.sparse(build / "Testing/Temporary/LastTest.log", 7)
            (build / "native-tests.json").write_text("{}")
            history = self.history(build)
            history.admit()
            self.assertEqual(history.peak_bytes, RAW_LIMIT + 7)
            self.assertTrue(raw.is_file())
            self.assertGreaterEqual(history.peak_entries, 7)

    def test_sparse_existing_full_history_refuses_before_owner_or_child(self) -> None:
        """Actual sparse st_size above the new-run reservation refuses without disposal."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            previous = build / "native-prior/ctest.xml"
            self.sparse(
                previous, native_history.HISTORY_BYTES - native_history.RUN_RESERVE_BYTES + 1
            )
            with (
                mock.patch.object(run_native_suite, "discovery") as child,
                self.assertRaisesRegex(ValueError, "preserve/dispose"),
            ):
                run_native_suite.run(
                    type("Args", (), {"build": build, "ctest": "ctest", "jobs": 1})()
                )
            child.assert_not_called()
            self.assertEqual(list(build.glob("native-*")), [previous.parent])
            self.assertTrue(previous.is_file())
            self.assertFalse((build / ".native-history-active").exists())

    def test_duplicate_inode_names_cannot_bypass_entry_reservation(self) -> None:
        """Even zero-byte hardlinks consume individual traversed entry capacity."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            raw = build / "native-prior/ctest.xml"
            self.sparse(raw, 0)
            for index in range(native_history.HISTORY_ENTRIES):
                os.link(raw, raw.with_name(str(index)))
            with self.assertRaisesRegex(ValueError, "4096"):
                self.history(build).admit()

    def test_symlink_candidate_refuses_without_following_or_deleting_target(self) -> None:
        """A redirected history owner refuses and a foreign sentinel remains intact."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            foreign = root / "foreign"
            foreign.mkdir()
            sentinel = foreign / "sentinel"
            sentinel.write_text("unchanged")
            if os.name == "nt":
                subprocess.run(
                    [
                        str(shutil.which("cmd")),
                        "/c",
                        "mklink",
                        "/J",
                        str(build / "native-link"),
                        str(foreign),
                    ],
                    check=True,
                    capture_output=True,
                    timeout=5,
                )
            else:
                (build / "native-link").symlink_to(foreign, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "redirected"):
                self.history(build).admit()
            self.assertEqual(sentinel.read_text(), "unchanged")

    def test_existing_oversized_testing_output_refuses_before_context_entry(self) -> None:
        """Admission enforces retained raw and metadata limits before any child can start."""
        for name, size, rule in (
            ("LastTest.log.tmpbb9f0", RAW_LIMIT + 1, "raw JUnit/journal exceeds"),
            ("unknown-metadata", native_history.METADATA_BYTES + 1, "metadata exceeds"),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                build = Path(temporary)
                path = build / "Testing/Temporary" / name
                self.sparse(path, size)
                with (
                    self.assertRaisesRegex(ValueError, rule),
                    native_history.owned_native_history(build, time.monotonic() + 5),
                ):
                    self.fail("oversized retained output reached the child admission boundary")
                self.assertTrue(path.exists())
                self.assertFalse((build / ".native-history-active").exists())

    def test_actual_generated_journal_and_fixed_junit_temporary_raw_bounds(self) -> None:
        """Known writer names gain raw admission; close-looking names remain small metadata."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            directory = build / "native-current"
            directory.mkdir()
            raw = build / "Testing/Temporary/LastTest.log.tmpbb9f0"
            self.sparse(raw, native_history.METADATA_BYTES + 1)
            xml = directory / "ctest.xml.tmp"
            self.sparse(xml, native_history.METADATA_BYTES + 1)
            history = self.history(build)
            history.directory = directory
            history.check()
            for path in (raw, xml):
                with self.subTest(path=path):
                    self.sparse(path, RAW_LIMIT + 1)
                    with self.assertRaisesRegex(ValueError, "raw JUnit/journal exceeds"):
                        history.check()
                    self.sparse(path, native_history.METADATA_BYTES + 1)
            raw.rename(raw.with_name("LastTest.log.tmpABCDE"))
            with self.assertRaisesRegex(ValueError, "metadata exceeds"):
                history.check()

    def test_special_or_redirected_nested_entry_refuses(self) -> None:
        """A real FIFO or Windows junction refuses inside an otherwise regular owner."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "native-prior").mkdir()
            if os.name == "nt":
                foreign = build / "foreign"
                foreign.mkdir()
                subprocess.run(
                    [
                        str(shutil.which("cmd")),
                        "/c",
                        "mklink",
                        "/J",
                        str(build / "native-prior/redirected"),
                        str(foreign),
                    ],
                    check=True,
                    capture_output=True,
                    timeout=5,
                )
            else:
                os.mkfifo(build / "native-prior/pipe")
            with self.assertRaisesRegex(ValueError, "redirected|special"):
                self.history(build).admit()

    def test_external_current_preflight_is_created_after_admission_and_guarded(self) -> None:
        """The concrete FFT preflight outside native-* shares admission and its raw bound."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            external = root / "release-preflight"
            with (
                self.assertRaisesRegex(ValueError, "raw JUnit/journal exceeds64MiB"),
                native_history.owned_native_history(
                    build, time.monotonic() + 5, external
                ) as history,
            ):
                self.assertTrue(external.is_dir())
                self.sparse(external / "ctest.xml", RAW_LIMIT + 1)
                history.check()
            self.assertTrue((external / "ctest.xml").exists())
            self.assertFalse((build / ".native-history-active").exists())

    def test_retained_preflight_stays_counted_by_later_full_release(self) -> None:
        """The fixed external FFT owner remains in the same aggregate after its scope closes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "out/release/app"
            build.mkdir(parents=True)
            preflight = root / "out/windows-resource-proof/release-preflight/ctest.xml"
            self.sparse(preflight, 12345)
            history = self.history(build)
            history.admit()
            self.assertEqual(history.peak_bytes, 12345)

    def test_nested_evidence_directory_refuses_before_recursion(self) -> None:
        """Current producers have flat owners, so arbitrary nested trees are not admitted."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "native-prior/nested").mkdir(parents=True)
            with self.assertRaisesRegex(ValueError, "must be flat"):
                self.history(build).admit()

    def test_existing_writer_refuses_without_another_owner(self) -> None:
        """The original cache-lock protocol serializes admission through qualification."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            with (
                exclusive_cache(build / ".native-history-active"),
                self.assertRaisesRegex(RuntimeError, "already owns"),
            ):
                run_native_suite.run(
                    type("Args", (), {"build": build, "ctest": "ctest", "jobs": 1})()
                )
            self.assertEqual(list(build.glob("native-*")), [])

    def test_growth_primary_survives_archive_failure(self) -> None:
        """The guard's actual primary object survives unsuccessful diagnostic retention."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            (build / "Testing/Temporary").mkdir(parents=True)
            primary = ValueError("native growth guard refusal")
            with (
                mock.patch.object(run_native_suite, "discovery", return_value=[{"name": "unit"}]),
                mock.patch.object(run_native_suite, "compilation"),
                mock.patch.object(run_native_suite, "registrations", return_value={"unit"}),
                mock.patch.object(native_history.NativeHistory, "check", side_effect=primary),
                mock.patch.object(
                    run_native_suite, "archive_ctest_output", side_effect=OSError("archive refused")
                ),
                self.assertRaises(ValueError) as caught,
            ):
                run_native_suite.run(
                    type("Args", (), {"build": build, "ctest": "fixture", "jobs": 1})()
                )
            self.assertIs(caught.exception, primary)
            self.assertIn("Diagnostic archive", " ".join(primary.__notes__))
            self.assertFalse((build / ".native-history-active").exists())

    def test_fast_child_final_monitor_cannot_miss_new_extent(self) -> None:
        """A final sample runs even when the first actual wait already reports completion."""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            history = self.history(build)
            history.admit()
            history.directory = build / "native-current"
            history.directory.mkdir()
            target = history.directory / "ctest.xml"
            closed = []

            test = self

            class FastChild:
                def wait(self, *, timeout: float) -> int:
                    test.assertGreater(timeout, 0)
                    test.sparse(target, RAW_LIMIT + 1)
                    return 0

            @contextlib.contextmanager
            def owner(*_args: object, **_kwargs: object) -> Iterator[FastChild]:
                try:
                    yield FastChild()
                finally:
                    closed.append(True)

            with (
                mock.patch.object(process_execution, "owned_process", side_effect=owner),
                self.assertRaisesRegex(ValueError, "raw JUnit/journal exceeds64MiB"),
            ):
                process_execution.monitored_process(["fixture"], {}, 5, history.check)
            self.assertEqual(closed, [True])

    def test_actual_growth_refusal_reaps_owned_waiting_child(self) -> None:
        """A live child creates a sparse oversized report then waits; refusal joins its owner."""
        with tempfile.TemporaryDirectory() as temporary, socket.socket() as server:
            build = Path(temporary)
            history = self.history(build)
            history.admit()
            history.directory = build / "native-current"
            history.directory.mkdir()
            pid = build / "pid"
            target = history.directory / "ctest.xml"
            server.bind(("127.0.0.1", 0))
            server.listen()
            script = (
                "import os,pathlib,socket,sys;"
                "pathlib.Path(sys.argv[1]).write_text(str(os.getpid()));"
                "f=open(sys.argv[2],'wb');f.truncate(int(sys.argv[3]));f.close();"
                "s=socket.create_connection(('127.0.0.1',int(sys.argv[4])),timeout=5);"
                "s.settimeout(None);s.recv(1)"
            )
            observed: list[subprocess.Popen[bytes]] = []
            original_owner = process_execution.owned_process

            @contextlib.contextmanager
            def retain_process(
                command: list[str], env: dict[str, str], output: None, *, graceful: bool
            ) -> Iterator[subprocess.Popen[bytes]]:
                with original_owner(command, env, output, graceful=graceful) as process:
                    observed.append(process)
                    yield process

            with (
                mock.patch.object(process_execution, "owned_process", side_effect=retain_process),
                self.assertRaisesRegex(ValueError, "raw JUnit/journal exceeds64MiB"),
            ):
                process_execution.monitored_process(
                    [
                        sys.executable,
                        "-B",
                        "-c",
                        script,
                        str(pid),
                        str(target),
                        str(RAW_LIMIT + 1),
                        str(server.getsockname()[1]),
                    ],
                    dict(os.environ),
                    5,
                    history.check,
                    graceful=True,
                )
            self.assertEqual(len(observed), 1)
            process = observed[0]
            self.assertEqual(process.pid, int(pid.read_text()))
            self.assertIsNotNone(process.returncode)
            self.assertNotEqual(process.returncode, 0)
            self.assertEqual(process.poll(), process.returncode)
            self.assertEqual(process.wait(timeout=0), process.returncode)
            if os.name == "posix":
                with self.assertRaises(ProcessLookupError):
                    os.kill(process.pid, 0)
            self.assertTrue(target.exists())

    def test_strict_runner_refuses_deliberately_skipped_child_fixture(self) -> None:
        """The real strict runner rejects a skip; this outer control itself executes on every OS."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            name = "test_history_skip_negative"
            (directory / (name + ".py")).write_text(
                "import unittest\n"
                "class SkipNegative(unittest.TestCase):\n"
                "    @unittest.skip('deliberate strict-runner negative')\n"
                "    def test_omitted(self): pass\n"
            )
            identifiers, passed, output, _elapsed = run_tooling_tests.module_run((directory, name))
            self.assertEqual(len(identifiers), 1)
            self.assertFalse(passed)
            self.assertIn("skipped", output)
