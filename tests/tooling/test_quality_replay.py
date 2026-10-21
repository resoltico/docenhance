# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual bounded pipe capture keeps failed effects, frozen argv and owned raw artifacts."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, BinaryIO
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_accounting_fixture import factual_slots
from quality_capture_fixture import capture_code
from quality_evidence_fixture import failure_response

from quality_attempt import MAX_RESPONSE_BYTES, CaptureAdmission, new_attempt
from quality_capture import capture_attempt
from quality_capture_storage import finalize_file
from quality_capture_stream import CaptureReader
from quality_provenance import child_environment

prepare_tool_imports()


def process_slot(command: list[str] | None = None) -> dict[str, Any]:
    """Select the actual replay slot while retaining the frozen processing command."""
    frozen = command or [sys.executable, "process", "source", "--out-dir", "original", "--json"]
    return factual_slots(0, frozen)[1]


class QualityReplayTests(unittest.TestCase):
    """The standalone capture is not native/no-descendant qualification or natural quality proof."""

    def test_actual_exit5_and_committed_stdout_are_retained_without_cleanup(self) -> None:
        """Actual exit5 and committed stdout are retained without cleanup."""
        response = failure_response(5, "completed")
        code = f"print({json.dumps(json.dumps(response))}); raise SystemExit(5)"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            slot = process_slot()
            attempt, admission, calls = capture_code(root, slot, code)
            self.assertEqual(calls, [slot["argv"]])
            self.assertEqual(attempt["exit_code"], 5)
            self.assertEqual(attempt["publication"], "completed")
            self.assertEqual(attempt["response"], response)
            self.assertEqual(attempt["response_origin"], "complete_pipe_bytes")
            self.assertEqual(attempt["outcome"], "failed")
            self.assertEqual(attempt["direct_join"], "confirmed")
            self.assertEqual(attempt["descendant_cleanup"], "unconfirmed")
            self.assertTrue((root / slot["planned_directory"]).is_dir())
            for name in ("stdout", "stderr"):
                stream = attempt[name]
                raw = (root / stream["path"]).read_bytes()
                self.assertEqual(hashlib.sha256(raw).hexdigest(), stream["sha256"])
                self.assertEqual(stream["retained_bytes"], len(raw))
                self.assertTrue(stream["closed"])
            self.assertFalse(admission.retained_live)

    def test_full_frozen_argv_changes_only_output_destination(self) -> None:
        """Full frozen argv changes only output destination."""
        command = [
            sys.executable,
            "process",
            "./source",
            "--fixed-threshold",
            "0.5",
            "--output-mode",
            "bw",
            "--out-dir",
            "./old//",
            "--binarize",
            "fixed",
            "--json",
        ]
        original = command.copy()
        slot = process_slot(command)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / "source").write_bytes(b"synthetic source")
            attempt, _, calls = capture_code(root, slot, "print('{\"incomplete\":', end='')")
            expected = original.copy()
            expected[8] = slot["planned_target"]
            self.assertEqual(calls, [expected])
            self.assertEqual(attempt["argv"], expected)
            self.assertEqual(command, original)
            self.assertEqual((root / "source").read_bytes(), b"synthetic source")
            self.assertEqual(attempt["publication"], "unknown")
            self.assertIsNone(attempt["response"])

    def test_option_looking_asset_does_not_redirect_output_substitution(self) -> None:
        """Option looking asset does not redirect output substitution."""
        command = [
            sys.executable,
            "process",
            "source",
            "--protect-mask",
            "--out-dir",
            "--out-dir",
            "original",
            "--json",
        ]
        original = command.copy()
        slot = process_slot(command)
        code = (
            "from pathlib import Path; "
            f"p=Path({slot['planned_target']!r}); p.mkdir(); "
            "(p/'retained').write_bytes(b'uncertain effect'); print('{\"incomplete\":',end='')"
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            (root / "source").write_bytes(b"synthetic input")
            (root / "--out-dir").write_bytes(b"synthetic mask")
            attempt, _, calls = capture_code(root, slot, code)
            expected = original.copy()
            expected[6] = slot["planned_target"]
            self.assertEqual(calls, [expected])
            self.assertEqual(command, original)
            self.assertEqual((root / "--out-dir").read_bytes(), b"synthetic mask")
            self.assertEqual(
                (root / slot["planned_target"] / "retained").read_bytes(), b"uncertain effect"
            )
            self.assertEqual(attempt["publication"], "unknown")
            self.assertIsNone(attempt["response"])

    def test_both_streams_are_individually_bounded_and_exact_ceiling_requires_eof(self) -> None:
        """Both streams are individually bounded and exact ceiling requires eof."""
        for name, count in (
            ("stdout", MAX_RESPONSE_BYTES),
            ("stdout", MAX_RESPONSE_BYTES + 1),
            ("stderr", MAX_RESPONSE_BYTES + 1),
        ):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                fd = 1 if name == "stdout" else 2
                code = (
                    f"import os; data=b'x'*{count};\nwhile data:\n"
                    f" n=os.write({fd},data); data=data[n:]"
                )
                attempt, admission, calls = capture_code(root, process_slot(), code)
                stream = attempt[name]
                self.assertEqual(len(calls), 1)
                self.assertLessEqual(stream["retained_bytes"], MAX_RESPONSE_BYTES)
                self.assertEqual((root / stream["path"]).stat().st_size, stream["retained_bytes"])
                self.assertEqual(attempt["publication"], "unknown")
                self.assertIsNone(attempt["response"])
                if count == MAX_RESPONSE_BYTES:
                    self.assertEqual(stream["read_state"], "eof")
                    self.assertEqual(stream["observed_extent"], "exact")
                    self.assertEqual(stream["retained_bytes"], count)
                    self.assertIsNone(stream["errors"]["overflow"])
                else:
                    self.assertIsNotNone(stream["errors"]["overflow"])
                    self.assertFalse(admission.is_open)
                    self.assertEqual(stream["retention_state"], "prefix")

    def test_deadline_stops_one_producer_and_retains_unknown_effect(self) -> None:
        """Deadline stops one producer and retains unknown effect."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            # The two main-owner clock observations force the deadline without a timing sleep.
            with patch("quality_capture.time.monotonic", side_effect=[0, 601]):
                attempt, admission, calls = capture_code(root, process_slot(), "while True: pass")
            self.assertEqual(len(calls), 1)
            self.assertTrue(attempt["timed_out"])
            self.assertEqual(attempt["errors"]["wait"]["code"], "deadline_exceeded")
            self.assertEqual(attempt["publication"], "unknown")
            self.assertEqual(attempt["direct_join"], "confirmed")
            self.assertFalse(admission.is_open)

    def test_second_reader_start_failure_never_joins_unstarted_thread(self) -> None:
        """Second reader start failure never joins unstarted thread."""
        start = CaptureReader.start
        joined: list[tuple[str, bool]] = []
        original_join = CaptureReader.join

        def fail_second(reader: CaptureReader) -> None:
            if reader.name == "stderr":
                message = "controlled second reader startup"
                raise RuntimeError(message)
            return start(reader)

        def record_join(
            reader: CaptureReader, attempt: dict[str, Any], admission: CaptureAdmission
        ) -> bool:
            joined.append((reader.name, reader.started))
            return original_join(reader, attempt, admission)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            with (
                patch.object(CaptureReader, "start", fail_second),
                patch.object(CaptureReader, "join", record_join),
            ):
                attempt, admission, calls = capture_code(root, process_slot(), "while True: pass")
            self.assertEqual(len(calls), 1)
            self.assertIsNotNone(attempt["errors"]["start_stderr_reader"])
            self.assertEqual(attempt["direct_join"], "confirmed")
            self.assertFalse(admission.is_open)
            self.assertIn(("stdout", True), joined)
            self.assertIn(("stderr", False), joined)
            self.assertEqual(attempt["publication"], "unknown")

    def test_collision_never_launches_retries_or_removes_foreign_data(self) -> None:
        """Collision never launches retries or removes foreign data."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            slot = process_slot()
            directory = root / slot["planned_directory"]
            directory.mkdir()
            foreign = directory / "foreign"
            foreign.write_bytes(b"preserve")
            with patch("quality_capture.owned_process") as launch:
                attempt = new_attempt(slot)
                admission = CaptureAdmission(environment=child_environment())
                capture_attempt(attempt, slot, root, admission)
                launch.assert_not_called()
            self.assertEqual(foreign.read_bytes(), b"preserve")
            self.assertEqual(attempt["primary_error"], "errors.prepare_directory")
            self.assertEqual(attempt["launch_state"], "not_started")
            self.assertFalse(admission.is_open)

    def test_completed_stdout_survives_independent_stderr_finalization_failure(self) -> None:
        """Completed stdout survives independent stderr finalization failure."""
        response = failure_response(5, "completed")
        code = f"print({json.dumps(json.dumps(response))}); raise SystemExit(5)"

        def fail_stderr(stream: dict[str, Any], destination: BinaryIO) -> dict[str, BaseException]:
            failures = finalize_file(stream, destination)
            if Path(destination.name).name == "stderr":
                failures["flush"] = OSError(28, "controlled storage refusal")
            return failures

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            with patch("quality_capture_stream.finalize_file", fail_stderr):
                attempt, admission, calls = capture_code(root, process_slot(), code)
            self.assertEqual(len(calls), 1)
            self.assertEqual(attempt["publication"], "completed")
            self.assertEqual(attempt["response"], response)
            self.assertEqual(attempt["outcome"], "failed")
            self.assertEqual(attempt["stderr"]["errors"]["flush"]["errno"], 28)
            self.assertFalse(admission.is_open)
            next_slot = factual_slots(1, process_slot()["requested_argv"])[0]
            following = new_attempt(next_slot)
            with patch("quality_capture.owned_process") as launch:
                capture_attempt(following, next_slot, root, admission)
                launch.assert_not_called()
            self.assertEqual(following["primary_error"], "errors.prepare_directory")
            self.assertEqual(following["errors"]["prepare_directory"]["code"], "admission_closed")
            self.assertIsNone(following["artifact_directory"])
            self.assertFalse((root / next_slot["planned_directory"]).exists())

    def test_partial_second_start_joins_the_thread_that_actually_started(self) -> None:
        """Partial second start joins the thread that actually started."""
        start = CaptureReader.start
        joined: list[tuple[str, bool]] = []
        original_join = CaptureReader.join

        def fail_after_start(reader: CaptureReader) -> None:
            start(reader)
            if reader.name == "stderr":
                message = "controlled exception after actual reader startup"
                raise RuntimeError(message)

        def record_join(
            reader: CaptureReader, attempt: dict[str, Any], admission: CaptureAdmission
        ) -> bool:
            joined.append((reader.name, reader.started))
            return original_join(reader, attempt, admission)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            with (
                patch.object(CaptureReader, "start", fail_after_start),
                patch.object(CaptureReader, "join", record_join),
            ):
                attempt, admission, calls = capture_code(root, process_slot(), "while True: pass")
            self.assertEqual(len(calls), 1)
            self.assertEqual(joined, [("stdout", True), ("stderr", True)])
            self.assertEqual(attempt["direct_join"], "confirmed")
            self.assertIsNotNone(attempt["errors"]["start_stderr_reader"])
            self.assertFalse(admission.retained_live)
            self.assertFalse(admission.is_open)
