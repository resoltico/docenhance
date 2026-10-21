# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Completed v3 enclosure preserves each original's64-level domain without widening evidence."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

from tools_path import prepare_tool_imports

from jsonschema import ValidationError
from quality_accounting_fixture import factual_run, factual_slots
from quality_capture_fixture import capture_code
from quality_fixtures import freeze

from document_quality import read_run_json, validate_run
from quality_attempt import issue
from quality_json import (
    COMPLETED_ENCLOSURE_DEPTH,
    MAX_JSON_DEPTH,
    canonical,
    frozen_digest,
    read_json,
    validate_depth,
)
from quality_report import report

prepare_tool_imports()


def nested_object(depth: int) -> dict[str, Any]:
    """One actual object chain has exactly the requested root-relative leaf depth."""
    value: dict[str, Any] = {}
    for _ in range(depth):
        value = {"child": value}
    return value


def build_snapshot(depth: int) -> dict[str, Any]:
    """Retain observed deep metadata and its provenance refusal; never qualify a native build."""
    value = factual_run(1)
    intended = value["intended"]
    plan = intended["plan"]
    entry = intended["referenced_evidence"][1]
    entry["value"]["build"] = nested_object(depth - 1)
    entry["reference"]["sha256"] = frozen_digest(entry["value"])
    plan["protocol"]["build_provenance"] = copy.deepcopy(entry["reference"])
    freeze(plan)
    receipt = intended["referenced_evidence"][0]
    receipt["value"] = {"protocol_sha256": plan["protocol_sha256"]}
    receipt["reference"]["sha256"] = frozen_digest(receipt["value"])
    plan["preregistration"] = copy.deepcopy(receipt["reference"])
    value["protocol_sha256"] = plan["protocol_sha256"]

    cause = issue("schema_refused", 1)
    current = value["report"]
    current["errors"]["referenced_evidence"] = cause
    value["terminal"].update(launch_admission="closed", closure_reason=cause)
    value["report"] = report(
        plan, current["executable_sha256"], current["evidence"], current["errors"], {}
    )
    return value


