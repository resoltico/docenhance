# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed v3 accounting controls use no producer or simulated successful native execution."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import PurePosixPath, PureWindowsPath
from unittest.mock import patch

import tools_path

from jsonschema import ValidationError
from quality_accounting_fixture import factual_run, factual_slots
from quality_fixtures import healthy_plan

from document_quality import read_run_json, validate_run
from quality_attempt import (
    ATTEMPT_ERRORS,
    CLEANUP_ERRORS,
    ISSUE_CODES,
    RUN_ERRORS,
    STREAM_ERRORS,
    VALIDATION_ERRORS,
    issue,
    new_attempt,
    new_original,
    new_stream,
    primary_references,
)
from quality_json import canonical, frozen_digest
from quality_plan import validate_plan
from quality_protocol import validate_work_index
from quality_report import report
from quality_run_observations import complete_stream, stream_relationships
from quality_run_report import admit_run_identities
from quality_schema import validator

EXPECTED_PAGE_COUNT = 2


class QualityRunTests(unittest.TestCase):
    """Static shape does not substitute for actual N, dependencies, snapshots or retained harms."""

    def test_source_projection_domains_and_exact_closed_shapes_agree(self) -> None:
        """Source projection domains and exact closed shapes agree."""
        schema = json.loads((tools_path.ROOT / "spec/document-quality.schema.json").read_bytes())[
            "$defs"
        ]
        self.assertEqual(schema["issue"]["properties"]["code"]["enum"], list(ISSUE_CODES))
        for name, keys in (
            ("attempt_errors", ATTEMPT_ERRORS),
            ("cleanup_errors", CLEANUP_ERRORS),
            ("stream_errors", STREAM_ERRORS),
            ("validation_errors", VALIDATION_ERRORS),
            ("run_errors", RUN_ERRORS),
        ):
            self.assertEqual(schema[name]["required"], list(keys))
        self.assertEqual(
            schema["attempt"]["properties"]["primary_error"]["enum"], [None, *primary_references()]
        )
        validator("stream").validate(new_stream())
        original = new_original(
            0, {"outcome": "missing_evidence", "bundle": None, "execution": None}
        )
        validator("original").validate(original)
        with self.assertRaises(ValidationError):
            validator("original").validate(original | {"primary_error": None})
        for slot in factual_slots(
            0, ["selected", "process", "source", "--out-dir", "original", "--json"]
        ):
            validator("attempt").validate(new_attempt(slot))

    def test_finished_abstention_accounts_every_original_and_planned_slot(self) -> None:
        """Finished abstention accounts every original and planned slot."""
        for path_type in (PurePosixPath, PureWindowsPath):
            with (
                self.subTest(path_type=path_type),
                patch("quality_accounting_fixture.Path", path_type),
                patch("quality_protocol.Path", path_type),
            ):
                value = factual_run()
                validate_run(value)
                self.assertEqual(value["report"]["decision"], "abstain")
                self.assertEqual(len(value["intended"]["invocations"]), 18)
                self.assertEqual(len(value["events"]), 42)
                self.assertEqual(value["terminal"]["accounted_roles"], 6)
                self.assertTrue(
                    all(
                        item["count"] == EXPECTED_PAGE_COUNT
                        for item in value["report"]["classes"].values()
                    )
                )
                self.assertFalse(validator("quality_run").is_valid(value["report"]))
                self.assertFalse(validator("quality_run").is_valid(value | {"protocol_version": 2}))

    def test_outer_only_flip_and_old_hash_cannot_admit_v3(self) -> None:
        """Outer only flip and old hash cannot admit v3."""
        plan = healthy_plan()
        del plan["protocol"]["evidence_contract"]
        plan["protocol_version"] = 2
        plan["protocol_sha256"] = frozen_digest(plan["protocol"])
        old = plan["protocol_sha256"]
        plan["protocol_version"] = 3
        with self.assertRaises(ValidationError):
            validate_plan(plan)
        plan["protocol"]["evidence_contract"] = "docenhance-quality-v3"
        with self.assertRaisesRegex(ValueError, "protocol or corpus identity"):
            validate_plan(plan)
        self.assertNotEqual(frozen_digest(plan["protocol"]), old)

    def test_static_shape_cannot_approve_wrong_dynamic_counts_or_duplicates(self) -> None:
        """Static shape cannot approve wrong dynamic counts or duplicates."""
        for change in ("work", "event", "duplicate", "terminal", "page"):
            value = factual_run()
            if change == "work":
                value["intended"]["invocations"].pop()
            elif change == "event":
                value["events"].pop()
            elif change == "duplicate":
                value["events"][1] = copy.deepcopy(value["events"][0])
            elif change == "terminal":
                value["terminal"]["accounted_invocations"] -= 1
            else:
                value["report"]["evidence"].pop()
            validator("quality_run").validate(value)
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_run(value)

    def test_final_role_agrees_with_finished_instead_of_prepared(self) -> None:
        """Final role agrees with finished instead of prepared."""
        value = factual_run()
        prepared = value["events"][1]["observation"]
        finished = value["events"][2]["observation"]
        self.assertNotEqual(prepared, finished)
        value["report"]["evidence"][0]["roles"]["candidate"]["scored_verify"] = copy.deepcopy(
            prepared
        )
        with self.assertRaisesRegex(ValueError, "FINISHED, not PREPARED"):
            validate_run(value)

    def test_prepared_first_cause_cannot_be_replaced(self) -> None:
        """Prepared first cause cannot be replaced."""
        value = factual_run()
        prepared, finished = value["events"][1]["observation"], value["events"][2]["observation"]
        prepared["primary_error"] = finished["primary_error"] = "errors.prepare_directory"
        prepared["errors"]["prepare_directory"] = issue("prepare_directory", 0)
        finished["errors"]["prepare_directory"] = issue("unexpected_exception", 0)
        with self.assertRaisesRegex(ValueError, "First fixed cause was replaced"):
            validate_run(value)
        invalid = copy.deepcopy(prepared)
        invalid["primary_error"] = "errors.arbitrary"
        self.assertFalse(validator("attempt").is_valid(invalid))

    def test_exact_argv_derivation_preserves_option_looking_asset_value(self) -> None:
        """Exact argv derivation preserves option looking asset value."""
        plan = healthy_plan(1, natural=False)
        frozen = [
            "selected",
            "process",
            "0.png",
            "--protect-mask",
            "--out-dir",
            "--out-dir",
            "./original//",
            "--json",
        ]
        policy = plan["protocol"]["pages"][0]
        slots = []
        for index, name in enumerate(("candidate", "baseline", "replicate")):
            policy["commands"][name] = frozen.copy()
            plan["assessments"][0][name]["bundle"] = "./original//"
            slots.extend(factual_slots(index, frozen))
        validate_work_index(plan, slots)
        self.assertEqual(slots[1]["argv"][4], "--out-dir")
        self.assertEqual(slots[1]["argv"][6], slots[1]["planned_target"])
        slots[1]["argv"][4] = "different-mask"
        with self.assertRaisesRegex(ValueError, "exact frozen/derived"):
            validate_work_index(plan, slots)

    def test_prior_harm_prefix_survives_later_unavailable_snapshot(self) -> None:
        """Prior harm prefix survives later unavailable snapshot."""
        reviews = healthy_plan(2)["assessments"][0]["reviews"]
        harmful = {"sample": "0", "reviews": copy.deepcopy(reviews)}
        harmful["reviews"][0].update(harms=["lost_mark"], delta=-1)
        later = {"sample": "0", "reviews": copy.deepcopy(reviews)}
        value = factual_run(revisions=[harmful, later])
        value["intended"]["referenced_evidence"][-1]["value"] = None
        page = value["report"]["evidence"][0]
        page["prior_reviews"] = [harmful]
        page["validation"]["prior_reviews"] = issue("identity_mismatch", 4)
        prior = {"0": harmful["reviews"], "1": []}
        value["report"] = report(
            value["intended"]["plan"],
            value["report"]["executable_sha256"],
            value["report"]["evidence"],
            value["report"]["errors"],
            prior,
        )
        validate_run(value)
        self.assertIn("0", value["report"]["classes"]["faint_marks"]["harmed"])
        self.assertEqual(value["report"]["classes"]["faint_marks"]["count"], 2)
        self.assertEqual(value["report"]["decision"], "abstain")

    def test_prefix_and_concatenated_documents_cannot_be_completed_runs(self) -> None:
        """Prefix and concatenated documents cannot be completed runs."""
        value = factual_run()
        prefix = b'{"kind":"quality_run","protocol_version":3,"events":['
        for raw in (prefix, canonical(value) + b"\n{}", canonical(value)[:-1]):
            with self.subTest(raw=raw[:60]), self.assertRaises(ValueError):
                read_run_json(raw)
        value["events"] = value["events"][:2]
        with self.assertRaisesRegex(ValueError, "21N"):
            validate_run(value)

    def test_attempt_and_original_causes_use_their_actual_owner_index(self) -> None:
        """Attempt and original causes use their actual owner index."""
        for domain in ("attempt", "original"):
            value = factual_run()
            if domain == "attempt":
                observation = value["events"][4]["observation"]
                self.assertEqual(observation["role_index"], 0)
                observation["primary_error"] = "errors.launch"
                observation["errors"]["launch"] = issue("launch", 0)
            else:
                observation = value["events"][0]["observation"]
                observation["errors"]["raw_stdout"] = issue("raw_stdout", 1)
            with (
                self.subTest(domain=domain),
                self.assertRaisesRegex(ValueError, "actual owner index"),
            ):
                validate_run(value)

    def test_dependent_preparation_cannot_follow_failed_predecessor(self) -> None:
        """Dependent preparation cannot follow failed predecessor."""
        value = factual_run()
        slot = value["intended"]["invocations"][1]
        prepared = value["events"][3]["observation"]
        prepared["argv"] = slot["argv"]
        prepared["artifact_directory"] = slot["planned_directory"]
        with self.assertRaisesRegex(ValueError, "coherent predecessor"):
            validate_run(value)

    def test_missing_original_target_has_nullable_verifiers_without_fake_argv(self) -> None:
        """Missing original target has nullable verifiers without fake argv."""
        value = factual_run()
        validate_run(value)
        slot = value["intended"]["invocations"][0]
        self.assertIsNone(slot["requested_argv"])
        self.assertIsNone(slot["argv"])
        self.assertIsNone(slot["planned_target"])
        slot.update(
            requested_argv=["binary", "verify", "invented", "--json"],
            argv=["binary", "verify", "invented", "--json"],
            planned_target="invented",
        )
        with self.assertRaisesRegex(ValueError, "bound role"):
            validate_run(value)

    def test_unavailable_revision_cannot_hide_later_independently_admitted_harm(self) -> None:
        """Unavailable revision cannot hide later independently admitted harm."""
        reviews = healthy_plan(2)["assessments"][0]["reviews"]
        revisions = [{"sample": "0", "reviews": copy.deepcopy(reviews)} for _ in range(3)]
        revisions[2]["reviews"][0].update(harms=["lost_mark"], delta=-1)
        value = factual_run(revisions=revisions)
        value["intended"]["referenced_evidence"][-2]["value"] = None
        page = value["report"]["evidence"][0]
        page["prior_reviews"] = [revisions[0], revisions[2]]
        page["validation"]["prior_reviews"] = issue("identity_mismatch", 4)
        prior = {"0": revisions[0]["reviews"] + revisions[2]["reviews"], "1": []}
        value["report"] = report(
            value["intended"]["plan"],
            value["report"]["executable_sha256"],
            value["report"]["evidence"],
            value["report"]["errors"],
            prior,
        )
        validate_run(value)
        self.assertIn("0", value["report"]["classes"]["faint_marks"]["harmed"])
        page["prior_reviews"] = [revisions[0]]
        with self.assertRaisesRegex(ValueError, "Admitted human revisions"):
            validate_run(value)

    def test_digest_survives_failed_close_without_claiming_complete_retention(self) -> None:
        """Digest survives failed close without claiming complete retention."""
        stream = new_stream("capture/stdout")
        stream.update(
            read_state="eof",
            retention_state="prefix",
            observed_prefix_bytes=1,
            observed_extent="exact",
            retained_bytes=1,
            sha256="0" * 64,
            closed=None,
        )
        stream["errors"]["close"] = issue("close", 0, OSError(5, "not serialized"))
        stream_relationships(stream, captured=True)
        self.assertFalse(complete_stream(stream))
        stream["retention_state"] = "complete"
        with self.assertRaisesRegex(ValueError, "persistence"):
            stream_relationships(stream, captured=True)
        stream["retention_state"] = "prefix"
        stream["errors"]["close"] = None
        with self.assertRaisesRegex(ValueError, "truthful close"):
            stream_relationships(stream, captured=True)

    def test_distinct_run_ids_span_all_pages_without_refunding_failed_admission(self) -> None:
        """Distinct run ids span all pages without refunding failed admission."""
        seen: set[str] = set()
        first = [f"{index:032x}" for index in range(6)]
        second = [f"{index:032x}" for index in range(6, 12)]
        admit_run_identities(first, seen)
        admit_run_identities(second, seen)
        self.assertEqual(len(seen), 12)
        before = seen.copy()
        cross_page = [f"{index:032x}" for index in range(12, 17)] + [first[3]]
        self.assertEqual(len(cross_page), len(set(cross_page)))
        with self.assertRaisesRegex(ValueError, "across the intended run"):
            admit_run_identities(cross_page, seen)
        self.assertEqual(seen, before)
        with self.assertRaisesRegex(ValueError, "identities collide"):
            admit_run_identities(["f" * 32, "f" * 32], seen)
        self.assertEqual(seen, before)
