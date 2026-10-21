# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Offline captured provenance and later admission failures retain factual earlier effects."""

from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from quality_accounting_fixture import factual_run, factual_slots
from quality_evidence_fixture import binary_record, disk_plan, successful_response, write_reference

from quality_attempt import delivered_path, issue, new_attempt, new_original, new_stream
from quality_inputs import FrozenEvidence
from quality_json import canonical
from quality_protocol import reference_snapshot_failures
from quality_provenance_contract import capture_references
from quality_provenance_validation import (
    native_binding,
    platform_binding,
    snapshot_failures,
    snapshot_order,
)
from quality_references import referenced_order
from quality_report import report
from quality_run_observations import attempt_relationships, original_relationships
from quality_run_report import report_relationships

prepare_tool_imports()


def retained_stream(root: Path, name: str, raw: bytes) -> tuple[dict[str, str], dict[str, Any]]:
    """Real fixture files identify receiver data; this never represents a launched producer."""
    reference = write_reference(root, name, raw)
    stream = new_stream(name)
    stream.update(
        read_state="eof",
        retention_state="complete",
        observed_extent="exact",
        observed_prefix_bytes=len(raw),
        retained_bytes=len(raw),
        sha256=reference["sha256"],
        closed=True,
    )
    return reference, stream


def completed_pair(root: Path, value: dict[str, Any]) -> dict[str, Any]:
    """Matched synthetic native wire data tests receiving relationships, not qualification."""
    plan = value["intended"]["plan"]
    page = value["report"]["evidence"][0]
    role: dict[str, Any] = page["roles"]["candidate"]
    frozen = plan["protocol"]["pages"][0]["commands"]["candidate"]
    slots = factual_slots(0, frozen)
    record = binary_record()
    record["source"]["sha256"] = plan["protocol"]["corpus"][0]["sha256"]
    value["intended"]["referenced_evidence"][1]["value"]["build"] = copy.deepcopy(record["build"])
    responses, attempts = [], []
    for index, target in enumerate((slots[0]["planned_target"], slots[1]["planned_target"])):
        directory = root / target
        directory.mkdir(parents=True)
        current = copy.deepcopy(record)
        current["record"]["run"] = ("a" if index == 0 else "b") * 32
        delivered = successful_response(directory, current)
        delivered["output"] = delivered_path(target, "result.png")
        responses.append(delivered)
        confirmed = [
            {key: current["output"][key] for key in ("path", "sha256", "bytes")},
            {
                "path": "run.json",
                "sha256": delivered["record_sha256"],
                "bytes": len((directory / "run.json").read_bytes()),
            },
        ]
        verified = {
            "schema_version": 15,
            "command": "verify",
            "exit_code": 0,
            "version": current["build"]["application_version"],
            "directory": target,
            "run": current["record"]["run"],
            "recorded": current["record"]["recorded"],
            "establishes": "artifacts_agree_with_record",
            "confirmed": confirmed,
        }
        attempts.append(verified)
    declared: dict[str, Any] = {
        "outcome": "completed",
        "bundle": slots[0]["planned_target"],
        "execution": None,
    }
    stdout_ref, stdout = retained_stream(root, "original.stdout", canonical(responses[0]))
    stderr_ref, stderr = retained_stream(root, "original.stderr", b"")
    declared["execution"] = {
        "argv": frozen,
        "stdout": stdout_ref,
        "stderr": stderr_ref,
        "exit_code": 0,
        "timed_out": False,
        "response": responses[0],
        "publication": "completed",
    }
    original = new_original(0, declared)
    original.update(
        stdout=stdout, stderr=stderr, response=responses[0], observed_publication="completed"
    )
    original_relationships(original)
    plan["assessments"][0]["candidate"] = copy.deepcopy(declared)
    role.update(
        outcome="completed",
        declared=copy.deepcopy(declared),
        original=original,
        record_sha256=responses[0]["record_sha256"],
    )
    for slot, response in zip(slots, (attempts[0], responses[1], attempts[1]), strict=True):
        attempt = new_attempt(slot)
        attempt.update(
            argv=slot["argv"],
            artifact_directory=slot["planned_directory"],
            pid=slot["index"] + 1,
            exit_code=0,
            launch_state="joined",
            direct_join="confirmed",
            descendant_cleanup="not_applicable",
            response=response,
            response_origin="complete_pipe_bytes",
            outcome="completed",
        )
        if slot["kind"] == "replay_process":
            attempt["publication"] = "completed"
        for name, raw in (("stdout", canonical(response)), ("stderr", b"")):
            path = slot["planned_" + name]
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            _, attempt[name] = retained_stream(root, path, raw)
        attempt_relationships(attempt, slot)
        role[slot["kind"]] = attempt
    return role


