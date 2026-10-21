# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Command delivery, retained reviews and integer preservation cannot fabricate quality benefit."""

from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_evidence_fixture import (
    binary_record,
    component_inputs,
    continuous_fixtures,
    disk_plan,
    refusal,
    successful_response,
    write_reference,
)
from quality_fixtures import healthy_plan

from evaluate_document_quality import read_bundle
from quality_adjudication import adjudicate
from quality_attempt import CaptureAdmission, delivered_path, new_original
from quality_collection import BundleChecks, _directory, _reject, collect
from quality_execution import execution_evidence
from quality_fidelity import check_comparator, fidelity
from quality_replay import replay
from quality_report import report as factual_report
from quality_run import QualityRun

prepare_tool_imports()


EXPECTED_PAGE_COUNT = 2


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
                run = QualityRun(
                    component_inputs(plan, root, executable), root / "quality-run.json"
                )
                try:
                    checks = BundleChecks(run, {}, read_bundle, replay_run, fidelity)
                    evidence = collect(plan, checks)
                    result = factual_report(
                        plan, plan["protocol"]["executable_sha256"], evidence, run.errors, {}
                    )
                    report = run.finish(result)["report"]
                    replay_run.assert_not_called()
                finally:
                    run.close()
            roles = report["evidence"][0]["roles"]
            self.assertEqual(
                [roles[name]["outcome"] for name in ("candidate", "baseline", "replicate")],
                ["refused", "failed", "missing_evidence"],
            )
            self.assertEqual(roles["baseline"]["declared"]["execution"]["publication"], "completed")
            self.assertEqual(roles["baseline"]["original"]["observed_publication"], "completed")
            self.assertEqual((len(run.originals), len(run.finished)), (6, 18))
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
                role["outcome"] = "failed"
                role["execution"].update(
                    exit_code=exit_code,
                    timed_out=timed_out,
                    response=None,
                    publication="unknown",
                    stdout=write_reference(root, "raw.stdout", b'{"partial":'),
                )
                original, admission = new_original(0, role), CaptureAdmission()
                self.assertFalse(execution_evidence(original, root, admission))
                self.assertEqual(original["observed_publication"], "unknown")
                role["execution"]["publication"] = "not_started"
                changed = new_original(0, role)
                self.assertFalse(execution_evidence(changed, root, CaptureAdmission()))
                self.assertEqual(changed["observed_publication"], "unknown")
                self.assertIsNotNone(changed["errors"]["delivery"])

    def test_complete_committed_response_survives_process_exit5_or7(self) -> None:
        """Confirmed stdout retains the commit despite actual exit5/7 or later stderr failure."""
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
                original = new_original(0, role)
                self.assertTrue(execution_evidence(original, root, CaptureAdmission()))
                self.assertEqual(original["observed_publication"], "completed")
                changed = copy.deepcopy(role)
                changed["execution"]["publication"] = "unknown"
                observed = new_original(0, changed)
                self.assertFalse(execution_evidence(observed, root, CaptureAdmission()))
                self.assertEqual(observed["observed_publication"], "completed")
                self.assertIsNotNone(observed["errors"]["delivery"])
            (root / role["execution"]["stderr"]["path"]).unlink()
            observed = new_original(0, role)
            self.assertFalse(execution_evidence(observed, root, CaptureAdmission()))
            self.assertEqual(observed["observed_publication"], "completed")
            self.assertIsNotNone(observed["stderr"]["errors"]["stat"])

    def test_actual_exit_command_and_raw_response_must_agree(self) -> None:
        """Self-declared JSON or matching stdout digest cannot change actual command/status."""
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
                original, admission = new_original(0, role), CaptureAdmission()
                with self.subTest(change=change):
                    self.assertFalse(execution_evidence(original, root, admission))
                    self.assertIsNotNone(original["errors"]["delivery"])
                    self.assertFalse(admission.is_open)

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
            run = QualityRun(component_inputs(plan, root, executable), root / "quality-run.json")
            try:
                checks = BundleChecks(run, {}, read_bundle, replay, fidelity)
                evidence = collect(plan, checks)
                prior = {
                    page["sample"]: [
                        review
                        for revision in page["prior_reviews"]
                        for review in revision["reviews"]
                    ]
                    for page in evidence
                }
                result = factual_report(
                    plan, plan["protocol"]["executable_sha256"], evidence, run.errors, prior
                )
                report = run.finish(result)["report"]
            finally:
                run.close()
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
            masks = [
                continuous_fixtures.Fixture(2, 1, pixels).encoded()
                for pixels in (((1,), (0,)), ((0,), (1,)))
            ]
            identities = [
                write_reference(root, f"mask-{i}.png", data) for i, data in enumerate(masks)
            ]
            candidate = binary_record()
            candidate["request"]["protection"] = {
                "supplied": {"sha256": identities[0]["sha256"], "bytes": len(masks[0])}
            }
            candidate["assets"] = [
                {
                    "role": "protection_mask",
                    "path": "assets/protect-mask.png",
                    "sha256": identities[0]["sha256"],
                    "bytes": len(masks[0]),
                    "width": 2,
                    "height": 1,
                    "frame": "metadata_oriented_source",
                    "protected_samples": 1,
                }
            ]
            replicate = copy.deepcopy(candidate)
            replicate["record"]["run"] = "b" * 32
            baseline = copy.deepcopy(candidate)
            baseline["record"]["run"] = "c" * 32
            baseline["request"]["output"].update(mode="gray", depth="16")
            baseline["request"]["binarization"] = None
            baseline["execution"]["binarization"] = None
            baseline["execution"]["interpretation"]["output"]["bit_depth"] = 16
            baseline["output"].update(depth=16, profile_embedded=True)
            check_comparator(candidate, baseline)
            baseline["assets"][0].update(sha256=identities[1]["sha256"], bytes=len(masks[1]))
            # This cold identity refusal must precede any absent payload acquisition.
            with self.assertRaisesRegex(ValueError, "interpretation or protection differs"):
                fidelity(
                    root,
                    {"candidate": "c", "baseline": "b", "replicate": "r"},
                    {"candidate": candidate, "baseline": baseline, "replicate": replicate},
                )

    def test_numerical_failure_cannot_be_relabelled_as_applicability_refusal(self) -> None:
        """Shared exit4 does not erase numerical failure versus an admission refusal."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            role = refusal(
                root, ["binary", "process", "source", "--out-dir", "result", "--json"], "raw"
            )
            role["execution"]["response"]["error"]["code"] = "E_NUMERICAL"
            role["execution"]["stdout"] = write_reference(
                root, "raw.stdout", role["execution"]["response"]
            )
            refused = new_original(0, role)
            self.assertFalse(execution_evidence(refused, root, CaptureAdmission()))
            self.assertIsNotNone(refused["errors"]["delivery"])
            role["outcome"] = "failed"
            failed = new_original(0, role)
            self.assertTrue(execution_evidence(failed, root, CaptureAdmission()))
            self.assertEqual(failed["observed_publication"], "not_started")

    def test_bundle_namespace_loss_does_not_abort_later_roles_or_pages(self) -> None:
        """Actual namespace refusal retains every role without inventing qualified verification."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            destination = "candidate-0"
            directory = root / destination
            directory.mkdir()
            record = binary_record()
            response = successful_response(directory, record)
            response["output"] = delivered_path(destination, "result.png")
            command = plan["protocol"]["pages"][0]["commands"]["candidate"]
            plan["assessments"][0]["candidate"] = {
                "outcome": "completed",
                "bundle": destination,
                "execution": {
                    "argv": command,
                    "exit_code": 0,
                    "timed_out": False,
                    "response": response,
                    "publication": "completed",
                    "stdout": write_reference(root, "candidate.stdout", response),
                    "stderr": write_reference(root, "candidate.stderr", b""),
                },
            }
            run = QualityRun(component_inputs(plan, root, executable), root / "quality-run.json")
            checks = BundleChecks(run, {}, read_bundle, replay, fidelity)
            try:
                candidate = run.pages[0]["roles"]["candidate"]
                _directory(checks, candidate, set())
                directory.rename(root / "retained-vanished-bundle")
                with self.assertRaises(OSError) as failure:
                    _directory(checks, candidate, set())
                _reject(checks, candidate, "role_identity", failure.exception)
                with patch("quality_capture.owned_process") as launch:
                    evidence = collect(plan, checks)
                    launch.assert_not_called()
                self.assertEqual(len(evidence), 2)
                candidate = evidence[0]["roles"]["candidate"]
                self.assertEqual(candidate["outcome"], "missing_evidence")
                self.assertIsNotNone(candidate["validation"]["role_identity"])
                self.assertEqual(candidate["original"]["observed_publication"], "completed")
                self.assertEqual(len(run.finished), 18)
                self.assertEqual(len(run.originals), 6)
                self.assertFalse(run.admission.is_open)
                self.assertTrue((root / "retained-vanished-bundle" / "run.json").is_file())
                result = factual_report(
                    plan, plan["protocol"]["executable_sha256"], evidence, run.errors, {}
                )
                value = run.finish(result)
                self.assertEqual(value["report"]["decision"], "abstain")
                self.assertTrue(
                    all(item["count"] == EXPECTED_PAGE_COUNT for item in result["classes"].values())
                )
            finally:
                run.close()

    def test_actual_signal_and_windows_abnormal_status_are_preserved(self) -> None:
        """Actual status integers retain unknown or confirmed completed effects unchanged."""
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
                    role["execution"].update(
                        exit_code=status, publication="completed" if committed else "unknown"
                    )
                    original = new_original(0, role)
                    execution_evidence(original, root, CaptureAdmission())
                    self.assertEqual(original["declared"]["execution"]["exit_code"], status)
                    self.assertEqual(
                        original["observed_publication"], "completed" if committed else "unknown"
                    )

    def test_delivered_path_keeps_dot_and_repeated_separator_identity_bytes(self) -> None:
        """The wire identity follows product string composition rather than Path normalization."""
        self.assertEqual(delivered_path("./result//", "result.png"), "./result///result.png")
        with patch("quality_attempt.os.name", "nt"):
            self.assertEqual(delivered_path(".\\result\\", "result.png"), ".\\result\\\\result.png")