class QualityRunDepthTests(unittest.TestCase):
    """The additional depth belongs only to the known wrapper, never an original input."""

    def test_exact64_original_build_snapshot_survives_four_actual_enclosing_levels(self) -> None:
        """Exact64 original build snapshot survives four actual enclosing levels."""
        value = build_snapshot(MAX_JSON_DEPTH)
        original = value["intended"]["referenced_evidence"][1]["value"]
        self.assertEqual(COMPLETED_ENCLOSURE_DEPTH, 4)
        self.assertEqual(read_json(canonical(original)), original)
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            read_json(canonical(value))
        self.assertEqual(read_run_json(canonical(value)), value)
        validate_run(value)
        self.assertEqual(value["report"]["decision"], "abstain")
        self.assertEqual(value["terminal"]["event_count"], 21)

    def test65_original_reference_refuses_both_standalone_and_completed_admission(self) -> None:
        """Depth65 original metadata refuses standalone and completed-container admission."""
        value = build_snapshot(MAX_JSON_DEPTH + 1)
        original = value["intended"]["referenced_evidence"][1]["value"]
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            read_json(canonical(original))
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            read_run_json(canonical(value))
        # DOM validation independently enforces the original cap, beyond the outer decoder.
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            validate_run(value)

    def test_original_plan_cap_is_independent_of_the_outer_wrapper_allowance(self) -> None:
        """Original plan cap is independent of the outer wrapper allowance."""
        value = factual_run(1)
        plan = value["intended"]["plan"]
        command = plan["protocol"]["pages"][0]["commands"]["candidate"]
        # plan(0)/assessments(1)/item(2)/candidate(3)/execution(4)/response(5).
        response = nested_object(MAX_JSON_DEPTH + 1 - 5)
        plan["assessments"][0]["candidate"] = {
            "outcome": "failed",
            "bundle": None,
            "execution": {
                "argv": command,
                "stdout": {"path": "stdout", "sha256": "a" * 64},
                "stderr": {"path": "stderr", "sha256": "a" * 64},
                "exit_code": 5,
                "timed_out": False,
                "response": response,
                "publication": "unknown",
            },
        }
        # The source plan fits inside68; its own original domain remains64. Its unmodified
        # copied report would also fail identity later; the independent depth check must win.
        validate_depth(value, MAX_JSON_DEPTH + COMPLETED_ENCLOSURE_DEPTH)
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            read_run_json(canonical(value))

    def test_completed_reader_refuses_old_bare_and_open_wrapper_forms(self) -> None:
        """Completed reader refuses old bare and open wrapper forms."""
        value = factual_run(1)
        self.assertEqual(read_run_json(canonical(value)), value)
        old = copy.deepcopy(value)
        old["protocol_version"] = 2
        for wrong in (value["report"], old, value | {"extra": None}):
            with self.subTest(kind=wrong.get("kind")), self.assertRaises(ValidationError):
                read_run_json(canonical(wrong))
        with self.assertRaises(ValueError):
            read_run_json(canonical(value) + b"\n{}")

    def test_original_declared_response_at_manifest64_keeps_its_report_copy(self) -> None:
        """Original declared response at manifest64 keeps its report copy."""
        value = factual_run(1)
        plan = value["intended"]["plan"]
        command = plan["protocol"]["pages"][0]["commands"]["candidate"]
        declared = {
            "outcome": "failed",
            "bundle": None,
            "execution": {
                "argv": command,
                "stdout": {"path": "stdout", "sha256": "a" * 64},
                "stderr": {"path": "stderr", "sha256": "a" * 64},
                "exit_code": 5,
                "timed_out": False,
                "response": nested_object(MAX_JSON_DEPTH - 5),
                "publication": "unknown",
            },
        }
        plan["assessments"][0]["candidate"] = copy.deepcopy(declared)
        role = value["report"]["evidence"][0]["roles"]["candidate"]
        role["declared"] = copy.deepcopy(declared)
        role["original"]["declared"] = copy.deepcopy(declared)
        cause = issue("schema_refused", 0)
        role["original"]["errors"]["delivery"] = cause
        value["events"][0]["observation"] = copy.deepcopy(role["original"])
        value["terminal"].update(launch_admission="closed", closure_reason=cause)
        current = value["report"]
        value["report"] = report(
            plan, current["executable_sha256"], current["evidence"], current["errors"], {}
        )
        self.assertEqual(read_json(canonical(plan)), plan)
        with self.assertRaisesRegex(ValueError, "nesting depth"):
            read_json(canonical(value))
        self.assertEqual(read_run_json(canonical(value)), value)
        self.assertEqual(role["outcome"], "missing_evidence")
        self.assertEqual(role["original"]["observed_publication"], "unknown")

    def test_deep_invalid_native_object_remains_raw_without_becoming_attempt_response(self) -> None:
        """Deep invalid native object remains raw without becoming an admitted response."""
        raw_value = nested_object(MAX_JSON_DEPTH)
        code = f"print({json.dumps(canonical(raw_value).decode())})"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            slot = factual_slots(
                0, [sys.executable, "process", "source", "--out-dir", "old", "--json"]
            )[1]
            observed, admission, calls = capture_code(root, slot, code)
            self.assertEqual(len(calls), 1)
            self.assertIsNone(observed["response"])
            self.assertIsNone(observed["response_origin"])
            self.assertEqual(observed["errors"]["response_schema"]["code"], "schema_refused")
            self.assertEqual(observed["publication"], "unknown")
            self.assertEqual(observed["stdout"]["retention_state"], "complete")
            self.assertEqual(read_json((root / observed["stdout"]["path"]).read_bytes()), raw_value)
            self.assertFalse(admission.is_open)
