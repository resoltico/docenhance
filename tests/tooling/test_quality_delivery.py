# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Command delivery, retained reviews and integer preservation cannot fabricate quality benefit."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_capture_fixture import pipe_producer
from quality_evidence_fixture import (
    binary_record,
    component_inputs,
    delivered_candidate,
    disk_plan,
    successful_response,
    write_reference,
)

from evaluate_document_quality import evaluate, read_bundle
from quality_attempt import CaptureAdmission
from quality_capture import capture_attempt
from quality_collection import BundleChecks
from quality_execution import execution_evidence
from quality_fidelity import fidelity
from quality_json import read_json
from quality_provenance import child_environment
from quality_replay import replay
from quality_run import QualityRun

prepare_tool_imports()


class QualityDeliveryTests(unittest.TestCase):
    """Real captured delivery refusals cannot become qualified verification or safe retry."""

    def test_actual_verifier_and_replay_cannot_accept_forged_minimal_success(self) -> None:
        """A real pipe's forged JSON fails full schema admission and blocks dependent replay."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            directory = root / "candidate-0"
            directory.mkdir()
            record = binary_record()
            response = successful_response(directory, record)
            response["output"] = "candidate-0/result.png"
            command = plan["protocol"]["pages"][0]["commands"]["candidate"]
            plan["assessments"][0]["candidate"] = {
                "outcome": "completed",
                "bundle": "candidate-0",
                "execution": {
                    "argv": command,
                    "exit_code": 0,
                    "timed_out": False,
                    "response": response,
                    "publication": "completed",
                    "stdout": write_reference(root, "original.stdout", response),
                    "stderr": write_reference(root, "original.stderr", b""),
                },
            }
            (root / "run.json").write_bytes(b"{}")
            forged = {"publication": "completed", "establishes": "artifacts_agree_with_record"}
            code = f"print({json.dumps(json.dumps(forged))})"
            calls: list[list[str]] = []
            run = QualityRun(component_inputs(plan, root, executable), root / "quality-run.json")
            checks = BundleChecks(run, record["build"], read_bundle, replay, fidelity)
            try:
                original = run.pages[0]["roles"]["candidate"]["original"]
                self.assertTrue(execution_evidence(original, root, run.admission))
                run.original(original)
                attempt = run.attempts[0]
                run.journal.observe("invocation_prepared", 0, attempt)
                admission = CaptureAdmission(environment=child_environment())
                with pipe_producer(code, calls):
                    capture_attempt(attempt, run.slots[0], root, admission)
                self.assertFalse(admission.is_open)
                self.assertIsNotNone(admission.closure_reason)
                run.journal.observe("invocation_finished", 0, attempt)
                run.finished[0] = copy.deepcopy(attempt)
                cause = admission.closure_reason
                if cause is None:
                    self.fail("The refused capture lost its real closure cause")
                run.admission.close(cause)
                # The receiving seam consumes an actual failed capture, never native success.
                with patch.object(run, "invoke", return_value=attempt) as consume:
                    with self.assertRaisesRegex(ValueError, "coherent complete delivery"):
                        read_bundle(checks, 0, record["source"]["sha256"], response)
                    consume.assert_called_once_with(0)
                with patch("quality_capture.owned_process") as later_launch:
                    self.assertIsNone(replay(checks, 0, record["source"]["sha256"], record))
                    later_launch.assert_not_called()
                self.assertEqual(len(calls), 1)
                self.assertEqual(calls[0][1], "verify")
                self.assert_refused_capture(root, run, attempt, forged)
            finally:
                run.close()

    def assert_refused_capture(
        self, root: Path, run: QualityRun, attempt: dict[str, Any], forged: dict[str, Any]
    ) -> None:
        """Inspect actual retained refused bytes and the still-closed dependent capture gate."""
        self.assertEqual(run.finished[0], attempt)
        self.assertEqual(attempt["outcome"], "failed")
        self.assertIsNotNone(attempt["errors"]["response_schema"])
        self.assertEqual(attempt["direct_join"], "confirmed")
        self.assertEqual(attempt["descendant_cleanup"], "unconfirmed")
        self.assertTrue((root / attempt["artifact_directory"]).is_dir())
        self.assertEqual(read_json((root / attempt["stdout"]["path"]).read_bytes()), forged)
        self.assertEqual(run.attempts[1]["launch_state"], "not_started")
        self.assertEqual((root / "run.json").read_bytes(), b"{}")
        self.assertFalse(run.admission.is_open)

    def test_pending_capture_receipt_refuses_before_journal_or_child(self) -> None:
        """Component fixtures cannot confer operational native/runtime capture qualification."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            output = root / "quality-run.json"
            before = sorted(path.name for path in root.iterdir())
            with (
                patch("evaluate_document_quality.QualityRun") as journal_owner,
                patch("quality_capture.owned_process") as producer,
            ):
                with self.assertRaises(ValueError):
                    evaluate(plan, root, executable, output)
                journal_owner.assert_not_called()
                producer.assert_not_called()
            self.assertFalse(output.exists())
            self.assertEqual(sorted(path.name for path in root.iterdir()), before)

    def test_pending_component_cannot_launch_an_original_ready_verifier(self) -> None:
        """Complete original delivery cannot erase the actual pending-receipt refusal."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable, _record = delivered_candidate(root)
            run = QualityRun(component_inputs(plan, root, executable), root / "quality-run.json")
            try:
                self.assertIsNone(run.provenance)
                cause = run.admission.closure_reason
                self.assertIsNotNone(run.errors["referenced_evidence"])
                self.assertFalse(run.admission.is_open)
                original = run.pages[0]["roles"]["candidate"]["original"]
                self.assertTrue(execution_evidence(original, root, run.admission))
                run.original(original)
                with patch("quality_capture.owned_process") as producer:
                    attempt = run.invoke(0)
                    producer.assert_not_called()
                self.assertEqual(run.admission.closure_reason, cause)
                self.assertIsNone(run.errors["no_child_admission"])
                self.assertFalse(run.admission.is_open)
                self.assertEqual(attempt["launch_state"], "not_started")
                self.assertIsNone(attempt["pid"])
                self.assertIsNone(attempt["artifact_directory"])
                self.assertFalse((root / run.slots[0]["planned_directory"]).exists())
            finally:
                run.close()
