# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Completed quality_run protocol, reference, event and report relationship admission."""

from __future__ import annotations

from typing import Any

from quality_observations import require
from quality_plan import validate_plan
from quality_protocol import (
    reference_snapshot_failures,
    reference_snapshots_coherent,
    validate_work_index,
)
from quality_provenance_validation import snapshot_order
from quality_run_events import events
from quality_run_report import bounded_causes, report_relationships


def validate_run_relationships(value: dict[str, Any]) -> None:
    """Check completed container relationships beyond its static closed shape."""
    plan = value["intended"]["plan"]
    validate_plan(plan)
    require(
        value["protocol_sha256"] == plan["protocol_sha256"]
        and value["report"]["protocol_sha256"] == plan["protocol_sha256"]
        and value["report"]["executable_sha256"] == plan["protocol"]["executable_sha256"],
        "Run protocol/executable identity differs",
    )
    references = value["intended"]["referenced_evidence"]
    require(
        [entry["reference"] for entry in references] == snapshot_order(plan, references),
        "Reference snapshot index differs",
    )
    reference_snapshots_coherent(references)
    if reference_snapshot_failures(plan, references):
        require(
            value["report"]["errors"]["referenced_evidence"] is not None,
            "Unavailable or incoherent referenced evidence lost its finite cause",
        )
    slots = value["intended"]["invocations"]
    validate_work_index(plan, slots)
    maximum = max(len(slots) - 1, len(references) - 1, 3 * len(plan["assessments"]) - 1, 0)
    original, finished = events(value, slots)
    terminal = value["terminal"]
    require(
        (terminal["accounted_roles"], terminal["accounted_invocations"], terminal["event_count"])
        == (3 * len(plan["assessments"]), len(slots), len(value["events"])),
        "Terminal accounting differs",
    )
    require(
        (terminal["launch_admission"] == "closed") == (terminal["closure_reason"] is not None),
        "Admission closure needs its actual finite cause",
    )
    bounded_causes([terminal["closure_reason"], *value["report"]["errors"].values()], maximum)
    if terminal["launch_admission"] == "closed":
        require(
            value["report"]["decision"] == "abstain",
            "Closed launch admission cannot earn acceptance",
        )
    report_relationships(value, original, finished, maximum)
