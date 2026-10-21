# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One fixed replay and verifier retain immutable finished observations on every outcome."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

from jsonschema import ValidationError

from quality_attempt import delivered_path, issue
from quality_collection import SEMANTIC_FACTS
from quality_protocol import BUNDLE_ROLES

if TYPE_CHECKING:
    from quality_collection import BundleChecks


EVIDENCE_ERRORS = (OSError, ValueError, TypeError, KeyError, MemoryError, ValidationError)


def _refuse(checks: BundleChecks, role_index: int, phase: str, error: Exception) -> None:
    """Semantic refusal belongs to the role, never a previously finished invocation."""
    role = checks.run.pages[role_index // 3]["roles"]
    report = role[BUNDLE_ROLES[role_index % 3]]
    cause = issue(phase, role_index, error)
    if report["validation"][phase] is None:
        report["validation"][phase] = cause
    report["outcome"] = "missing_evidence"
    checks.run.admission.close(cause)


def replay(
    checks: BundleChecks, role_index: int, source_sha256: str, scored_record: dict[str, Any]
) -> dict[str, Any] | None:
    """Consume exactly the fixed process/verify slots; no temporary directories or retries."""
    run = checks.run
    process_index = 3 * role_index + 1
    attempt = run.invoke(process_index)
    if attempt["outcome"] != "completed":
        message = "Replay processing did not complete"
        _refuse(checks, role_index, "delivery", ValueError(message))
        run.skip(process_index + 1)
        return None
    phase = "record"
    try:
        response = attempt["response"]
        # This callable always consumes the verifier slot before inspecting artifacts.
        record = checks.read_bundle(checks, process_index + 1, source_sha256, response)
        target = run.slots[process_index]["planned_target"]
        if response["output"] != delivered_path(target, "result.png"):
            message = "Replay delivery differs from its fixed owned target"
            raise ValueError(message)
        phase = "source"
        if record["source"]["sha256"] != source_sha256:
            message = "Replay source differs from frozen source identity"
            raise ValueError(message)
        phase = "build"
        if record["build"] != checks.build:
            message = "Replay build differs from frozen provenance"
            raise ValueError(message)
        phase = "repetition"
        if record["record"]["run"] == scored_record["record"]["run"]:
            message = "Replay reused its scored run identity"
            raise ValueError(message)
        if set(record) != set(scored_record) or any(
            record[field] != scored_record[field] for field in SEMANTIC_FACTS
        ):
            message = "Frozen replay differs from scored processing semantics"
            raise ValueError(message)
    except EVIDENCE_ERRORS as error:
        _refuse(checks, role_index, phase, error)
        return None
    else:
        return record
