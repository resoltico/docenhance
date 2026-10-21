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
from types import SimpleNamespace
from typing import Any
from unittest.mock import patch

from tools_path import ROOT

from jsonschema import Draft202012Validator
from quality_accounting_fixture import (
    factual_run,
    factual_slots,
    unreached_fidelity,
    unreached_read,
    unreached_replay,
)
from quality_evidence_fixture import (
    binary_record,
    continuous_fixtures,
    delivered_candidate,
    refusal,
    successful_response,
    write_reference,
)

from quality_attempt import (
    MAX_RESPONSE_BYTES,
    CaptureAdmission,
    delivered_path,
    new_attempt,
    new_original,
)
from quality_capture_response import observe_response
from quality_collection import BundleChecks, _runs
from quality_execution import execution_evidence
from quality_observations import completed_record_agreement, original_ready


class QualityBoundaryTests(unittest.TestCase):
    """Raw byte identities cannot substitute for bounded delivery or distinct executions."""

    def test_oversized_original_stdout_is_retained_without_full_read_or_safe_effect(self) -> None:
        """Valid JSON plus whitespace exceeds L without rewriting historical bytes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            command = [sys.executable, "process", "source", "--out-dir", "result", "--json"]
            role = refusal(root, command, "large", exit_code=5, publication="completed")
            role["outcome"] = "failed"
            response = copy.deepcopy(role["execution"]["response"])
            raw = json.dumps(response).encode() + b" " * MAX_RESPONSE_BYTES
            role["execution"]["stdout"] = write_reference(root, "large.stdout", raw)
            role["execution"].update(response=None, publication="unknown")
            original = new_original(0, role)
            with patch.object(Path, "read_bytes", side_effect=AssertionError("unbounded read")):
                execution_evidence(original, root, CaptureAdmission())
            self.assertEqual(original["observed_publication"], "unknown")
            self.assertEqual(original["stdout"]["retained_bytes"], len(raw))
            self.assertEqual((root / "large.stdout").stat().st_size, len(raw))
            role["execution"].update(response=response, publication="completed")
            changed = new_original(0, role)
            self.assertFalse(execution_evidence(changed, root, CaptureAdmission()))
            self.assertIsNotNone(changed["errors"]["delivery"])
            self.assertEqual(changed["observed_publication"], "unknown")

    def test_replay_checks_delivered_exit_and_output_before_accepting_verified_record(self) -> None:
        """Actual native-schema/target admission refuses wrong delivery without erasing commit."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            slot = factual_slots(
                0, [sys.executable, "process", "source", "--out-dir", "old", "--json"]
            )[1]
            output = root / slot["planned_target"]
            output.mkdir(parents=True)
            response = successful_response(output, binary_record())
            response["output"] = delivered_path(slot["planned_target"], "result.png")
            schema = json.loads((ROOT / "schemas/command-response.schema.json").read_bytes())
            # Pure response-boundary controls; no producer/verified product is fabricated.
            for change in ("healthy", "target", "actual_exit", "invalid_response_exit"):
                attempt = new_attempt(slot)
                attempt.update(exit_code=0, publication="unknown")
                candidate = copy.deepcopy(response)
                if change == "target":
                    candidate["output"] = "another/result.png"
                    Draft202012Validator(schema).validate(candidate)
                elif change == "actual_exit":
                    attempt["exit_code"] = 4
                elif change == "invalid_response_exit":
                    candidate["exit_code"] = 5
                    attempt["publication"] = "completed"
                admission = CaptureAdmission()
                observe_response(attempt, slot, json.dumps(candidate).encode(), admission)
                self.assertEqual(attempt["publication"], "completed")
                if change == "healthy":
                    self.assertIsNone(attempt["primary_error"])
                else:
                    self.assertIsNotNone(attempt["primary_error"])
                    self.assertFalse(admission.is_open)
                self.assertEqual(attempt["outcome"], "failed")

    def test_baseline_and_replay_identity_collisions_cannot_claim_distinct_runs(self) -> None:
        """Actual collection's pure identity ledger includes all six scored/replay identifiers."""
        names = ("candidate", "baseline", "replicate")
        pairs = [(binary_record(f"{i:032x}"), binary_record(f"{i + 3:032x}")) for i in range(3)]
        for collision in (None, "candidate", "replicate", "replayed_candidate"):
            pages = factual_run()["report"]["evidence"]
            checks = BundleChecks(
                SimpleNamespace(pages=pages, admission=CaptureAdmission()),
                {},
                unreached_read,
                unreached_replay,
                unreached_fidelity,
            )
            current = copy.deepcopy(pairs)
            if collision == "candidate":
                current[1][0]["record"]["run"] = current[0][0]["record"]["run"]
            elif collision == "replicate":
                current[2][0]["record"]["run"] = current[1][0]["record"]["run"]
            elif collision == "replayed_candidate":
                current[1][1]["record"]["run"] = current[0][1]["record"]["run"]
            seen: set[str] = set()
            admitted = []
            for name, pair in zip(names, current, strict=True):
                admitted.append(_runs(checks, pages[0]["roles"][name], pair, seen))
            if collision is None:
                self.assertEqual(admitted, [True, True, True])
                self.assertEqual(len(seen), 6)
            else:
                self.assertIn(member=False, container=admitted)
                self.assertFalse(checks.run.admission.is_open)
                self.assertTrue(
                    any(
                        role["validation"]["role_identity"] is not None
                        for role in pages[0]["roles"].values()
                    )
                )

    def test_replay_cannot_reuse_scored_execution_identity(self) -> None:
        """Actual same-pair identity refusal leaves completed observation snapshots untouched."""
        pages = factual_run()["report"]["evidence"]
        role = pages[0]["roles"]["baseline"]
        before = copy.deepcopy(role["replay_process"])
        checks = BundleChecks(
            SimpleNamespace(pages=pages, admission=CaptureAdmission()),
            {},
            unreached_read,
            unreached_replay,
            unreached_fidelity,
        )
        record = binary_record()
        self.assertFalse(_runs(checks, role, (record, copy.deepcopy(record)), set()))
        self.assertEqual(role["replay_process"], before)
        self.assertIsNotNone(role["validation"]["role_identity"])
        self.assertEqual(role["outcome"], "missing_evidence")

    def test_deep_original_and_replay_stdout_remain_unknown_without_abort(self) -> None:
        """A bounded deeply nested response remains raw evidence rather than parsed success."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            command = [sys.executable, "process", "source", "--out-dir", "result", "--json"]
            role = refusal(root, command, "deep", exit_code=5, publication="completed")
            role["outcome"] = "failed"
            raw = b'{"x":' + b"[" * 1500 + b"0" + b"]" * 1500 + b"}"
            role["execution"]["stdout"] = write_reference(root, "stdout", raw)
            role["execution"].update(response=None, publication="unknown")
            original = new_original(0, role)
            self.assertFalse(execution_evidence(original, root, CaptureAdmission()))
            self.assertEqual(original["observed_publication"], "unknown")
            slot = factual_slots(0, command)[1]
            attempt = new_attempt(slot)
            attempt.update(exit_code=5, publication="unknown")
            observe_response(attempt, slot, raw, CaptureAdmission())
            self.assertEqual(attempt["publication"], "unknown")
            self.assertIsNone(attempt["response"])
            self.assertIsNotNone(attempt["errors"]["interpret_stdout"])
            self.assertEqual((root / "stdout").read_bytes(), raw)

    def test_original_ready_requires_empty_stderr_and_both_streams_within_limit(self) -> None:
        """Original ready requires empty stderr and both streams within L."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, _executable, _record = delivered_candidate(root)
            role = plan["assessments"][0]["candidate"]
            healthy = new_original(0, role)
            self.assertTrue(execution_evidence(healthy, root, CaptureAdmission()))
            self.assertTrue(original_ready(healthy))
            role["execution"]["stderr"] = write_reference(root, "nonempty.stderr", b"x")
            nonempty = new_original(0, role)
            self.assertTrue(execution_evidence(nonempty, root, CaptureAdmission()))
            self.assertFalse(original_ready(nonempty))
            self.assertEqual(nonempty["stderr"]["retained_bytes"], 1)
            self.assertEqual(nonempty["observed_publication"], "completed")
            role["execution"]["stderr"] = write_reference(
                root, "large.stderr", b"x" * (MAX_RESPONSE_BYTES + 1)
            )
            oversized = new_original(0, role)
            self.assertFalse(execution_evidence(oversized, root, CaptureAdmission()))
            self.assertFalse(original_ready(oversized))
            self.assertEqual(oversized["observed_publication"], "completed")
            self.assertEqual(oversized["errors"]["raw_stderr"]["code"], "overflow")

    def test_completed_record_agreement_binds_each_verifier_artifact_identity(self) -> None:
        """Completed record agreement binds each verifier artifact identity."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            record = binary_record()
            mask = continuous_fixtures.Fixture(2, 1, ((1,), (0,))).encoded()
            mask_ref = write_reference(root, "supplied-mask.png", mask)
            record["request"]["protection"] = {
                "supplied": {"sha256": mask_ref["sha256"], "bytes": len(mask)}
            }
            record["assets"] = [
                {
                    "role": "protection_mask",
                    "path": "assets/protect-mask.png",
                    "sha256": mask_ref["sha256"],
                    "bytes": len(mask),
                    "width": 2,
                    "height": 1,
                    "frame": "metadata_oriented_source",
                    "protected_samples": 1,
                }
            ]
            repeated = copy.deepcopy(record)
            repeated["record"]["run"] = "b" * 32
            role: dict[str, Any] = {"declared": {"bundle": "scored"}}
            # Pure receiving data uses actual fixture bytes/lengths, not native qualification.
            for target, current, name in (
                ("scored", record, "scored_verify"),
                ("replayed", repeated, "replay_verify"),
            ):
                directory = root / target
                directory.mkdir()
                (directory / "assets").mkdir()
                (directory / "assets" / "protect-mask.png").write_bytes(mask)
                delivered = successful_response(directory, current)
                delivered["output"] = delivered_path(target, "result.png")
                raw_record = (directory / "run.json").read_bytes()
                confirmed = [
                    {key: item[key] for key in ("path", "sha256", "bytes")}
                    for item in [current["output"], *current["assets"]]
                ]
                confirmed.append(
                    {
                        "path": "run.json",
                        "sha256": delivered["record_sha256"],
                        "bytes": len(raw_record),
                    }
                )
                role[name] = {
                    "argv": ["selected", "verify", target, "--json"],
                    "response": {
                        "schema_version": 15,
                        "command": "verify",
                        "exit_code": 0,
                        "version": current["build"]["application_version"],
                        "directory": target,
                        "run": current["record"]["run"],
                        "recorded": current["record"]["recorded"],
                        "establishes": "artifacts_agree_with_record",
                        "confirmed": confirmed,
                    },
                }
                if name == "scored_verify":
                    role["original"] = {"response": delivered}
                    role["record_sha256"] = delivered["record_sha256"]
                else:
                    role["replay_process"] = {
                        "response": delivered,
                        "argv": ["selected", "process", "source", "--out-dir", target, "--json"],
                    }
            source, build = {"sha256": record["source"]["sha256"]}, {"build": record["build"]}
            self.assertEqual(completed_record_agreement(role, source, build), ("a" * 32, "b" * 32))
            for name in ("scored_verify", "replay_verify"):
                for index in (0, 1):
                    for key in ("sha256", "bytes"):
                        wrong = copy.deepcopy(role)
                        identity = wrong[name]["response"]["confirmed"][index]
                        identity[key] = "f" * 64 if key == "sha256" else identity[key] + 1
                        with (
                            self.subTest(name=name, index=index, key=key),
                            self.assertRaisesRegex(ValueError, "artifact identity"),
                        ):
                            completed_record_agreement(wrong, source, build)
                wrong = copy.deepcopy(role)
                wrong[name]["response"]["confirmed"].append(
                    copy.deepcopy(wrong[name]["response"]["confirmed"][0])
                )
                with self.assertRaisesRegex(ValueError, "artifact inventory"):
                    completed_record_agreement(wrong, source, build)
                for invalid_size in (0, 256 * 1024 + 1):
                    wrong = copy.deepcopy(role)
                    wrong[name]["response"]["confirmed"][-1]["bytes"] = invalid_size
                    with self.assertRaisesRegex(ValueError, "Completed verifier disagrees"):
                        completed_record_agreement(wrong, source, build)
