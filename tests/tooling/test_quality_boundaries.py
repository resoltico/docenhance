# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent final-review counterexamples for effect delivery and execution identity."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from tools_path import ROOT

from jsonschema import Draft202012Validator
from quality_evidence_fixture import refusal, write_reference

from quality_collection import BundleChecks, replay_record, validate_run_identities
from quality_execution import MAX_RESPONSE_BYTES, execution_evidence
from quality_replay import ReplayObservation, delivery
from quality_replay import replay_record as delivered_record


class QualityBoundaryTests(unittest.TestCase):
    """Raw byte identities cannot substitute for bounded delivery or distinct executions."""

    def test_oversized_original_stdout_is_retained_without_full_read_or_safe_effect(self) -> None:
        """Even valid JSON plus excessive whitespace must not escape the parse bound."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            command = [sys.executable, "process", "source", "--out-dir", "result", "--json"]
            role = refusal(root, command, "large", exit_code=5, publication="completed")
            role["outcome"] = "failed"
            response = copy.deepcopy(role["execution"]["response"])
            raw = json.dumps(response).encode() + b" " * MAX_RESPONSE_BYTES
            role["execution"]["stdout"] = write_reference(root, "large.stdout", raw)
            role["execution"].update(response=None, publication="unknown")
            observed: dict[str, str] = {}
            with patch.object(Path, "read_bytes", side_effect=AssertionError("unbounded read")):
                execution_evidence(role, root, observed)
            self.assertEqual(observed["observed_publication"], "unknown")
            self.assertEqual((root / "large.stdout").stat().st_size, len(raw))
            role["execution"].update(response=response, publication="completed")
            with self.assertRaisesRegex(ValueError, "bounded raw stdout"):
                execution_evidence(role, root, observed)
            self.assertEqual(observed["observed_publication"], "unknown")

    def test_replay_checks_delivered_exit_and_output_before_accepting_verified_record(self) -> None:
        """A valid wrong-output response is not proof of delivery from the substituted command."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            output = root / "candidate"
            output.mkdir()
            (root / "stderr").write_bytes(b"")
            record = {"record": {"run": "a" * 32}}
            ref = write_reference(output, "run.json", record)
            response = {
                "schema_version": 14,
                "command": "process",
                "version": "0.7.0",
                "exit_code": 0,
                "publication": "completed",
                "method": "B03",
                "method_version": 1,
                "binarization": None,
                "rotation_degrees": 0,
                "source_decoding": None,
                "output": str(output / "result.png"),
                "record": {
                    "run": "a" * 32,
                    "path": "run.json",
                    "sha256": ref["sha256"],
                    "bytes": (output / "run.json").stat().st_size,
                },
            }
            schema = json.loads((ROOT / "schemas/command-response.schema.json").read_bytes())
            for change in ({"output": "another/result.png"}, {"exit_code": 5}):
                wrong = response | change
                if "output" in change:
                    Draft202012Validator(schema).validate(wrong)
                attempt = {
                    "exit_code": 0,
                    "timed_out": False,
                    "response": wrong,
                    "publication": "completed",
                }
                verify = Mock(return_value=record)
                with self.assertRaisesRegex(ValueError, "coherent processing"):
                    delivered_record(attempt, output, root / "source", Path(sys.executable), verify)
                verify.assert_not_called()
                self.assertEqual(attempt["publication"], "completed")

    def test_baseline_and_replay_identity_collisions_cannot_claim_distinct_runs(self) -> None:
        """All six original/replay identifiers are checked, including the comparator."""
        names = ("candidate", "baseline", "replicate")
        records = {name: {"record": {"run": f"{index:032x}"}} for index, name in enumerate(names)}
        roles = {
            name: {"replay_attempt": {"response": {"record": {"run": f"{index + 3:032x}"}}}}
            for index, name in enumerate(names)
        }
        validate_run_identities(records, roles)
        for collided in ("candidate", "replicate"):
            wrong = copy.deepcopy(records)
            wrong["baseline"]["record"]["run"] = wrong[collided]["record"]["run"]
            with self.assertRaisesRegex(ValueError, "pairwise distinct"):
                validate_run_identities(wrong, roles)
        repeated = copy.deepcopy(roles)
        repeated["baseline"]["replay_attempt"]["response"]["record"]["run"] = records["candidate"][
            "record"
        ]["run"]
        with self.assertRaisesRegex(ValueError, "pairwise distinct"):
            validate_run_identities(records, repeated)

    def test_replay_cannot_reuse_scored_execution_identity(self) -> None:
        """Keep the actual attempt but refuse identity reuse before semantic equality can pass."""
        record = {"record": {"run": "a" * 32}}
        attempt = {"argv": ["selected", "process"]}
        replay = Mock(return_value=ReplayObservation(record, attempt))
        checks = BundleChecks(Path("selected"), {}, Mock(), replay, Mock())
        report = {"role": "baseline", "command": ["selected", "process"]}
        with self.assertRaisesRegex(ValueError, "independently identified"):
            replay_record(Path(), record, report, checks)
        self.assertEqual(report["replay_attempt"], attempt)
        self.assertEqual(replay.call_count, 1)

    def test_deep_original_and_replay_stdout_remain_unknown_without_abort(self) -> None:
        """A small deeply nested response retains raw evidence and never escapes collection."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            command = [sys.executable, "process", "source", "--out-dir", "result", "--json"]
            role = refusal(root, command, "deep", exit_code=5, publication="completed")
            role["outcome"] = "failed"
            raw = b'{"x":' + b"[" * 1500 + b"0" + b"]" * 1500 + b"}"
            role["execution"]["stdout"] = write_reference(root, "stdout", raw)
            role["execution"].update(response=None, publication="unknown")
            observed: dict[str, str] = {}
            execution_evidence(role, root, observed)
            self.assertEqual(observed["observed_publication"], "unknown")
            attempt = {"exit_code": 5, "timed_out": False, "errors": []}
            delivery(attempt, root)
            self.assertEqual(attempt["publication"], "unknown")
            self.assertIsNone(attempt["response"])
            self.assertEqual((root / "stdout").read_bytes(), raw)
