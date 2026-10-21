# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reader startup, joined ownership and first-error identity at real capture boundaries."""

from __future__ import annotations

import io
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, override
from unittest.mock import patch

from tools_path import ROOT

import capture_reader
import process_execution

if TYPE_CHECKING:
    from collections.abc import Callable
    from typing import IO, Never


class CaptureAbort(BaseException):
    """A nonstandard false-valued control still retains its identity."""

    def __bool__(self) -> bool:
        """Exception selection must use identity, never truthiness."""
        return False


class CaptureReaderTests(unittest.TestCase):
    """Close only quiescent borrows, preserve primary failures and refuse unknown cleanup."""

    @override
    def setUp(self) -> None:
        """Each test owns two finite memory streams and their final teardown."""
        self.source = io.BytesIO()
        self.sink = io.BytesIO()
        self.addCleanup(self.source.close)
        self.addCleanup(self.sink.close)

    @staticmethod
    def stopped_reader() -> threading.Thread:
        """Return a genuinely started, joined reader without timing-based synchronization."""
        reader = threading.Thread(target=lambda: None)
        reader.start()
        reader.join(timeout=5)
        if reader.is_alive():
            message = "Test-owned reader did not finish"
            raise RuntimeError(message)
        return reader

    @staticmethod
    def raise_error(error: BaseException) -> Never:
        """Enter an actual error handler without relying on a platform fault or sleep."""
        raise error

    def test_healthy_join_closes_both_borrows(self) -> None:
        """Normal return attests completed join and both actual stream closes."""
        reader = self.stopped_reader()
        capture_reader.finish_capture_reader(reader, self.source, self.sink, 0)
        self.assertTrue(self.source.closed)
        self.assertTrue(self.sink.closed)
        self.assertFalse(reader.is_alive())

    def test_join_failure_closes_quiescent_borrows_and_keeps_identity(self) -> None:
        """A failed join never reports healthy cleanup even when quiescence permits closing."""
        reader = self.stopped_reader()
        original = CaptureAbort("join")
        with (
            patch.object(reader, "join", side_effect=original),
            self.assertRaises(CaptureAbort) as caught,
        ):
            capture_reader.finish_capture_reader(reader, self.source, self.sink, 0)
        self.assertIs(caught.exception, original)
        self.assertTrue(self.source.closed)
        self.assertTrue(self.sink.closed)

    def test_live_reader_retains_both_stream_borrows(self) -> None:
        """A zero-time join cannot close a stream still borrowed by a known live thread."""
        ready = threading.Event()
        release = threading.Event()

        def work() -> None:
            ready.set()
            release.wait()

        reader = threading.Thread(target=work)
        reader.start()
        try:
            self.assertTrue(ready.wait(timeout=5))
            with self.assertRaisesRegex(RuntimeError, "did not finish"):
                capture_reader.finish_capture_reader(reader, self.source, self.sink, 0)
            self.assertFalse(self.source.closed)
            self.assertFalse(self.sink.closed)
        finally:
            release.set()
            reader.join(timeout=5)
        self.assertFalse(reader.is_alive())

    def test_close_failures_keep_first_error_and_attempt_both_once(self) -> None:
        """Two close errors preserve the first, including a false-valued BaseException."""
        first = CaptureAbort("pipe close")
        second = OSError("sink close")
        with (
            patch.object(self.source, "close", side_effect=first) as source_close,
            patch.object(self.sink, "close", side_effect=second) as sink_close,
            self.assertRaises(CaptureAbort) as caught,
        ):
            capture_reader.finish_capture_reader(None, self.source, self.sink, 0)
        self.assertIs(caught.exception, first)
        self.assertIn("sink close", "\n".join(first.__notes__))
        source_close.assert_called_once_with()
        sink_close.assert_called_once_with()

    def test_body_error_survives_join_and_close_errors(self) -> None:
        """Cleanup preserves every primary exception class and records all secondary errors."""
        reader = self.stopped_reader()
        for original in (
            RuntimeError("body"),
            KeyboardInterrupt(),
            SystemExit(19),
            CaptureAbort("body"),
        ):
            with (
                self.subTest(kind=type(original)),
                patch.object(reader, "join", side_effect=OSError("join")),
                patch.object(self.source, "close", side_effect=OSError("pipe")) as source_close,
                patch.object(self.sink, "close", side_effect=OSError("sink")) as sink_close,
                self.assertRaises(type(original)) as caught,
            ):
                capture_reader.finish_capture_reader(reader, self.source, self.sink, 0, original)
            self.assertIs(caught.exception, original)
            notes = "\n".join(original.__notes__)
            for text in ("join", "pipe", "sink"):
                self.assertIn(text, notes)
            source_close.assert_called_once_with()
            sink_close.assert_called_once_with()

    def test_ambient_handler_is_not_a_capture_failure(self) -> None:
        """A healthy operation inside an existing handler retains no unrelated error."""
        try:
            self.raise_error(ValueError("ambient"))
        except ValueError:
            self.assertIsNone(capture_reader.capture_failure(None, complete=True))
            capture_reader.finish_capture_reader(None, self.source, self.sink, 0)
        self.assertTrue(self.source.closed)
        self.assertTrue(self.sink.closed)

    def test_owned_cleanup_error_is_secondary_to_body(self) -> None:
        """Owned-process teardown cannot replace the already observed original body error."""
        original = CaptureAbort("body")
        try:
            self.raise_error(OSError("owned cleanup"))
        except OSError:
            result = capture_reader.capture_failure(original, complete=False)
        self.assertIs(result, original)
        self.assertIn("owned cleanup", "\n".join(original.__notes__))

    def failed_start(
        self, original: BaseException, start: Callable[[threading.Thread], None], *, closed: bool
    ) -> threading.Thread | None:
        """Attain a real capture startup failure and inspect its reader/stream ownership."""
        owners: list[tuple[threading.Thread | None, IO[bytes] | None, IO[bytes] | None]] = []
        finish = capture_reader.finish_capture_reader

        def record_finish(
            reader: threading.Thread | None,
            source: IO[bytes] | None,
            sink: IO[bytes] | None,
            timeout: float,
            primary: BaseException | None = None,
        ) -> None:
            owners.append((reader, source, sink))
            finish(reader, source, sink, timeout, primary)

        with tempfile.TemporaryDirectory() as directory:
            with (
                patch.object(threading.Thread, "start", start),
                patch.object(capture_reader, "finish_capture_reader", side_effect=record_finish),
                self.assertRaises(type(original)) as caught,
            ):
                process_execution.bounded_capture(
                    [sys.executable, "-B", "-c", "print('capture')"],
                    dict(os.environ),
                    Path(directory) / "capture.log",
                    5,
                    process_execution.CapturePolicy(1024, lambda: None, interval=0.05),
                )
            self.assertIs(caught.exception, original)
            self.assertEqual(len(owners), 1)
            reader, source, sink = owners[0]
            try:
                self.assertIsNotNone(source)
                self.assertIsNotNone(sink)
                self.assertEqual(source.closed if source is not None else None, closed)
                self.assertEqual(sink.closed if sink is not None else None, closed)
            finally:
                # Only the injected test start establishes when no native worker was created.
                if reader is not None and reader.ident is not None:
                    reader.join(timeout=5)
                if reader is None or reader.ident is None or not reader.is_alive():
                    if source is not None:
                        source.close()
                    if sink is not None:
                        sink.close()
            return reader

    def test_before_start_failure_is_explicitly_unconfirmed(self) -> None:
        """A failed start without a public identity cannot justify a join or stream close."""
        original = CaptureAbort("before start")

        def before_start(_reader: threading.Thread) -> None:
            raise original

        reader = self.failed_start(original, before_start, closed=False)
        self.assertIsNotNone(reader)
        self.assertIsNone(reader.ident if reader is not None else None)
        self.assertIn("unconfirmed", "\n".join(original.__notes__))

    def test_start_then_raise_still_joins_and_closes(self) -> None:
        """A raised startup call can still have created a real reader that must be joined."""
        original = CaptureAbort("after start")
        actual_start = threading.Thread.start

        def after_start(reader: threading.Thread) -> None:
            actual_start(reader)
            raise original

        reader = self.failed_start(original, after_start, closed=True)
        self.assertIsNotNone(reader)
        self.assertFalse(reader.is_alive() if reader is not None else True)

    def test_healthy_real_capture_inside_ambient_handler(self) -> None:
        """The public API captures actual child output and environment without ambient failure."""
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "capture.log"
            try:
                self.raise_error(ValueError("ambient"))
            except ValueError:
                status = process_execution.bounded_capture(
                    [
                        sys.executable,
                        "-B",
                        "-c",
                        "import os; print(os.environ['CAPTURE_TEST_ROOT'])",
                    ],
                    {**os.environ, "CAPTURE_TEST_ROOT": str(ROOT)},
                    log,
                    5,
                    process_execution.CapturePolicy(1024, lambda: None, interval=0.05),
                )
            self.assertEqual(status, 0)
            self.assertEqual(log.read_text(), str(ROOT) + "\n")


if __name__ == "__main__":
    unittest.main()
