# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent frozen-protocol controls use first-party synthetic bytes and assertions only."""

from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path, PurePosixPath, PureWindowsPath
from unittest.mock import patch

from jsonschema import Draft202012Validator, ValidationError
from quality_evidence_fixture import component_inputs, disk_plan, write_reference
from quality_fixtures import freeze, healthy_plan

from document_quality import read_run_json, validate_run
from evaluate_document_quality import evaluate, read_bundle
from quality_adjudication import adjudicate
from quality_collection import BundleChecks, collect
from quality_evidence import admit, evidence_error, replay_options
from quality_fidelity import fidelity
from quality_inputs import FrozenEvidence
from quality_json import frozen_digest, read_json
from quality_plan import validate_plan
from quality_protocol import validate_work_index
from quality_replay import replay
from quality_report import report as factual_report
from quality_run import QualityRun
from quality_schema import validator
from quality_work import invocation_index


class QualityProtocolTests(unittest.TestCase):
    """Drift refuses before collection; missing role evidence gives factual abstention."""

    def test_real_asset_admission_and_complete_missing_evidence_report(self) -> None:
        """Different per-page masks/PSFs pass admission without inventing human quality."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            validate_plan(plan)
            self.assertNotEqual(
                plan["protocol"]["pages"][0]["assets"], plan["protocol"]["pages"][1]["assets"]
            )
            run = QualityRun(component_inputs(plan, root, executable), root / "quality-run.json")
            try:
                checks = BundleChecks(run, {}, read_bundle, replay, fidelity)
                with patch("quality_capture.owned_process") as launch:
                    pages = collect(plan, checks)
                    launch.assert_not_called()
                report = factual_report(
                    plan, plan["protocol"]["executable_sha256"], pages, run.errors, {}
                )
                value = run.finish(report)
            finally:
                run.close()
            validate_run(value)
            self.assertEqual(read_run_json((root / "quality-run.json").read_bytes()), value)
            validator("report").validate(report)
            self.assertEqual(report["decision"], "abstain")
            self.assertEqual(len(report["evidence"]), 2)
            for page in report["evidence"]:
                self.assertEqual(set(page["roles"]), {"candidate", "baseline", "replicate"})
                self.assertTrue(
                    all(role["outcome"] == "missing_evidence" for role in page["roles"].values())
                )
            digest_report = report.pop("report_sha256")
            self.assertEqual(frozen_digest(report), digest_report)

    def test_planned_streams_preserve_exact_wire_spelling_on_both_path_models(self) -> None:
        """Native path formatting cannot rewrite frozen slash-delimited capture identities."""
        plan = healthy_plan()
        slots = invocation_index(plan)
        slots[0]["planned_directory"] = "qualité/墨"
        for stream in ("stdout", "stderr"):
            slots[0]["planned_" + stream] = "qualité/墨/" + stream
        for path_type in (PurePosixPath, PureWindowsPath):
            with self.subTest(path_type=path_type), patch("quality_protocol.Path", path_type):
                validate_work_index(plan, slots)
                for stream in ("stdout", "stderr"):
                    key = "planned_" + stream
                    original = slots[0][key]
                    for wrong in (
                        original.replace("/", "\\"),
                        original + "/extra",
                        "../foreign/" + stream,
                        "/outside/" + stream,
                        "X:" + original,
                        original.replace("/" + stream, "/./" + stream),
                        original.upper(),
                    ):
                        changed = copy.deepcopy(slots)
                        changed[0][key] = wrong
                        with (
                            self.subTest(stream=stream, wrong=wrong),
                            self.assertRaisesRegex(ValueError, "Planned stream escaped"),
                        ):
                            validate_work_index(plan, changed)

    def test_protocol_changes_cannot_be_hidden_by_corpus_identity(self) -> None:
        """Options, source, comparator, build, assets, panel and assignment share one freeze."""
        changes = ("options", "source", "comparator", "build", "assets", "panel", "assignment")
        for change in changes:
            plan = healthy_plan(2)
            protocol = plan["protocol"]
            if change == "options":
                protocol["pages"][0]["commands"]["candidate"].insert(-1, "--gamma")
            elif change == "source":
                protocol["corpus"][0]["sha256"] = "c" * 64
            elif change == "comparator":
                protocol["pages"][0]["commands"]["baseline"][4] = "other"
            elif change == "build":
                protocol["build_provenance"]["sha256"] = "c" * 64
            elif change == "assets":
                protocol["pages"][0]["assets"] = [{"path": "mask", "sha256": "c" * 64}]
            elif change == "panel":
                protocol["panel"][0]["id"] = "different"
            else:
                protocol["assignment"]["sha256"] = "c" * 64
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_plan(plan)

    def test_actual_source_and_asset_byte_drift_refuse_before_collection(self) -> None:
        """Valid frozen JSON cannot authorize changed source, mask or PSF bytes."""
        for field in ("source", "mask", "psf", "build", "assignment", "receipt", "executable"):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                plan, executable = disk_plan(root)
                paths = {
                    "source": "0.png",
                    "mask": "mask-0.png",
                    "psf": "psf-0.png",
                    "build": "build.json",
                    "assignment": "assignment.json",
                    "receipt": "receipt.json",
                    "executable": "binary",
                }
                (root / paths[field]).write_bytes(b"drift")
                with (
                    self.subTest(field=field),
                    patch("quality_capture.owned_process") as launch,
                    self.assertRaises((OSError, ValueError)),
                ):
                    evaluate(plan, root, executable, root / "quality-run.json")
                self.assertFalse((root / "quality-run.json").exists())
                self.assertFalse(any(root.glob("quality-run-*")))
                launch.assert_not_called()

    def test_independent_receipt_and_assignment_contents_bind_exact_protocol(self) -> None:
        """Matching file hashes do not make a wrong receipt or assignment coherent."""
        for field in ("receipt", "assignment"):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                plan, executable = disk_plan(root)
                if field == "receipt":
                    plan["preregistration"] = write_reference(
                        root, "receipt.json", {"protocol_sha256": "c" * 64}
                    )
                else:
                    plan["protocol"]["assignment"] = write_reference(
                        root, "assignment.json", {"pairs": []}
                    )
                    freeze(plan)
                    plan["preregistration"] = write_reference(
                        root, "receipt.json", {"protocol_sha256": plan["protocol_sha256"]}
                    )
                with self.subTest(field=field), self.assertRaises(ValueError):
                    admit(plan, root, executable, replay_options, FrozenEvidence(plan, root))

    def test_strict_json_and_obsolete_protocol_shapes_are_refused(self) -> None:
        """Duplicate decoded keys, nonfinite values and invalid Unicode never acquire identities."""
        invalid = (
            b'{"x":1,"x":2}',
            b'{"x":1,"\\u0078":2}',
            b'{"x":NaN}',
            b'{"x":Infinity}',
            b'{"x":1e10000}',
            b'{"x":"\\ud800"}',
            b'{"x":"\xff"}',
            b"[]",
        )
        for raw in invalid:
            with self.subTest(raw=raw), self.assertRaises((ValueError, UnicodeError, TypeError)):
                read_json(raw)
        plan = healthy_plan()
        plan["protocol_version"] = 1
        self.assertFalse(validator("plan").is_valid(plan))
        self.assertEqual(read_json('{"\u0101": 1}'.encode()), {"\u0101": 1})

    def test_tuning_requires_cleared_inputs_and_their_bound_receipt(self) -> None:
        """Explicit no-tuning is valid; declared tuning cannot omit inputs or receipt identity."""
        plan = healthy_plan(2)
        validate_plan(plan)
        for receipt in (None, {"path": "receipt", "sha256": "a" * 64}):
            altered = copy.deepcopy(plan)
            altered["protocol"]["tuning"] = {"performed": True, "receipt": receipt}
            freeze(altered)
            with self.assertRaisesRegex(ValueError, "Tuning requires"):
                validate_plan(altered)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            sample = copy.deepcopy(plan["protocol"]["corpus"][0])
            sample.update(
                id="tuned", document_id="tuned", path="tuned.png", split="tuning", sha256="d" * 64
            )
            (root / "tuned.png").write_bytes(b"separate tuning source")
            sample["sha256"] = write_reference(root, "tuned.png", b"separate tuning source")[
                "sha256"
            ]
            plan["protocol"]["corpus"].append(sample)
            plan["protocol"]["tuning"] = {
                "performed": True,
                "receipt": write_reference(
                    root, "tuning.json", {"tuning_corpus_sha256": frozen_digest([sample])}
                ),
            }
            freeze(plan)
            plan["preregistration"] = write_reference(
                root, "receipt.json", {"protocol_sha256": plan["protocol_sha256"]}
            )
            validate_plan(plan)
            admit(plan, root, executable, replay_options, FrozenEvidence(plan, root))
            plan["protocol"]["tuning"]["receipt"] = write_reference(
                root, "tuning.json", {"tuning_corpus_sha256": "a" * 64}
            )
            with self.assertRaisesRegex(ValueError, "Tuning receipt"):
                admit(plan, root, executable, replay_options, FrozenEvidence(plan, root))

    def test_derived_schema_error_is_concise_without_changing_raw_evidence(self) -> None:
        """Derived prose changes; byte-identified declared evidence remains intact."""
        schema = {
            "type": "object",
            "description": "large reviewed description" * 10000,
            "properties": {"verified": {"const": True}},
        }
        value = {"verified": "completed"}
        original = copy.deepcopy(value)
        with self.assertRaises(ValidationError) as refused:
            Draft202012Validator(schema).validate(value)
        self.assertEqual(
            evidence_error(refused.exception), "JSON schema refusal at /verified (rule const)"
        )
        self.assertEqual(value, original)

    def test_control_minimum_full_universe_and_synthetic_abstention(self) -> None:
        """Synthetic benefit/no-op claims and undersized no-harm controls always abstain."""
        self.assertEqual(adjudicate(healthy_plan(natural=False))["decision"], "abstain")
        one = adjudicate(healthy_plan(1))
        self.assertFalse(one["classes"]["already_good"]["accepted"])
        two = adjudicate(healthy_plan(2))
        self.assertTrue(two["classes"]["already_good"]["accepted"])
        plan = healthy_plan()
        plan["protocol"]["claim_policy"]["classes"].pop()
        freeze(plan)
        self.assertFalse(validator("plan").is_valid(plan))
        plan = healthy_plan()
        plan["protocol"]["pages"][1]["blind_pair"] = plan["protocol"]["pages"][0]["blind_pair"]
        freeze(plan)
        with self.assertRaisesRegex(ValueError, "blind pair"):
            validate_plan(plan)
