# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Original raw delivery, publication and evidence eligibility are separate observations."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

from jsonschema import Draft202012Validator, ValidationError

from quality_acquisition import acquire_json_file, acquire_original_stream
from quality_attempt import MAX_RESPONSE_BYTES, CaptureAdmission, issue, note_issue
from quality_json import ROOT, read_json

if TYPE_CHECKING:
    from pathlib import Path


COHERENT_DELIVERY_EXITS = frozenset({0, 2, 3, 4, 130})
REFUSAL_CODES = frozenset(
    {"E_ARGUMENT", "E_INPUT", "E_RESOURCE", "E_METHOD_INAPPLICABLE", "E_NOT_IMPLEMENTED"}
)


def ambiguous_exit(code: int | None) -> bool:
    """Keep actual abnormal process integers; an exit alone never undoes a confirmed commit."""
    return code not in COHERENT_DELIVERY_EXITS


def execution_evidence(original: dict[str, Any], root: Path, admission: CaptureAdmission) -> bool:
    """Acquire each original once, preserving complete bound stdout despite later I/O failure."""
    declared = original["declared"]
    evidence = declared.get("execution")
    if evidence is None:
        return False
    payload = acquire_original_stream(original, "stdout", root, admission)
    response = None
    if payload is not None:
        try:
            parsed = read_json(payload)
            schema = acquire_json_file(ROOT / "schemas/command-response.schema.json")
            Draft202012Validator(schema).validate(parsed)
            response = parsed
            original["response"] = response
        except (OSError, ValueError, TypeError, MemoryError, ValidationError) as error:
            note_issue(
                original,
                "errors.delivery",
                issue("schema_refused", original["role_index"], error),
                admission,
            )
    confirmed = response.get("publication") if response is not None else None
    ambiguous = evidence["timed_out"] or ambiguous_exit(evidence["exit_code"]) or confirmed is None
    original["observed_publication"] = (
        "completed" if confirmed == "completed" else "unknown" if ambiguous else confirmed
    )
    # Even a failed stderr acquisition cannot erase the same-byte confirmed stdout commit.
    acquire_original_stream(original, "stderr", root, admission)
    for name in ("stdout", "stderr"):
        extent = original[name]["observed_prefix_bytes"]
        if extent is not None and extent > MAX_RESPONSE_BYTES:
            note_issue(
                original, "errors.raw_" + name, issue("overflow", original["role_index"]), admission
            )
    if response is None or not coherent_delivery(
        declared, response, original["observed_publication"], ambiguous=ambiguous
    ):
        note_issue(
            original, "errors.delivery", issue("delivery", original["role_index"]), admission
        )
    complete = all(
        original[name]["retention_state"] == "complete"
        and original[name]["read_state"] == "eof"
        and original[name]["closed"] is True
        for name in ("stdout", "stderr")
    )
    return (
        complete
        and not any(original["errors"].values())
        and all(not any(original[name]["errors"].values()) for name in ("stdout", "stderr"))
    )


def coherent_delivery(
    declared: dict[str, Any], response: dict[str, Any] | None, effect: str, *, ambiguous: bool
) -> bool:
    """Classify actual complete delivery without inferring retry safety from exit 5/7."""
    evidence = declared["execution"]
    if response != evidence["response"] or evidence["publication"] != effect:
        return False
    if response is not None and (
        response.get("command") != "process"
        or (
            not ambiguous_exit(evidence["exit_code"])
            and evidence["exit_code"] != response["exit_code"]
        )
    ):
        return False
    if declared["outcome"] == "completed":
        return (
            evidence["exit_code"] == 0
            and not evidence["timed_out"]
            and response is not None
            and response.get("publication") == "completed"
        )
    if evidence["exit_code"] == 0 and not evidence["timed_out"]:
        return False
    known = response is not None and response.get("error", {}).get("code") in REFUSAL_CODES
    refusal_effect = not ambiguous and effect in {"not_started", "not_published"}
    if declared["outcome"] == "failed" and known and refusal_effect:
        return False
    return declared["outcome"] != "refused" or (
        refusal_effect and evidence["exit_code"] in {2, 3, 4} and known
    )
