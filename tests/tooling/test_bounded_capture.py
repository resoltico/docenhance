# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bounded native output capture retains diagnostic head and tail."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import capture_reader
import process_execution


class BoundedLogTests(unittest.TestCase):
    """Capture limits keep failure diagnostics while discarding repetitive progress."""

    def test_final_monitor_observes_bytes_written_before_normal_exit(self) -> None:
        """A short engine cannot evade disk admission between periodic polls."""
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "engine.log"
            checks = 0
            expected = b"x" * 100

            def refuse_final() -> None:
                nonlocal checks
                checks += 1
                if checks > 1 and log.stat().st_size >= len(expected):
                    message = "final disk threshold exceeded"
                    raise ValueError(message)

            command = [sys.executable, "-c", "import os; os.write(1, b'x' * 100)"]
            with self.assertRaisesRegex(ValueError, "final disk threshold exceeded"):
                process_execution.bounded_capture(
                    command,
                    dict(os.environ),
                    log,
                    5,
                    process_execution.CapturePolicy(1024, refuse_final),
                )
            self.assertEqual(log.read_bytes(), expected)

    def test_head_tail_survive_without_unbounded_log_growth(self) -> None:
        """The first and last bytes survive a noisy completed subprocess."""
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "engine.log"
            command = [
                sys.executable,
                "-c",
                "import os; os.write(1, b'HEAD' + b'x' * 200000 + b'TAIL')",
            ]
            result = process_execution.bounded_capture(
                command,
                dict(os.environ),
                log,
                5,
                process_execution.CapturePolicy(1024, lambda: None),
            )
            captured = log.read_bytes()
            self.assertEqual(result, 0)
            self.assertTrue(captured.startswith(b"HEAD"))
            self.assertTrue(captured.endswith(b"TAIL"))
            self.assertIn(b"engine output omitted: 198984 bytes", captured)
            self.assertLess(len(captured), 1100)

    def test_monitor_refusal_stops_owned_process_and_preserves_log_tail(self) -> None:
        """The disk monitor stops the child while retaining its last output."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log = root / "engine.log"
            command = [
                sys.executable,
                "-c",
                "import os, time; os.write(1, b'finding-tail'); time.sleep(5)",
            ]

            def refuse() -> None:
                if log.exists() and log.stat().st_size:
                    message = "disk threshold exceeded"
                    raise ValueError(message)

            with self.assertRaisesRegex(ValueError, "disk threshold exceeded"):
                process_execution.bounded_capture(
                    command,
                    dict(os.environ),
                    log,
                    6,
                    process_execution.CapturePolicy(1024, refuse, 0.02),
                )
            self.assertEqual(log.read_bytes(), b"finding-tail")

    def test_log_writer_failure_stops_engine_without_pipe_deadlock(self) -> None:
        """A failed capture drains until the supervisor kills and joins the child."""
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "engine.log"
            command = [sys.executable, "-c", "import os, time; os.write(1, b'x'); time.sleep(5)"]
            with (
                patch.object(
                    capture_reader,
                    "_capture_bounded",
                    side_effect=OSError("synthetic log write failure"),
                ),
                self.assertRaisesRegex(OSError, "Engine output capture failed"),
            ):
                process_execution.bounded_capture(
                    command,
                    dict(os.environ),
                    log,
                    6,
                    process_execution.CapturePolicy(1024, lambda: None, 0.02),
                )
