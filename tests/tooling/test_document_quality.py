# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""No-harm negative controls; synthetic reviews never establish natural-image benefit."""

from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

import tools_path

from jsonschema import ValidationError
from quality_fixtures import freeze, healthy_plan

import evaluate_document_quality as evaluator
from document_quality import adjudicate, frozen_digest, validate_plan
from evaluate_document_quality import (
    check_comparator,
    evaluate,
    fidelity,
    main,
    replay_options,
)
from quality_collection import BundleChecks, collect
from quality_evidence import confined
from quality_replay import ReplayObservation


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
            "request": {"operation": {"kind": "continuous", "parameters": {"alpha": "white"}}}
        }
        check_comparator(candidate, candidate)
        baseline = copy.deepcopy(candidate)
        baseline["request"]["operation"]["parameters"]["alpha"] = "black"
        with self.assertRaisesRegex(ValueError, "Representation policies differ"):
            check_comparator(candidate, baseline)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            replay_options(["--contrast", "gamma"], root)
            for options in (["--out-dir", "foreign"], ["--help"], ["--psf-file", "../outside"]):
                with self.subTest(options=options), self.assertRaises(ValueError):
                    replay_options(options, root)

    def test_real_boundary_rejects_executable_and_source_tampering(self) -> None:
        """Good review JSON cannot substitute a different executable or escaped source identity."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            executable = root / "binary"
            executable.write_bytes(b"unreviewed binary")
            with self.assertRaisesRegex(ValueError, "Executable differs"):
                evaluate(healthy_plan(), root, executable)
            with self.assertRaisesRegex(ValueError, "escapes"):
                confined(root, "../outside.png")
            link = root / "redirect"
            link.symlink_to(executable)
            with self.assertRaisesRegex(ValueError, "symbolic links"):
                confined(root, "redirect")

    def test_artifact_agreement_cannot_invent_the_comparator(self) -> None:
        """A forged peer fails replay without erasing successes or later intended pages."""
        plan = healthy_plan(2)
        record: dict[str, Any] = {
            field: {}
            for field in ("source", "request", "execution", "protection", "output", "build")
        }
        record["record"] = {"run": "a" * 32}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for sample in plan["protocol"]["corpus"]:
                (root / sample["path"]).write_text(sample["id"])
            for page in plan["protocol"]["pages"]:
                for role in ("candidate", "baseline", "replicate"):
                    page["commands"][role][0] = "binary"
                    folder = root / page["commands"][role][4]
                    folder.mkdir()
                    (folder / "run.json").write_text("{}")
            for assessment in plan["assessments"]:
                for role in ("candidate", "baseline", "replicate"):
                    destination = assessment[role]["bundle"]
                    assessment[role]["execution"]["response"] = {
                        "record": {
                            "run": "a" * 32,
                            "sha256": hashlib.sha256(b"{}").hexdigest(),
                            "bytes": 2,
                        },
                        "output": str(Path(destination) / "result.png"),
                    }
            originals = [copy.deepcopy(record) for _ in range(6)]
            repeats = [copy.deepcopy(record) for _ in range(6)]
            for index, (original, repeated) in enumerate(zip(originals, repeats, strict=True)):
                original["record"]["run"] = f"{index + 1:032x}"
                repeated["record"]["run"] = f"{index + 7:032x}"
            repeats[1]["protection"] = {"sha256": "different-mask"}
            for index, assessment in enumerate(plan["assessments"]):
                for number, role in enumerate(("candidate", "baseline", "replicate")):
                    assessment[role]["execution"]["response"]["record"]["run"] = originals[
                        3 * index + number
                    ]["record"]["run"]
            attempts = [
                ReplayObservation(value, {"argv": [], "response": {"record": value["record"]}})
                for value in repeats
            ]
            with (
                patch("quality_collection.execution_evidence"),
                patch("evaluate_document_quality.read_bundle", side_effect=originals),
                patch("evaluate_document_quality.fidelity"),
                patch(
                    "evaluate_document_quality.replay",
                    side_effect=attempts,
                ) as run,
            ):
                assessed, evidence = collect(
                    plan,
                    root,
                    BundleChecks(
                        Path("binary"),
                        {},
                        evaluator.read_bundle,
                        evaluator.replay,
                        evaluator.fidelity,
                    ),
                )
            self.assertEqual(run.call_count, 6)
            self.assertEqual(len(evidence), 2)
            self.assertEqual(evidence[0]["roles"]["candidate"]["outcome"], "completed")
            self.assertIn("Frozen baseline differs", evidence[0]["roles"]["baseline"]["errors"][0])
            self.assertEqual(evidence[1]["roles"]["baseline"]["outcome"], "completed")
            self.assertEqual(adjudicate(assessed)["decision"], "abstain")

    def test_report_publication_cannot_overwrite_source_or_prior_evidence(self) -> None:
        """A real exclusive file write rejects collisions and preserves existing bytes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, output = root / "manifest.json", root / "report.json"
            manifest.write_text("{}")
            command = [
                "evaluate",
                "--manifest",
                str(manifest),
                "--executable",
                "binary",
                "--out",
                str(output),
            ]
            with (
                patch.object(sys, "argv", command),
                patch("evaluate_document_quality.evaluate", return_value={"decision": "accepted"}),
                contextlib.redirect_stderr(io.StringIO()),
            ):
                self.assertEqual(main(), 0)
                original = output.read_bytes()
                self.assertEqual(main(), 1)
                self.assertEqual(output.read_bytes(), original)

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
            field: {}
            for field in ("source", "request", "execution", "protection", "output", "build")
        }
        candidate["record"] = {"run": "a"}
        for change in ("run", "build", "protection", "source"):
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