class QualityProvenanceSnapshotTests(unittest.TestCase):
    """Check captured provenance without claiming current runtime proof."""

    def test_independent_body_correspondence_rejects_swaps_and_platform_environment_drift(
        self,
    ) -> None:
        """Independent body correspondence rejects swaps and platform environment drift."""
        # This narrow correlation control assumes separate shape admission. These partial
        # dictionaries are deliberately not valid native bodies or execution premises.
        receipt: dict[str, Any] = {
            "source_commit": "a" * 40,
            "executable_sha256": "b" * 64,
            "build": {"compiler": "same captured compiler"},
            "capture_scope": {"child_environment": {}},
        }
        native: dict[str, Any] = {
            "build": copy.deepcopy(receipt["build"]),
            "source_epoch": {"value": {"head": receipt["source_commit"]}},
            "executable": {"sha256": receipt["executable_sha256"]},
            "configuration": {"value": {"CMAKE_SYSTEM_NAME": "Linux"}},
            "imports": {"system_directory": None},
        }
        native_binding(native, receipt)
        platform_binding(native, {"observed": {"platform": "linux"}})
        for field in ("build", "source_epoch", "executable"):
            wrong = copy.deepcopy(native)
            if field == "build":
                wrong[field]["compiler"] = "another compiler"
            elif field == "source_epoch":
                wrong[field]["value"]["head"] = "c" * 40
            else:
                wrong[field]["sha256"] = "c" * 64
            with (
                self.subTest(field=field),
                self.assertRaisesRegex(ValueError, "selected build receipt"),
            ):
                native_binding(wrong, receipt)
        with self.assertRaisesRegex(ValueError, "platforms differ"):
            platform_binding(native, {"observed": {"platform": "darwin"}})
        receipt["capture_scope"]["child_environment"] = {"SystemRoot": "C:\\Windows"}
        with self.assertRaisesRegex(ValueError, "POSIX"):
            native_binding(native, receipt)
        native["configuration"]["value"]["CMAKE_SYSTEM_NAME"] = "Windows"
        native["imports"]["system_directory"] = "C:\\Windows\\System32"
        native_binding(native, receipt)
        platform_binding(native, {"observed": {"platform": "win32"}})
        native["imports"]["system_directory"] = "C:\\other\\System32"
        with self.assertRaisesRegex(ValueError, "Windows native environment"):
            native_binding(native, receipt)

    def test_component_order_and_failure_indices_use_only_acquired_receipt_and_bodies(self) -> None:
        """Component order and failure indices use only acquired receipt and bodies."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            plan, _executable = disk_plan(root, count=1)
            acquired = FrozenEvidence(plan, root)
            entries = acquired.entries
            primary = referenced_order(plan)
            selected = capture_references(entries[1]["value"])
            self.assertEqual(snapshot_order(plan, entries), primary + selected)
            first = len(primary)
            self.assertEqual([entry["reference"] for entry in entries[first:]], selected)
            self.assertTrue(all(entries[index]["value"] == {} for index in (first, first + 1)))
            # Pending {} bodies are captured facts but cannot become reviewed execution premises.
            with (
                patch(
                    "quality_acquisition.acquire_reference", side_effect=AssertionError("reopen")
                ),
                patch(
                    "quality_acquisition.fingerprint", side_effect=AssertionError("live admission")
                ),
            ):
                self.assertEqual(snapshot_failures(plan, entries), [first, first + 1])
                self.assertTrue(reference_snapshot_failures(plan, entries))
            for index in (first, first + 1):
                self.assertEqual(acquired.failures[index]["subject"], index)
                self.assertEqual(acquired.failures[index]["code"], "referenced_evidence")
            swapped = copy.deepcopy(entries)
            swapped[-2:] = reversed(swapped[-2:])
            self.assertNotEqual(
                [entry["reference"] for entry in swapped], snapshot_order(plan, swapped)
            )
            self.assertEqual(snapshot_failures(plan, swapped), [first, first + 1])
            missing = copy.deepcopy(entries)
            missing.pop()
            self.assertNotEqual(
                [entry["reference"] for entry in missing], snapshot_order(plan, missing)
            )
            self.assertIn(first + 1, snapshot_failures(plan, missing))

    def test_old_malformed_receipt_does_not_invent_component_suffix(self) -> None:
        """Old malformed receipt does not invent component suffix."""
        value = factual_run(1)
        plan, entries = value["intended"]["plan"], value["intended"]["referenced_evidence"]
        self.assertEqual(snapshot_order(plan, entries), referenced_order(plan))
        self.assertEqual(snapshot_failures(plan, entries), [])
        self.assertTrue(reference_snapshot_failures(plan, entries))

    def test_later_prelaunch_refusal_preserves_completed_pair_and_run_abstention(self) -> None:
        """Later prelaunch refusal preserves completed pair and run abstention."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            value = factual_run(1)
            role = completed_pair(root, value)
            before = copy.deepcopy(role)
            original = {
                event["role_index"]: event["observation"]
                for event in value["events"]
                if event["event"] == "original_observed"
            }
            finished = {
                event["invocation_index"]: event["observation"]
                for event in value["events"]
                if event["event"] == "invocation_finished"
            }
            original[0] = role["original"]
            for index, kind in enumerate(("scored_verify", "replay_process", "replay_verify")):
                finished[index] = role[kind]
            current = value["report"]
            current["errors"]["no_child_admission"] = issue("no_child_admission", 3)
            value["report"] = report(
                value["intended"]["plan"],
                current["executable_sha256"],
                current["evidence"],
                current["errors"],
                {},
            )
            report_relationships(
                value, original, finished, len(value["intended"]["invocations"]) - 1
            )
            self.assertEqual(role, before)
            self.assertEqual(role["outcome"], "completed")
            self.assertEqual(value["report"]["decision"], "abstain")
            role["replay_process"]["descendant_cleanup"] = "unconfirmed"
            with self.assertRaisesRegex(
                ValueError, "full original/verification/replay obligations"
            ):
                report_relationships(value, original, finished, 8)
