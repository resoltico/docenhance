# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Failed real child commands cannot masquerade as small successful resource envelopes."""

from __future__ import annotations

import contextlib
import io
import json
import os
import platform
import selectors
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING
from unittest.mock import Mock, patch

import tools_path

import measure_pipeline_resources as measurement
import process_execution
from measure_pipeline_resources import main, observe

if TYPE_CHECKING:
    from resource import struct_rusage


class PipelineResourceTests(unittest.TestCase):
    """Exercise the actual subprocess boundary and controlled prerequisite refusal."""

    def test_refused_or_unknown_publication_cannot_emit_resource_evidence(self) -> None:
        """Matching error exit codes and successful unknown publication both refuse measurement."""
        self.assertTrue(tools_path.ROOT.is_dir())
        if os.name != "posix":
            with (
                patch.object(sys, "argv", ["measure", "--executable", "binary", "--out", "report"]),
                self.assertRaises(SystemExit) as error,
            ):
                main()
            self.assertEqual(error.exception.code, 2)
            return
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "command"
            for index, (code, state) in enumerate(((3, "not_started"), (0, "unknown"))):
                payload = json.dumps({"exit_code": code, "publication": state})
                executable.write_text(
                    f"#!{sys.executable}\nprint({payload!r})\nraise SystemExit({code})\n"
                )
                executable.chmod(0o755)
                with self.assertRaisesRegex(ValueError, "successful completed processing"):
                    observe(executable, root / "source.png", root / str(index), [])

    def test_real_capture_preserves_wait4_values_without_premature_poll(self) -> None:
        """Healthy output keeps the exact direct child's wait4 RSS/CPU values."""
        if os.name != "posix":
            self.assertFalse(hasattr(os, "wait4"))
            return

        observations: list[struct_rusage] = []
        actual_wait = os.wait4
        actual_poll = subprocess.Popen.poll

        def waiting(pid: int, flags: int) -> tuple[int, int, struct_rusage]:
            result = actual_wait(pid, flags)
            if result[0]:
                observations.append(result[2])
            return result

        def polling(child: subprocess.Popen[bytes]) -> int | None:
            self.assertIsNotNone(child.returncode, "Popen must not reap before wait4")
            return actual_poll(child)

        with patch.object(os, "wait4", waiting), patch.object(subprocess.Popen, "poll", polling):
            payload, facts = measurement.capture(
                [sys.executable, "-c", "print('healthy')"], None, 5
            )
        self.assertEqual(payload, b"healthy\n")
        self.assertEqual(len(observations), 1)
        usage = observations[0]
        self.assertEqual(facts["user_cpu_seconds"], usage.ru_utime)
        self.assertEqual(facts["system_cpu_seconds"], usage.ru_stime)
        self.assertEqual(
            facts["peak_rss_bytes"],
            usage.ru_maxrss * (1 if platform.system() == "Darwin" else 1024),
        )
        self.assertGreaterEqual(facts["wall_seconds"], 0)

    def test_noisy_or_stderr_children_stop_before_waiting_for_more_output(self) -> None:
        """Overlarge stdout and tiny stderr both stop children blocked on a signal."""
        if os.name != "posix":
            self.assertFalse(hasattr(os, "wait4"))
            return

        for descriptor, data in ((1, b"x" * 65), (2, b"genuine error")):
            with (
                self.subTest(descriptor=descriptor),
                patch.object(measurement, "CAPTURE_LIMIT", 64),
            ):
                script = f"import os,signal;os.write({descriptor},{data!r});signal.pause()"
                with self.assertRaisesRegex(ValueError, "overlarge or failed delivery"):
                    measurement.capture([sys.executable, "-c", script], None, 2)

    def test_timeout_and_descendant_pipe_cleanup_use_owned_group(self) -> None:
        """Native deadlines stop blocked children, including descendants after parent exit."""
        if os.name != "posix":
            self.assertFalse(hasattr(os, "wait4"))
            return

        with self.assertRaises(TimeoutError):
            measurement.capture([sys.executable, "-c", "import signal;signal.pause()"], None, 0.1)
        for redirected in (False, True):
            with self.subTest(redirected=redirected):
                script = (
                    "import subprocess,sys;subprocess.Popen([sys.executable,'-c',"
                    "'import signal;signal.pause()']"
                    + (",stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL" if redirected else "")
                    + ");print('parent-complete')"
                )
                with patch.object(
                    process_execution, "kill_group", wraps=process_execution.kill_group
                ) as kill:
                    payload, _ = measurement.capture([sys.executable, "-c", script], None, 2)
                self.assertEqual(payload, b"parent-complete\n")
                self.assertEqual(kill.call_count, 1)

    def test_scratch_redirect_entry_and_byte_limits_refuse(self) -> None:
        """Observed redirection refuses; ordinary entries and bytes have finite sampled limits."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            owned = root / "owned"
            owned.mkdir()
            foreign = root / "foreign"
            foreign.write_bytes(b"outside")
            link = owned / "redirect"
            link.symlink_to(foreign)
            with self.assertRaisesRegex(ValueError, "redirected or special"):
                measurement.disk_bytes(owned)
            link.unlink()
            (owned / "large").write_bytes(b"x" * 65)
            with patch.object(measurement, "CASE_DISK_LIMIT", 64), self.assertRaises(ValueError):
                measurement.disk_bytes(owned)
            with patch.object(measurement, "CASE_ENTRY_LIMIT", 1), self.assertRaises(ValueError):
                measurement.disk_bytes(owned)
            self.assertEqual(foreign.read_bytes(), b"outside")

    def test_version_uses_finite_capture_before_fixture_creation(self) -> None:
        """Version discovery cannot bypass the same response/time bound as processing."""
        if os.name != "posix":
            self.assertFalse(hasattr(os, "wait4"))
            return

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            argv = ["measure", "--executable", str(root / "binary"), "--out", str(root / "report")]
            with (
                patch.object(sys, "argv", argv),
                patch.object(
                    measurement, "capture", side_effect=TimeoutError("version timeout")
                ) as capture,
                patch.object(measurement.FIXTURES, "Fixture") as fixture,
                self.assertRaisesRegex(TimeoutError, "version timeout"),
            ):
                main()
            capture.assert_called_once_with(
                [str((root / "binary").resolve()), "version", "--json"], None, 600
            )
            fixture.assert_not_called()
            self.assertFalse((root / "report").exists())

    def test_exact_disappeared_darwin_ps_is_the_only_empty_nonzero_exception(self) -> None:
        """A reaped target can vanish before ps without admitting failed native commands."""
        with (
            patch.object(platform, "system", return_value="Darwin"),
            patch.object(measurement, "capture", return_value=(b"", {})) as capture,
        ):
            self.assertEqual(measurement.threads(123, float("inf")), 0)
        self.assertEqual(capture.call_args.args[0], ["/bin/ps", "-M", "-p", "123", "-o", "pid="])
        measurement.require_command_success(capture.call_args.args[0], 1, bytearray())
        for command, code, payload in (
            (["binary", "process"], 1, b""),
            (["binary", "version"], 1, b""),
            (["/bin/ps", "-M", "-p", "123", "-o", "pid="], 2, b""),
            (["/bin/ps", "-M", "-p", "123", "-o", "pid="], 1, b"unexpected"),
        ):
            with self.subTest(command=command, code=code), self.assertRaises(ValueError):
                measurement.require_command_success(command, code, bytearray(payload))

    def test_partial_registration_closes_both_pipes_and_cleanup_keeps_first_error(self) -> None:
        """Startup and group cleanup failures never replace the original capture refusal."""
        stdout, stderr = io.BytesIO(), io.BytesIO()
        child = Mock(spec=subprocess.Popen, stdout=stdout, stderr=stderr)
        selector = Mock(spec=selectors.BaseSelector)
        original = ValueError("capture refusal")
        selector.register.side_effect = original
        with self.assertRaises(ValueError) as refused, contextlib.ExitStack() as stack:
            measurement.register_pipes(child, stack, selector)
        self.assertIs(refused.exception, original)
        self.assertTrue(stdout.closed)
        self.assertTrue(stderr.closed)
        if os.name != "posix":
            self.assertFalse(hasattr(os, "wait4"))
            return
        actual_kill = process_execution.kill_group

        def cleanup(child: subprocess.Popen[bytes]) -> None:
            actual_kill(child)
            message = "cleanup failure"
            raise OSError(message)

        with (
            patch.object(measurement, "read_ready", side_effect=original),
            patch.object(process_execution, "kill_group", cleanup),
            self.assertRaises(ValueError) as refusal,
        ):
            measurement.capture([sys.executable, "-c", "import signal;signal.pause()"], None, 2)
        self.assertIs(refusal.exception, original)
        self.assertTrue(any("cleanup failure" in note for note in original.__notes__))
