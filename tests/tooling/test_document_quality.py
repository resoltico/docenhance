# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""No-harm negative controls; synthetic reviews never establish natural-image benefit."""

from __future__ import annotations

import contextlib
import copy
import io
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import Any
from unittest.mock import Mock, patch

import tools_path

from jsonschema import ValidationError
from quality_accounting_fixture import factual_run, factual_slots
from quality_evidence_fixture import binary_record, component_inputs, disk_plan, successful_response
from quality_fixtures import freeze, healthy_plan

from document_quality import read_run_json
from evaluate_document_quality import evaluate, main
from quality_acquisition import confined
from quality_adjudication import adjudicate
from quality_attempt import CaptureAdmission, delivered_path
from quality_collection import BundleChecks
from quality_evidence import replay_options
from quality_fidelity import check_comparator, fidelity
from quality_json import canonical, frozen_digest
from quality_plan import validate_plan
from quality_replay import replay


class DocumentQualityTests(unittest.TestCase):
    """Faint ink loss cannot be averaged away, and untraceable evidence cannot score."""

    def test_per_class_harm_overrides_positive_average(self) -> None:
        """One material lost mark blocks every affected class despite nine positive pages."""
        plan = healthy_plan()
        self.assertEqual(adjudicate(plan)["decision"], "accepted")
        plan["assessments"][0]["reviews"][0]["harms"] = ["lost_mark"]
        report = adjudicate(plan)
        self.assertEqual(report["decision"], "abstain")
        self.assertEqual(report["classes"]["faint_marks"]["harmed"], ["0"])

    def test_uncertainty_disagreement_and_regression_abstain(self) -> None:
        """Neither uncertain nor dissenting panel observations imply measured improvement."""
        for mutation in ({"delta": -1}, {"uncertain": True}, {"delta": 0}):
            with self.subTest(mutation=mutation):
                plan = healthy_plan()
                plan["assessments"][0]["reviews"][0].update(mutation)
                self.assertEqual(adjudicate(plan)["decision"], "abstain")

    def test_provenance_split_and_complete_panel_are_mandatory(self) -> None:
        """Missing permission, frozen split drift, duplicate bytes and decorative reviews refuse."""
        self.assertTrue(tools_path.ROOT.is_dir())
        for change in ("permission", "split", "identity", "reviewer", "coverage", "panel", "pair"):
            with self.subTest(change=change):
                plan = copy.deepcopy(healthy_plan())
                if change == "permission":
                    del plan["protocol"]["corpus"][0]["permission"]
                elif change == "split":
                    plan["protocol"]["corpus"][0]["split"] = "tuning"
                elif change == "identity":
                    plan["protocol"]["corpus"][0]["sha256"] = plan["protocol"]["corpus"][1][
                        "sha256"
                    ]
                    plan["protocol"]["frozen_corpus_sha256"] = frozen_digest(
                        plan["protocol"]["corpus"]
                    )
                elif change == "reviewer":
                    plan["assessments"][0]["reviews"][0]["reviewer"] = "invented"
                elif change == "coverage":
                    plan["assessments"].pop()
                elif change == "panel":
                    plan["protocol"]["panel"][0]["independent"] = False
                else:
                    plan["assessments"][0]["reviews"][0]["blind_pair"] = "different"
                with self.assertRaises((ValueError, ValidationError)):
                    validate_plan(plan)

    def test_small_or_missing_class_cannot_establish_benefit(self) -> None:
        """Synthetic positive scores with insufficient independent pages still abstain."""
        plan = healthy_plan()
        plan["protocol"]["corpus"] = plan["protocol"]["corpus"][:1]
        plan["assessments"] = plan["assessments"][:1]
        plan["protocol"]["pages"] = plan["protocol"]["pages"][:1]
        freeze(plan)
        self.assertEqual(adjudicate(plan)["decision"], "abstain")

    def test_derived_physical_page_cannot_leak_or_inflate_holdout(self) -> None:
        """Re-encoded/cropped bytes remain the same physical document across partitions."""
        for split in ("heldout", "tuning"):
            plan = healthy_plan()
            plan["protocol"]["corpus"][1]["document_id"] = plan["protocol"]["corpus"][0][
                "document_id"
            ]
            plan["protocol"]["corpus"][1]["split"] = split
            freeze(plan)
            with self.assertRaisesRegex(ValueError, "physical document"):
                validate_plan(plan)

    def test_comparator_and_replay_cannot_change_processing_scope(self) -> None:
        """Changed alpha policy, output redirection or escaped kernel paths refuse scoring."""
        candidate: dict[str, Any] = {
            "source": {},
            "build": {},
            "assets": [],
            "request": {
                "output": {"mode": "gray", "depth": "16", "alpha": "white"},
                "rotation": 0,
                "protection": None,
                **dict.fromkeys(
                    (
                        "illumination",
                        "denoising",
                        "restoration",
                        "contrast",
                        "sharpening",
                        "binarization",
                    )
                ),
            },
            "execution": {
                "interpretation": {},
                **dict.fromkeys(
                    (
                        "illumination",
                        "denoising",
                        "restoration",
                        "contrast",
                        "sharpening",
                        "binarization",
                    )
                ),
            },
        }
        check_comparator(candidate, candidate)
        baseline = copy.deepcopy(candidate)
        baseline["request"]["output"]["alpha"] = "black"
        with self.assertRaisesRegex(ValueError, "Representation policies differ"):
            check_comparator(candidate, baseline)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            replay_options(["--contrast", "gamma"], root)
            for options in (["--out-dir", "foreign"], ["--help"], ["--psf-file", "../outside"]):
                with self.subTest(options=options), self.assertRaises(ValueError):
                    replay_options(options, root)

    def test_real_boundary_rejects_executable_and_source_tampering(self) -> None:
        """Changed executable refuses before effects; escaped/link sources cannot be acquired."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            executable.write_bytes(b"unreviewed binary")
            before = set(root.iterdir())
            with patch("quality_capture.owned_process") as launch:
                with self.assertRaisesRegex(ValueError, "Executable differs"):
                    evaluate(plan, root, executable, root / "quality-run.json")
                launch.assert_not_called()
            self.assertFalse((root / "quality-run.json").exists())
            self.assertEqual(set(root.iterdir()), before)
            with self.assertRaisesRegex(ValueError, "escapes"):
                confined(root, "../outside.png")
            link = root / "redirect"
            link.symlink_to(executable)
            with self.assertRaisesRegex(ValueError, "symbolic links"):
                confined(root, "redirect")

    def test_artifact_agreement_cannot_invent_the_comparator(self) -> None:
        """Pure receiving controls: verified bytes still cannot replace frozen replay semantics."""
        for change in (None, "source", "build", "request", "execution", "assets", "output"):
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                scored = binary_record("a" * 32)
                # A canonical mask DTO is present in both records; equal population is insufficient.
                scored["request"]["protection"] = {"supplied": {"sha256": "d" * 64, "bytes": 70}}
                scored["assets"] = [
                    {
                        "role": "protection_mask",
                        "path": "assets/protect-mask.png",
                        "sha256": "d" * 64,
                        "bytes": 70,
                        "width": 2,
                        "height": 1,
                        "frame": "metadata_oriented_source",
                        "protected_samples": 1,
                    }
                ]
                repeated = copy.deepcopy(scored)
                repeated["record"]["run"] = "b" * 32
                if change == "source":
                    repeated["source"]["sha256"] = "f" * 64
                elif change == "build":
                    repeated["build"]["compiler"] = "another compiler"
                elif change == "request":
                    repeated["request"]["binarization"]["parameters"]["threshold"] = 0.25
                elif change == "execution":
                    repeated["execution"]["interpretation"]["clipped_components"] = 1
                elif change == "assets":
                    repeated["assets"][0]["sha256"] = "e" * 64
                elif change == "output":
                    repeated["output"]["sha256"] = "e" * 64
                command = ["selected", "process", "source", "--out-dir", "original", "--json"]
                slots = factual_slots(0, command)
                directory = root / slots[1]["planned_target"]
                directory.mkdir(parents=True)
                response = successful_response(directory, repeated)
                response["output"] = delivered_path(slots[1]["planned_target"], "result.png")
                pages = factual_run(1)["report"]["evidence"]
                # Only the coherent invocation/verified-record receiving boundary is substituted;
                # no complete run, native descendant proof, producer or accepted role is claimed.
                consume = Mock(return_value={"outcome": "completed", "response": response})
                run = SimpleNamespace(
                    root=root,
                    slots=slots,
                    pages=pages,
                    invoke=consume,
                    admission=CaptureAdmission(),
                )
                verified = Mock(return_value=repeated)
                checks = BundleChecks(run, scored["build"], verified, replay, fidelity)
                observed = replay(checks, 0, scored["source"]["sha256"], scored)
                consume.assert_called_once_with(1)
                verified.assert_called_once_with(checks, 2, scored["source"]["sha256"], response)
                if change is None:
                    self.assertEqual(observed, repeated)
                else:
                    self.assertIsNone(observed)
                    role = pages[0]["roles"]["candidate"]
                    self.assertEqual(role["outcome"], "missing_evidence")
                    self.assertTrue(any(role["validation"].values()))
                    self.assertFalse(run.admission.is_open)

    def test_report_publication_cannot_overwrite_source_or_prior_evidence(self) -> None:
        """Actual CLI/writer publishes an abstention once and preserves a colliding file."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, executable = disk_plan(root)
            manifest, output = root / "manifest.json", root / "quality-run.json"
            original_manifest = canonical(plan)
            manifest.write_bytes(original_manifest)
            command = [
                "evaluate",
                "--manifest",
                str(manifest),
                "--executable",
                str(executable),
                "--out",
                str(output),
            ]
            with patch.object(sys, "argv", command), contextlib.redirect_stderr(io.StringIO()):
                with patch("quality_capture.owned_process") as launch:
                    self.assertEqual(main(), 1)
                    launch.assert_not_called()
                self.assertFalse(output.exists())
                # Component admission has no native provenance and cannot launch children.
                inputs = component_inputs(plan, root, executable)
                with (
                    patch("evaluate_document_quality.admit_evaluation", return_value=inputs),
                    patch("quality_capture.owned_process") as launch,
                ):
                    self.assertEqual(main(), 1)
                    original = output.read_bytes()
                    value = read_run_json(original)
                    self.assertEqual(value["kind"], "quality_run")
                    self.assertEqual(value["report"]["decision"], "abstain")
                    self.assertEqual(value["terminal"]["event_count"], 42)
                    self.assertEqual(main(), 1)
                    self.assertEqual(output.read_bytes(), original)
                    self.assertEqual(manifest.read_bytes(), original_manifest)
                    launch.assert_not_called()

    def test_unanimous_tie_cannot_hide_in_large_positive_class(self) -> None:
        """Eleven gains and one certain tie pass the sign bound but not the benefit contract."""
        plan = healthy_plan(12)
        self.assertEqual(adjudicate(plan)["decision"], "accepted")
        for review in plan["assessments"][0]["reviews"]:
            review["delta"] = 0
        result = adjudicate(plan)
        self.assertLess(result["classes"]["faint_marks"]["sign_tail"], 0.05 / 13)
        self.assertFalse(result["classes"]["faint_marks"]["accepted"])
        self.assertTrue(result["classes"]["already_good"]["accepted"])

    def test_replicate_aliases_and_wrong_build_cannot_prove_repeatability(self) -> None:
        """Aliased paths, identical runs and different source/mask/build facts refuse evidence."""
        for prefix in ("", "./"):
            plan = healthy_plan()
            item = plan["assessments"][0]
            item["replicate"]["bundle"] = prefix + item["candidate"]["bundle"]
            with self.assertRaisesRegex(ValueError, "distinct bundle directories"):
                validate_plan(plan)
        candidate: dict[str, Any] = {
            field: {} for field in ("source", "request", "execution", "assets", "output", "build")
        }
        candidate["record"] = {"run": "a"}
        for change in ("run", "build", "assets", "source"):
            replicate = copy.deepcopy(candidate)
            replicate["record"]["run"] = "b"
            message = "independently" if change == "run" else "Repeated processing differs"
            if change == "run":
                replicate["record"]["run"] = "a"
            else:
                replicate[change] = {"identity": "different"}
            with self.assertRaisesRegex(ValueError, message):
                fidelity(
                    Path(),
                    {},
                    {"candidate": candidate, "baseline": candidate, "replicate": replicate},
                )
