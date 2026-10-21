# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One actual journal owner refuses overflow, collisions and unsuccessful final delivery."""

from __future__ import annotations

import copy
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_accounting_fixture import factual_run, factual_slots
from quality_capture_fixture import capture_code, pipe_producer
from quality_evidence_fixture import component_inputs, delivered_candidate, failure_response

from document_quality import read_run_json
from quality_attempt import CaptureAdmission, new_attempt
from quality_capture import capture_attempt
from quality_execution import execution_evidence
from quality_journal import JournalBudget, JournalError, QualityJournal
from quality_provenance import child_environment
from quality_run import QualityRun

if TYPE_CHECKING:
    from collections.abc import Buffer

prepare_tool_imports()


SECOND_FLUSH = 2


class FaultHandle:
    """Inject only the selected real handle's final operation; never retry the failed frame."""

    def __init__(self, stream: io.FileIO, fault: str) -> None:
        """Retain the actual exclusive descriptor owner and one selected delivery fault."""
        self.stream, self.fault = stream, fault
        self.writes = self.flushes = self.closes = 0

    def write(self, data: Buffer, /) -> int | None:
        """Forward a real write and retain its actual progress result."""
        self.writes += 1
        return self.stream.write(data)

    def flush(self) -> None:
        """Inject only the requested flush after recording its call."""
        self.flushes += 1
        if self.fault == "flush" or (self.fault == "second_flush" and self.flushes == SECOND_FLUSH):
            raise OSError(28, "controlled final flush")
        return self.stream.flush()

    def close(self) -> None:
        """Consume the real stream once unless the control refuses before close."""
        self.closes += 1
        if self.fault == "before_close":
            raise OSError(5, "controlled refusal before close")
        self.stream.close()
        if self.fault == "close":
            raise OSError(5, "controlled final close")


class UncheckedVolume:
    """Isolate framing controls; actual capacity observations have separate tests."""

    def check(self) -> None:
        """Do not manufacture a volume observation in this journal-only fixture."""


def actual_sink(journal: QualityJournal) -> io.FileIO:
    """Require the concrete fixture's actual opened file before wrapping a delivery fault."""
    if not isinstance(journal.stream, io.FileIO):
        message = "The journal fixture has no actual raw file owner"
        raise TypeError(message)
    return journal.stream


def journal_fixture(output: Path) -> tuple[dict[str, Any], QualityJournal, CaptureAdmission]:
    """Actual projections and exclusive raw handle; no producer/native success is fabricated."""
    value = factual_run(1)
    pages, errors = value["report"]["evidence"], value["report"]["errors"]
    prior: dict[str, list[dict[str, Any]]] = {page["sample"]: [] for page in pages}
    budget = JournalBudget(value["intended"], pages, errors, prior, 8)
    admission = CaptureAdmission()
    # Isolate append/framing; actual capacity observations have separate volume controls.
    volume = UncheckedVolume()
    journal = QualityJournal(output, budget, admission, errors, volume)
    return value, journal, admission


def append_observations(value: dict[str, Any], journal: QualityJournal) -> None:
    """Persist the real fixture's fixed original and invocation frames once."""
    for item in value["events"]:
        key = "role_index" if item["event"] == "original_observed" else "invocation_index"
        journal.observe(item["event"], item[key], item["observation"])


