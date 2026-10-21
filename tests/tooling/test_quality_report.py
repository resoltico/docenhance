# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual report projections and count-only allowances preserve every repeated frozen value."""

from __future__ import annotations

import copy
import unittest
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_accounting_fixture import factual_run

from quality_adjudication import adjudicate
from quality_allowance import attempt_allowance, original_allowance
from quality_attempt import MAX_RESPONSE_BYTES, CaptureAdmission, issue, new_attempt, note_issue
from quality_json import canonical, frozen_digest, read_json
from quality_report import report
from quality_report_allowance import _Bytes, _count, report_allowance
from quality_schema import harm_ids

prepare_tool_imports()


EXPECTED_PAGE_COUNT = 2


class QualityReportTests(unittest.TestCase):
    """Conservative source counters are distinct from platform serialization qualification."""

    def test_report_identity_excludes_itself_and_retains_every_intended_member(self) -> None:
        """Report identity excludes itself and retains every intended member."""
        run = factual_run()
        value = run["report"]
        preimage = {key: child for key, child in value.items() if key != "report_sha256"}
        self.assertEqual(value["report_sha256"], frozen_digest(preimage))
        changed = copy.deepcopy(preimage)
        changed["evidence"][0]["reviews"][0]["rationale"] += "changed"
        self.assertNotEqual(frozen_digest(changed), value["report_sha256"])
        self.assertEqual(len(value["evidence"]), 2)
        self.assertTrue(
            all(item["count"] == EXPECTED_PAGE_COUNT for item in value["classes"].values())
        )

    def test_counter_accounts_unicode_controls_paths_reviews_and_response_duplicates(self) -> None:
        """Counter accounts unicode controls paths reviews and response duplicates."""
        run = factual_run()
        plan, pages = run["intended"]["plan"], run["report"]["evidence"]
        text = 'é漢😀"\\\n\t\u0000' * 500
        # This is count-only projection coverage, not a claim these synthetic responses ran.
        response = {"synthetic_counter_payload": text}
        for page in pages:
            page["frozen_policy"]["source"]["permission"]["evidence"] = text
            for review in page["reviews"]:
                review["rationale"] = text
            page["prior_reviews"] = [
                {"sample": page["sample"], "reviews": copy.deepcopy(page["reviews"])}
            ]
            for role in page["roles"].values():
                role["declared"]["execution"] = {"counter_exact_frozen_text": text}
                role["original"]["declared"] = copy.deepcopy(role["declared"])
                role["original"]["response"] = response
                for name in ("scored_verify", "replay_process", "replay_verify"):
                    role[name]["response"] = response
        prior = {page["sample"]: page["prior_reviews"][0]["reviews"] for page in pages}
        errors = run["report"]["errors"]
        value = report(plan, run["report"]["executable_sha256"], pages, errors, prior)
        context = {"invocations": run["intended"]["invocations"], "subject_max": 99999}
        bound = report_allowance(plan, pages, errors, prior, context)
        self.assertGreaterEqual(bound, len(canonical(value)))
        self.assertGreater(len(canonical(value)), len(canonical(response)) * 24)
        role = pages[0]["roles"]["candidate"]
        self.assertGreaterEqual(
            original_allowance(role["original"], canonical), len(canonical(role["original"]))
        )
        slot = context["invocations"][1]
        self.assertGreaterEqual(
            attempt_allowance(slot, canonical), len(canonical(role["replay_process"]))
        )

    def test_all_harms_prior_uncertainty_and_run_failure_keep_class_denominators(self) -> None:
        """All harms prior uncertainty and run failure keep class denominators."""
        run = factual_run()
        plan, pages = run["intended"]["plan"], run["report"]["evidence"]
        earlier = copy.deepcopy(plan["assessments"][0]["reviews"])
        earlier[0].update(harms=list(harm_ids()), uncertain=True, delta=-2)
        prior = {"0": earlier, "1": []}
        errors = copy.deepcopy(run["report"]["errors"])
        errors["final_flush"] = issue("final_flush", None, OSError(5, "not serialized"))
        before = copy.deepcopy(plan)
        result = report(plan, run["report"]["executable_sha256"], pages, errors, prior)
        self.assertEqual(plan, before)
        self.assertEqual(result["decision"], "abstain")
        for category in result["classes"].values():
            self.assertEqual(category["count"], 2)
            self.assertIn("0", category["harmed"])
            self.assertIn("0", category["uncertain"])
            self.assertEqual(len(category["pages"]), 2)
        context = {"invocations": run["intended"]["invocations"], "subject_max": 17}
        self.assertGreaterEqual(
            report_allowance(plan, pages, errors, prior, context), len(canonical(result))
        )

    def test_count_only_values_cannot_be_serialized_as_maximum_success(self) -> None:
        """Count only values cannot be serialized as maximum success."""
        with self.assertRaises(TypeError):
            canonical(_Bytes(100))
        repeated = {"x": ["é\n", "é\n"], "y": {"z": "é\n"}}
        self.assertEqual(_count(repeated), len(canonical(repeated)))
        self.assertGreater(_count(repeated), _count({"x": ["é\n"], "y": {"z": "é\n"}}))
        with self.assertRaises(ValueError):
            _count(_Bytes(-1))

    def test_new_adjudication_members_close_allowance_admission(self) -> None:
        """New adjudication members close allowance admission."""
        run = factual_run()
        plan, value = run["intended"]["plan"], run["report"]

        for place in ("class", "page"):
            skeleton = adjudicate(plan, {})
            category = next(iter(skeleton["classes"].values()))
            target = category if place == "class" else category["pages"][0]
            target["unreviewed_new_member"] = "must not silently disappear"
            with (
                patch("quality_report_allowance.adjudicate", return_value=skeleton),
                self.subTest(place=place),
                self.assertRaisesRegex(ValueError, "Unhandled"),
            ):
                report_allowance(
                    plan,
                    value["evidence"],
                    value["errors"],
                    {},
                    {"invocations": run["intended"]["invocations"], "subject_max": 17},
                )

    def test_local_issue_subject_and_first_reference_cannot_be_reassigned(self) -> None:
        """Local issue subject and first reference cannot be reassigned."""
        slot = factual_run()["intended"]["invocations"][4]
        attempt, admission = new_attempt(slot), CaptureAdmission()
        note_issue(attempt, "stdout.errors.write", issue("write", 4), admission)
        note_issue(attempt, "cleanup_errors.stop_producer", issue("stop_producer", None), admission)
        self.assertEqual(attempt["primary_error"], "stdout.errors.write")
        self.assertEqual(admission.closure_reason, issue("write", 4))
        with self.assertRaisesRegex(ValueError, "actual observation owner"):
            note_issue(attempt, "stderr.errors.read", issue("read", 1), admission)
        self.assertIsNone(attempt["stderr"]["errors"]["read"])

    def test_selected_serializer_samples_respect_conditional_response_envelope(self) -> None:
        """Selected serializer samples respect conditional response envelope."""
        # Finite source controls complement the separate pinned interpreter/platform proof.
        raws = [
            b'{"x":5e-324}',
            b'{"x":1.7976931348623157e308}',
            b'{"x":-0.0}',
            b'{"x":1e-7}',
            b'{"x":1e20}',
            b'{"\\u0000\\u00e9\\ud83d\\ude00":"\\u0001\\n\\t"}',
            b'{"x":' + b"9" * 1000 + b"}",
        ]
        for raw in raws:
            self.assertLessEqual(len(raw), MAX_RESPONSE_BYTES)
            self.assertLessEqual(len(canonical(read_json(raw))), 32 * len(raw))
