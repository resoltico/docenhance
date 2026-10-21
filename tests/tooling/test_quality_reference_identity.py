# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Frozen references and original responses use the bytes of their one hashed acquisition."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import Any
from unittest.mock import Mock, patch

from tools_path import prepare_tool_imports

from quality_accounting_fixture import unreached_fidelity, unreached_replay
from quality_evidence_fixture import (
    binary_record,
    disk_plan,
    refusal,
    successful_response,
    write_reference,
)

from evaluate_document_quality import read_bundle
from quality_acquisition import acquire_reference
from quality_adjudication import adjudicate
from quality_attempt import MAX_RESPONSE_BYTES, CaptureAdmission, delivered_path, new_original
from quality_collection import BundleChecks, frozen_processing_assets
from quality_execution import coherent_delivery, execution_evidence
from quality_inputs import FrozenEvidence
from quality_json import read_json

prepare_tool_imports()


class QualityReferenceIdentityTests(unittest.TestCase):
    """Later path changes cannot replace frozen dissent with favorable parsed evidence."""

    def test_verified_record_identity_is_observed_before_same_bytes_are_parsed(self) -> None:
        """Pure receiving control uses the native producer's result-then-record identity shape."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            directory = root / "candidate"
            directory.mkdir()
            record = binary_record()
            delivered = successful_response(directory, record)
            delivered["output"] = delivered_path("candidate", "result.png")
            payload = (directory / "run.json").read_bytes()
            identity = {
                "path": "run.json",
                "sha256": hashlib.sha256(payload).hexdigest(),
                "bytes": len(payload),
            }
            observed = {
                "schema_version": 15,
                "command": "verify",
                "exit_code": 0,
                "version": record["build"]["application_version"],
                "directory": "candidate",
                "run": record["record"]["run"],
                "recorded": record["record"]["recorded"],
                "establishes": "artifacts_agree_with_record",
                "confirmed": [
                    {key: record["output"][key] for key in ("path", "sha256", "bytes")},
                    identity,
                ],
            }
            consume = Mock(return_value={"outcome": "completed", "response": observed})
            run = SimpleNamespace(
                root=root, slots=[{"planned_target": "candidate"}], invoke=consume
            )
            checks = BundleChecks(
                run, record["build"], read_bundle, unreached_replay, unreached_fidelity
            )
            self.assertEqual(read_bundle(checks, 0, record["source"]["sha256"], delivered), record)
            consume.assert_called_once_with(0)
            for compiler in ("another valid compiler", "different"):
                changed = json.loads(payload)
                changed["build"]["compiler"] = compiler
                replacement = json.dumps(changed).encode()
                self.assertEqual(len(replacement) == len(payload), compiler == "different")
                self.assertEqual(changed["record"], record["record"])
                self.assertEqual(changed["output"], record["output"])
                self.assertNotEqual(hashlib.sha256(replacement).hexdigest(), identity["sha256"])
                (directory / "run.json").write_bytes(replacement)
                consume.reset_mock()
                with (
                    patch(
                        "evaluate_document_quality.read_json",
                        side_effect=AssertionError("unbound parse"),
                    ),
                    self.assertRaisesRegex(ValueError, "declared"),
                ):
                    read_bundle(checks, 0, record["source"]["sha256"], delivered)
                consume.assert_called_once_with(0)

    def test_parsed_prior_harm_is_the_exact_frozen_acquisition(self) -> None:
        """Parsed prior harm is the exact frozen acquisition."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, _ = disk_plan(root)
            assessment = plan["assessments"][0]
            adverse = copy_revision(plan, assessment, harm=True)
            positive = copy_revision(plan, assessment, harm=False)
            reference = write_reference(root, "prior.json", adverse)
            assessment["review_revisions"] = [reference]
            calls = []

            def acquired(directory: Path, ref: dict[str, Any]) -> object:
                value = acquire_reference(directory, ref)
                if ref == reference:
                    calls.append(ref)
                    (root / ref["path"]).write_text(json.dumps(positive))
                return value

            with patch("quality_inputs.acquire_reference", acquired):
                snapshots = FrozenEvidence(plan, root)
            self.assertEqual(calls, [reference])
            self.assertEqual(snapshots.prior[assessment["sample"]], [adverse])
            self.assertEqual(snapshots.value(reference), adverse)
            self.assertNotEqual(
                hashlib.sha256((root / reference["path"]).read_bytes()).hexdigest(),
                reference["sha256"],
            )

    def test_changed_metadata_is_refused_before_parsing(self) -> None:
        """Changed metadata is refused before parsing."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            reference = write_reference(root, "receipt.json", {"protocol_sha256": "original"})
            (root / reference["path"]).write_bytes(b'{"protocol_sha256":"changed"}')
            with (
                patch("quality_acquisition.read_json", side_effect=AssertionError("unbound parse")),
                self.assertRaisesRegex(ValueError, "declared"),
            ):
                acquire_reference(root, reference)

    def test_earlier_harm_survives_later_reference_schema_or_io_failure(self) -> None:
        """Earlier harm survives later reference schema or io failure."""
        for failure in ("hash", "schema", "io", "panel"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                plan, _ = disk_plan(root)
                assessment = plan["assessments"][0]
                harmful = copy_revision(plan, assessment, harm=True)
                second = copy_revision(plan, assessment, harm=False)
                if failure == "schema":
                    second = {"sample": assessment["sample"]}
                elif failure == "panel":
                    second["reviews"][0]["reviewer"] = "outside-frozen-panel"
                references = [
                    write_reference(root, f"revision-{index}.json", value)
                    for index, value in enumerate((harmful, second))
                ]
                if failure == "hash":
                    references[1]["sha256"] = "0" * 64
                elif failure == "io":
                    (root / references[1]["path"]).unlink()
                assessment["review_revisions"] = references
                snapshots = FrozenEvidence(plan, root)
                self.assertEqual(snapshots.prior[assessment["sample"]], [harmful])
                self.assertIn(assessment["sample"], snapshots.prior_failures)
                self.assertEqual(assessment["review_revisions"], references)
                report = adjudicate(plan, {assessment["sample"]: harmful["reviews"]})
                self.assertIn(assessment["sample"], report["classes"]["faint_marks"]["harmed"])
                self.assertEqual(report["decision"], "abstain")

    def test_healthy_multiple_revisions_remain_in_declared_order(self) -> None:
        """Healthy multiple revisions remain in declared order."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, _ = disk_plan(root)
            assessment = plan["assessments"][0]
            values = [
                copy_revision(plan, assessment, harm=True),
                copy_revision(plan, assessment, harm=False),
            ]
            assessment["review_revisions"] = [
                write_reference(root, f"revision-{index}.json", value)
                for index, value in enumerate(values)
            ]
            snapshots = FrozenEvidence(plan, root)
            self.assertEqual(snapshots.prior[assessment["sample"]], values)
            self.assertNotIn(assessment["sample"], snapshots.prior_failures)

    def test_original_stdout_parse_uses_hashed_bytes_and_keeps_existing_limit(self) -> None:
        """Original stdout parse uses hashed bytes and keeps existing limit."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            declared = refusal(
                root,
                ["binary", "process", "source", "--out-dir", "out", "--json"],
                "original",
                exit_code=5,
                publication="completed",
            )
            reference = declared["execution"]["stdout"]
            path = root / reference["path"]
            payload = path.read_bytes()

            def parse(raw: bytes) -> dict[str, Any]:
                self.assertEqual(raw, payload)
                path.write_bytes(b'{"publication":"not_started"}')
                return read_json(raw)

            original = new_original(0, declared)
            with patch("quality_execution.read_json", side_effect=parse):
                self.assertTrue(execution_evidence(original, root, CaptureAdmission()))
            self.assertEqual(original["observed_publication"], "completed")
            changed = new_original(0, declared)
            self.assertFalse(execution_evidence(changed, root, CaptureAdmission()))
            self.assertEqual(changed["observed_publication"], "unknown")
            oversized = payload + b" " * MAX_RESPONSE_BYTES
            reference.update(write_reference(root, path.name, oversized))
            original = new_original(0, declared)
            with patch.object(Path, "read_bytes", side_effect=AssertionError("unbounded raw read")):
                self.assertFalse(execution_evidence(original, root, CaptureAdmission()))
            self.assertIsNone(original["response"])
            self.assertEqual(path.read_bytes(), oversized)

    def test_confirmed_commit_survives_missing_or_invalid_stderr_reference(self) -> None:
        """Confirmed commit survives missing or invalid stderr reference."""
        for failure in ("missing", "hash"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                declared = refusal(
                    root,
                    ["binary", "process", "source", "--out-dir", "out", "--json"],
                    "committed",
                    exit_code=5,
                    publication="completed",
                )
                if failure == "missing":
                    (root / declared["execution"]["stderr"]["path"]).unlink()
                else:
                    declared["execution"]["stderr"]["sha256"] = "0" * 64
                original, admission = new_original(0, declared), CaptureAdmission()
                self.assertFalse(execution_evidence(original, root, admission))
                self.assertFalse(admission.is_open)
                self.assertEqual(original["observed_publication"], "completed")

    def test_known_error_does_not_turn_unknown_or_completed_effects_into_refusal(self) -> None:
        """A diagnostic category cannot establish a safe refusal or erase a reported commit."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for effect in ("not_started", "not_published", "unknown", "completed"):
                declared = refusal(
                    root,
                    ["binary", "process", "source", "--out-dir", "out", "--json"],
                    "effect-" + effect,
                    exit_code=3,
                    publication=effect,
                )
                response = declared["execution"]["response"]
                response["error"]["code"] = "E_RESOURCE"
                declared["outcome"] = "refused"
                self.assertEqual(
                    coherent_delivery(declared, response, effect, ambiguous=False),
                    effect in {"not_started", "not_published"},
                )
                declared["outcome"] = "failed"
                self.assertEqual(
                    coherent_delivery(declared, response, effect, ambiguous=False),
                    effect in {"unknown", "completed"},
                )
                self.assertEqual(response["publication"], effect)

    def test_scored_supplied_mask_and_psf_bind_to_frozen_raw_asset_identities(self) -> None:
        """Scored supplied mask and psf bind to frozen raw asset identities."""
        mask_sha, kernel_sha = "a" * 64, "b" * 64
        references = [
            {"path": "mask.png", "sha256": mask_sha},
            {"path": "kernel.png", "sha256": kernel_sha},
        ]
        options = ["--protect-mask", "mask.png", "--psf-file", "kernel.png"]
        record: dict[str, Any] = {
            "request": {"protection": {"supplied": {"sha256": mask_sha}}},
            "assets": [
                {"role": "protection_mask", "sha256": "c" * 64},
                {"role": "psf_kernel", "sha256": kernel_sha},
            ],
        }
        frozen_processing_assets(record, options, references)
        record["request"]["protection"]["supplied"]["sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "differs from its frozen identity"):
            frozen_processing_assets(record, options, references)
        record["request"]["protection"]["supplied"]["sha256"] = mask_sha
        record["assets"][1]["sha256"] = "e" * 64
        with self.assertRaisesRegex(ValueError, "differs from its frozen identity"):
            frozen_processing_assets(record, options, references)
        record["assets"][1]["sha256"] = kernel_sha
        with self.assertRaisesRegex(ValueError, "absent from its frozen command"):
            frozen_processing_assets(record, [], references)
        frozen_processing_assets({"request": {"protection": None}, "assets": []}, [], [])


def copy_revision(
    plan: dict[str, Any], assessment: dict[str, Any], *, harm: bool
) -> dict[str, Any]:
    """Synthetic prior judgments bind to the frozen pair and actual declared panel."""
    page = next(
        page for page in plan["protocol"]["pages"] if page["sample"] == assessment["sample"]
    )
    reviews = [
        {
            "reviewer": person["id"],
            "blind_pair": page["blind_pair"],
            "delta": -1 if harm else 1,
            "harms": ["lost_mark"] if harm else [],
            "uncertain": False,
            "rationale": "synthetic retained revision control",
        }
        for person in plan["protocol"]["panel"]
    ]
    return {"sample": assessment["sample"], "reviews": reviews}