class QualityJournalTests(unittest.TestCase):
    """Keep journal framing, effects and failed delivery monotone."""

    def test_frame_overflow_refuses_before_write_and_closes_later_admission(self) -> None:
        """Frame overflow refuses before write and closes later admission."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "quality-run.json"
            _, journal, admission = journal_fixture(output)
            try:
                before, spent = output.read_bytes(), journal.spent
                with self.assertRaisesRegex(JournalError, "pre-effect allowance"):
                    journal.append(b"one byte too many", 0, "append")
                self.assertEqual(output.read_bytes(), before)
                self.assertEqual(journal.spent, spent)
                self.assertIsNotNone(journal.errors["serialization_allowance"])
                self.assertFalse(admission.is_open)
                with self.assertRaises(JournalError):
                    journal.append(b"retry", 5, "append")
                self.assertEqual(output.read_bytes(), before)
            finally:
                journal.close()

    def test_exclusive_collision_preserves_foreign_bytes_without_replacement(self) -> None:
        """Exclusive collision preserves foreign bytes without replacement."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "foreign"
            output.write_bytes(b"previous evidence must survive")
            with self.assertRaises(JournalError):
                journal_fixture(output)
            self.assertEqual(output.read_bytes(), b"previous evidence must survive")
            self.assertEqual(sorted(path.name for path in output.parent.iterdir()), ["foreign"])

    def test_prefix_is_not_completed_and_exact_final_file_has_one_root(self) -> None:
        """Prefix is not completed and exact final file has one root."""
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "quality-run.json"
            value, journal, _ = journal_fixture(output)
            prefix = output.read_bytes()
            self.assertEqual(prefix, journal.budget.prefix)
            with self.assertRaises(ValueError):
                read_run_json(prefix)
            append_observations(value, journal)
            before = output.read_bytes()
            with self.assertRaises(ValueError):
                read_run_json(before)
            journal.finish(value["report"], value["terminal"])
            actual = output.read_bytes()
            self.assertTrue(actual.endswith(b"}\n"))
            self.assertEqual(read_run_json(actual), value)
            self.assertLessEqual(len(actual), journal.budget.total)
            self.assertEqual(actual.count(b'"kind":"quality_run"'), 1)
            self.assertEqual(journal.spent, len(actual))
            self.assertEqual(len(journal.events), 21)

    def test_failed_final_flush_or_close_retains_bytes_without_success_or_retry(self) -> None:
        """Failed final flush or close retains bytes without success or retry."""
        for fault in ("flush", "close", "before_close"):
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "quality-run.json"
                value, journal, admission = journal_fixture(output)
                append_observations(value, journal)
                handle = FaultHandle(actual_sink(journal), fault)
                journal.stream = handle
                before_finish = copy.deepcopy(value)
                try:
                    with self.subTest(fault=fault), self.assertRaises(JournalError):
                        journal.finish(value["report"], value["terminal"])
                    retained = output.read_bytes()
                    self.assertEqual(read_run_json(retained), before_finish)
                    self.assertFalse(admission.is_open)
                    field = "final_flush" if fault == "flush" else "final_close"
                    self.assertIsNotNone(journal.errors[field])
                    self.assertEqual(handle.closes, 1)
                    if fault == "before_close":
                        self.assertFalse(handle.stream.closed)
                        self.assertIn(handle, admission.retained_live)
                    journal.close()
                    self.assertEqual(handle.closes, 1)
                    with self.assertRaises(JournalError):
                        journal.append(b"retry", 5, "final")
                    self.assertEqual(output.read_bytes(), retained)
                finally:
                    # Fixture cleanup of its retained actual owner is not a production retry.
                    handle.stream.close()

    def test_report_allowance_failure_preserves_completed_publication_observation(self) -> None:
        """Report allowance failure preserves completed publication observation."""
        response = failure_response(5, "completed")
        code = f"print({json.dumps(json.dumps(response))}); raise SystemExit(5)"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            slot = factual_slots(
                0, [sys.executable, "process", "source", "--out-dir", "old", "--json"]
            )[1]
            observed, _, calls = capture_code(root, slot, code)
            self.assertEqual(len(calls), 1)
            self.assertEqual(observed["publication"], "completed")
            self.assertEqual(observed["outcome"], "failed")
            before = copy.deepcopy(observed)
            value, journal, admission = journal_fixture(root / "quality-run.json")
            value["report"]["evidence"][0]["roles"]["candidate"]["replay_process"] = observed
            journal.budget.report = 0
            try:
                with self.assertRaises(JournalError):
                    journal.finish(value["report"], value["terminal"])
                self.assertEqual(observed, before)
                self.assertEqual(observed["publication"], "completed")
                self.assertFalse(admission.is_open)
                self.assertIsNotNone(journal.errors["serialization_allowance"])
            finally:
                journal.close()

    def test_prepared_or_finished_persistence_failure_is_fatal_without_later_launch(self) -> None:
        """The public closed-gate run attaches actual persistence causes without a child."""
        for phase in ("prepared", "finished"):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                plan, executable, _record = delivered_candidate(root)
                run = QualityRun(
                    component_inputs(plan, root, executable), root / "quality-run.json"
                )
                original = run.pages[0]["roles"]["candidate"]["original"]
                self.assertTrue(execution_evidence(original, root, run.admission))
                run.original(original)
                handle = FaultHandle(
                    actual_sink(run.journal), "flush" if phase == "prepared" else "second_flush"
                )
                run.journal.stream = handle
                try:
                    with patch("quality_capture.owned_process") as launch:
                        with self.subTest(phase=phase), self.assertRaises(JournalError):
                            run.invoke(0)
                        launch.assert_not_called()
                    attempt = run.attempts[0]
                    cause = (
                        attempt["errors"]["prepared_persist"]
                        if phase == "prepared"
                        else attempt["cleanup_errors"]["finished_persist"]
                    )
                    self.assertIsNotNone(cause)
                    self.assertEqual(
                        attempt["primary_error"],
                        "errors.prepared_persist"
                        if phase == "prepared"
                        else "cleanup_errors.finished_persist",
                    )
                    self.assertFalse(run.admission.is_open)
                    self.assertNotIn(0, run.finished)
                    self.assertIsNone(attempt["artifact_directory"])
                    self.assertIsNone(attempt["pid"])
                    self.assertFalse((root / run.slots[0]["planned_directory"]).exists())
                    retained = (root / "quality-run.json").read_bytes()
                    with self.assertRaises(ValueError):
                        read_run_json(retained)
                    with self.assertRaises(JournalError):
                        run.skip(1)
                    self.assertEqual((root / "quality-run.json").read_bytes(), retained)
                finally:
                    run.close()

    def test_finished_journal_failure_keeps_actual_standalone_capture_first_cause(self) -> None:
        """Public journal failure retains an unqualified capture's actual earlier schema cause."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            value, journal, journal_admission = journal_fixture(root / "quality-run.json")
            slot = value["intended"]["invocations"][1]
            attempt = new_attempt(slot)
            admission = CaptureAdmission(environment=child_environment())
            calls: list[list[str]] = []
            journal.observe("original_observed", 0, value["events"][0]["observation"])
            journal.observe("invocation_prepared", 1, attempt)
            try:
                with pipe_producer("print('{}')", calls):
                    capture_attempt(attempt, slot, root, admission)
                before = copy.deepcopy(attempt)
                self.assertEqual(len(calls), 1)
                self.assertEqual(attempt["direct_join"], "confirmed")
                self.assertEqual(attempt["descendant_cleanup"], "unconfirmed")
                self.assertEqual(attempt["primary_error"], "errors.response_schema")
                stdout = (root / attempt["stdout"]["path"]).read_bytes()
                journal.stream = FaultHandle(actual_sink(journal), "flush")
                with self.assertRaises(JournalError):
                    journal.observe("invocation_finished", 1, attempt)
                self.assertEqual(attempt, before)
                self.assertEqual((root / attempt["stdout"]["path"]).read_bytes(), stdout)
                self.assertFalse(journal_admission.is_open)
                self.assertIsNotNone(journal.errors["append_flush"])
                retained = (root / "quality-run.json").read_bytes()
                with self.assertRaises(JournalError):
                    journal.observe(
                        "invocation_prepared", 2, new_attempt(value["intended"]["invocations"][2])
                    )
                self.assertEqual((root / "quality-run.json").read_bytes(), retained)
            finally:
                journal.close()
