# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Command delivery, retained reviews and integer preservation cannot fabricate quality benefit."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from jsonschema import ValidationError
from quality_evidence_fixture import continuous_fixtures, disk_plan, refusal, write_reference
from quality_fixtures import healthy_plan

from document_quality import adjudicate, read_json
from evaluate_document_quality import evaluate, fidelity, read_bundle, replay
from quality_collection import BundleChecks, collect, delivered_path
from quality_execution import execution_evidence
from quality_replay import ReplayObservation


class QualityEffectsTests(unittest.TestCase):
    """All unsuccessful roles remain reported and ambiguous effects are never replayed."""

    def test_refused_failed_and_missing_roles_all_survive_without_replay(self) -> None:
        """Known committed effects survive delivery failure alongside successful peer evidence."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            page = plan["protocol"]["pages"][0]
            assessment = plan["assessments"][0]
            assessment["candidate"] = refusal(root, page["commands"]["candidate"], "refused")
            assessment["baseline"] = refusal(
                root, page["commands"]["baseline"], "failed", exit_code=5, publication="completed"
            )
            with patch("evaluate_document_quality.replay") as replay_run:
                report = evaluate(plan, root, executable)
                replay_run.assert_not_called()
            roles = report["evidence"][0]["roles"]
            self.assertEqual(
                [roles[name]["outcome"] for name in ("candidate", "baseline", "replicate")],
                ["refused", "failed", "missing_evidence"],
            )
            self.assertEqual(roles["baseline"]["declared"]["execution"]["publication"], "completed")
            self.assertEqual(len(report["evidence"]), 2)
            self.assertEqual(report["decision"], "abstain")

    def test_incomplete_delivery_and_unknown_exit_cannot_establish_safe_retry(self) -> None:
        """Exit5/7, timeout and incomplete stdout retain unknown effects without execution."""
        for exit_code, timed_out in ((5, False), (7, False), (None, True), (4, False)):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                role = refusal(
                    root, ["binary", "process", "source", "--out-dir", "result", "--json"], "raw"
                )
                evidence = role["execution"]
                role["outcome"] = "failed"
                evidence.update(
                    exit_code=exit_code,
                    timed_out=timed_out,
                    response=None,
                    publication="unknown",
                    stdout=write_reference(root, "raw.stdout", b'{"partial":'),
                )
                execution_evidence(role, root, {})
                evidence["publication"] = "not_started"
                with (
                    self.subTest(exit_code=exit_code),
                    self.assertRaisesRegex(ValueError, "safe retry"),
                ):
                    execution_evidence(role, root, {})

    def test_complete_committed_response_survives_process_exit5_or7(self) -> None:
        """A confirmed commit is retained even when final command/delivery is unsuccessful."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            role = refusal(
                root,
                ["binary", "process", "source", "--out-dir", "result", "--json"],
                "raw",
                exit_code=5,
                publication="completed",
            )
            for code in (5, 7):
                role["execution"]["exit_code"] = code
                execution_evidence(role, root, {})
                role["execution"]["publication"] = "unknown"
                with self.assertRaisesRegex(ValueError, "confirmed publication"):
                    execution_evidence(role, root, {})
                role["execution"]["publication"] = "completed"

    def test_actual_exit_command_and_raw_response_must_agree(self) -> None:
        """Self-declared JSON success or matching stdout digest cannot change actual status."""
        for change in ("exit", "command", "stdout"):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                role = refusal(
                    root, ["binary", "process", "source", "--out-dir", "result", "--json"], "raw"
                )
                if change == "exit":
                    role["execution"]["exit_code"] = 3
                elif change == "command":
                    role["execution"]["response"]["command"] = "verify"
                    role["execution"]["stdout"] = write_reference(
                        root, "raw.stdout", role["execution"]["response"]
                    )
                else:
                    role["execution"]["stdout"] = write_reference(
                        root, "raw.stdout", {"different": "response"}
                    )
                with self.subTest(change=change), self.assertRaises((ValueError, ValidationError)):
                    execution_evidence(role, root, {})

    def test_prior_harm_and_uncertainty_are_retained_and_block_acceptance(self) -> None:
        """A new unanimous panel cannot delete the independently retained earlier judgment."""
        plan = healthy_plan()
        earlier = copy.deepcopy(plan["assessments"][0]["reviews"])
        earlier[0]["harms"] = ["lost_mark"]
        earlier[1]["uncertain"] = True
        report = adjudicate(plan, {"0": earlier})
        self.assertEqual(report["classes"]["faint_marks"]["harmed"], ["0"])
        self.assertEqual(report["decision"], "abstain")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            revision = {"sample": "0", "reviews": earlier}
            plan["assessments"][0]["review_revisions"] = [
                write_reference(root, "revision.json", revision)
            ]
            report = evaluate(plan, root, executable)
            self.assertEqual(report["evidence"][0]["prior_reviews"], [revision])
            self.assertEqual(
                report["evidence"][0]["review_revisions"],
                plan["assessments"][0]["review_revisions"],
            )
            self.assertEqual(plan["assessments"][0]["reviews"], [])

    def test_equal_count_different_masks_refuse_integer_fidelity(self) -> None:
        """Mask identity equality includes actual identities, not just protected population."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for name in ("c", "b"):
                (root / name).mkdir()
                (root / name / "result.png").write_bytes(
                    continuous_fixtures.Fixture(1, 1, ((127,),)).encoded()
                )
            candidate: dict[str, Any] = {
                "record": {"run": "a"},
                "source": {},
                "build": {},
                "output": {},
                "request": {
                    "rotation_degrees": 0,
                    "operation": {"kind": "continuous", "parameters": {}},
                },
                "execution": {
                    name: {"method": None}
                    for name in (
                        "illumination",
                        "denoising",
                        "restoration",
                        "contrast",
                        "sharpening",
                    )
                },
                "protection": {"protected_samples": 1, "sha256": "a" * 64},
            }
            replicate = copy.deepcopy(candidate)
            replicate["record"]["run"] = "b"
            baseline = copy.deepcopy(candidate)
            baseline["protection"]["sha256"] = "b" * 64
            with self.assertRaisesRegex(ValueError, "protection masks differ"):
                fidelity(
                    root,
                    {"candidate": "c", "baseline": "b", "replicate": "r"},
                    {"candidate": candidate, "baseline": baseline, "replicate": replicate},
                )

    def test_numerical_failure_cannot_be_relabelled_as_applicability_refusal(self) -> None:
        """Shared exit4 does not erase the distinction between numerical failure and admission."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            role = refusal(
                root, ["binary", "process", "source", "--out-dir", "result", "--json"], "raw"
            )
            role["execution"]["response"]["error"]["code"] = "E_NUMERICAL"
            role["execution"]["stdout"] = write_reference(
                root, "raw.stdout", role["execution"]["response"]
            )
            with self.assertRaisesRegex(ValueError, "Refusal requires"):
                execution_evidence(role, root, {})
            role["outcome"] = "failed"
            execution_evidence(role, root, {})

    def test_bundle_namespace_loss_does_not_abort_later_roles_or_pages(self) -> None:
        """A stat refusal after completed checks becomes one missing-evidence observation."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan = healthy_plan(2)
            checks = BundleChecks(
                Path("binary"),
                {},
                lambda *_args: {},
                lambda *_args: ReplayObservation({}, {"argv": []}),
                lambda *_args: None,
            )
            with (
                patch("quality_collection.execution_evidence"),
                patch(
                    "quality_collection.completed_record", return_value=({}, {}, root / "vanished")
                ),
            ):
                assessed, evidence = collect(plan, root, checks)
            self.assertEqual(len(evidence), 2)
            for page in evidence:
                for role in page["roles"].values():
                    self.assertEqual(role["outcome"], "missing_evidence")
                    self.assertEqual(role["declared"]["outcome"], "completed")
                    self.assertTrue(role["errors"])
            self.assertEqual(adjudicate(assessed)["decision"], "abstain")

    def test_actual_signal_and_windows_abnormal_status_are_preserved(self) -> None:
        """Signal and Windows status integers retain unknown or confirmed committed effects."""
        for status in (-9, 3221225477, 254):
            for committed in (False, True):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary).resolve()
                    role = refusal(
                        root,
                        ["binary", "process", "source", "--out-dir", "result", "--json"],
                        "raw",
                        exit_code=5 if committed else 4,
                        publication="completed" if committed else "not_started",
                    )
                    role["outcome"] = "failed"
                    role["execution"]["exit_code"] = status
                    role["execution"]["publication"] = "completed" if committed else "unknown"
                    execution_evidence(role, root, {})
                    self.assertEqual(role["execution"]["exit_code"], status)

    def test_delivered_path_keeps_dot_and_repeated_separator_identity_bytes(self) -> None:
        """The wire identity follows product string composition rather than Path normalization."""
        self.assertEqual(delivered_path("./result//", "result.png"), "./result///result.png")
        with patch("quality_collection.os.name", "nt"):
            self.assertEqual(delivered_path(".\\result\\", "result.png"), ".\\result\\\\result.png")

    def test_actual_verifier_and_replay_cannot_accept_forged_minimal_success(self) -> None:
        """Real subprocess output reaches full schemas rather than one success-like field."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            executable = Path(sys.executable)
            execute = subprocess.run
            value = {"publication": "completed", "establishes": "artifacts_agree_with_record"}
            code = f"print({json.dumps(json.dumps(value))})"

            def fake_response(
                command: list[str], **_kwargs: object
            ) -> subprocess.CompletedProcess[bytes]:
                self.assertIn(command[1], {"verify", "process"})
                return execute([sys.executable, "-c", code], capture_output=True, check=True)

            (root / "run.json").write_text("{}")
            source = root / "source.png"
            source.write_bytes(b"synthetic source")
            with (
                patch("evaluate_document_quality.subprocess.run", side_effect=fake_response),
                self.assertRaises(ValidationError),
            ):
                read_bundle(executable, root, "a" * 64)
            actual = replay(
                [str(executable), "process", "source.png", "--out-dir", "original", "--json"], root
            )
            self.assertIsNone(actual.record)
            self.assertEqual(actual.attempt["outcome"], "failed")
            self.assertTrue((root / actual.attempt["artifact_directory"]).is_dir())
            self.assertEqual(read_json((root / "run.json").read_bytes()), {})
