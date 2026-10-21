# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Concrete capture storage/lifetime negatives preserve stdout candidates and live owners."""

from __future__ import annotations

import io
import tempfile
import threading
import unittest
from typing import TYPE_CHECKING, Any, BinaryIO, override
from unittest.mock import Mock, patch

from tools_path import prepare_tool_imports

from quality_accounting_fixture import factual_slots

from quality_attempt import MAX_RESPONSE_BYTES, CaptureAdmission, new_attempt
from quality_capture_stream import CaptureReader, finish_reader

if TYPE_CHECKING:
    from collections.abc import Buffer

prepare_tool_imports()


class ShortDestination(io.BytesIO):
    """An actual partial write returns its exact extent; no invented successful bytes."""

    @override
    def write(self, data: Buffer, /) -> int:
        """Report the exact one-byte-short write made to this real destination."""
        view = memoryview(data).cast("B")
        return super().write(view[: max(0, view.nbytes - 1)])


def observed_bytes(
    raw: bytes, destination: BinaryIO
) -> tuple[CaptureReader, dict[str, Any], CaptureAdmission]:
    """Use a real bounded source descriptor and the reader's public start/join boundary."""
    slot = factual_slots(0, ["selected", "process", "source", "--out-dir", "original", "--json"])[1]
    attempt, admission = new_attempt(slot), CaptureAdmission()
    with tempfile.TemporaryFile(mode="w+b") as pipe:
        pipe.write(raw)
        pipe.seek(0)
        reader = CaptureReader("stdout", pipe, destination)
        reader.start()
        if not reader.join(attempt, admission):
            message = "The bounded fixture reader did not join"
            raise AssertionError(message)
        reader.notice(attempt, admission)
    return reader, attempt, admission


class QualityCaptureStorageTests(unittest.TestCase):
    """Retain failed capture storage and uncertain live owners."""

    def test_short_write_keeps_complete_stdout_candidate_without_raw_success(self) -> None:
        """Short write keeps complete stdout candidate without raw success."""
        raw = b'{"publication":"completed"}'
        destination = ShortDestination()
        reader, attempt, admission = observed_bytes(raw, destination)
        candidate = reader.snapshot(attempt["stdout"])
        self.assertEqual(candidate, raw)
        self.assertEqual(destination.getvalue(), raw[:-1])
        self.assertEqual(reader.retained, len(raw) - 1)
        self.assertEqual(attempt["stdout"]["observed_extent"], "exact")
        self.assertEqual(attempt["stdout"]["observed_prefix_bytes"], len(raw))
        self.assertIsNotNone(attempt["stdout"]["errors"]["write"])
        self.assertEqual(attempt["primary_error"], "stdout.errors.write")
        self.assertFalse(admission.is_open)
        self.assertEqual(attempt["stdout"]["retention_state"], "not_started")

    def test_unconfirmed_reader_keeps_real_destination_and_never_finalizes_it(self) -> None:
        """Unconfirmed reader keeps real destination and never finalizes it."""
        destination = io.BytesIO()
        pipe = io.BytesIO()
        reader = CaptureReader("stdout", pipe, destination)
        reader.started = True
        thread = Mock(spec=threading.Thread)
        thread.is_alive.return_value = True
        reader.thread = thread
        slot = factual_slots(
            0, ["selected", "process", "source", "--out-dir", "original", "--json"]
        )[1]
        attempt, admission = new_attempt(slot), CaptureAdmission()
        with patch("quality_capture_stream.finalize_file") as finalize:
            candidate = finish_reader(reader, attempt, admission)
            finalize.assert_not_called()
        thread.join.assert_called_once_with(timeout=10)
        self.assertFalse(pipe.closed)
        self.assertFalse(destination.closed)
        self.assertIsNone(candidate)
        self.assertIn(reader, admission.retained_live)
        self.assertEqual(attempt["stdout"]["retention_state"], "unavailable")
        self.assertIsNone(attempt["stdout"]["sha256"])
        self.assertIsNone(attempt["stdout"]["closed"])
        self.assertEqual(attempt["primary_error"], "cleanup_errors.join_stdout_reader")
        self.assertFalse(admission.is_open)

    def test_incomplete_prefix_never_becomes_an_exact_complete_response(self) -> None:
        """Incomplete prefix never becomes an exact complete response."""
        exact = io.BytesIO()
        healthy, admitted, _ = observed_bytes(b"x" * MAX_RESPONSE_BYTES, exact)
        self.assertEqual(len(exact.getvalue()), MAX_RESPONSE_BYTES)
        self.assertEqual(healthy.snapshot(admitted["stdout"]), b"x" * MAX_RESPONSE_BYTES)
        destination = io.BytesIO()
        reader, attempt, admission = observed_bytes(
            b"x" * MAX_RESPONSE_BYTES + b"one more actual input chunk", destination
        )
        candidate = reader.snapshot(attempt["stdout"])
        self.assertIsNone(candidate)
        self.assertEqual(len(destination.getvalue()), MAX_RESPONSE_BYTES)
        self.assertEqual(attempt["stdout"]["observed_extent"], "at_least")
        self.assertEqual(attempt["stdout"]["observed_prefix_bytes"], MAX_RESPONSE_BYTES + 27)
        self.assertIsNotNone(attempt["stdout"]["errors"]["overflow"])
        self.assertFalse(admission.is_open)
