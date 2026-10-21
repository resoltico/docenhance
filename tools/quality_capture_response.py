# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Complete quality stdout admission and monotonic processing effect observations."""

from __future__ import annotations

from typing import Any

from jsonschema import Draft202012Validator, ValidationError

from quality_acquisition import acquire_json_file
from quality_attempt import CaptureAdmission, delivered_path, issue, note_issue
from quality_json import ROOT, read_json


def observe_response(
    attempt: dict[str, Any], slot: dict[str, Any], payload: bytes, admission: CaptureAdmission
) -> None:
    """Only complete native-schema-admitted stdout can monotonically confirm a commit."""
    try:
        response = read_json(payload)
    except (ValueError, TypeError, MemoryError) as error:
        note_issue(
            attempt,
            "errors.interpret_stdout",
            issue("interpret_stdout", slot["index"], error),
            admission,
        )
        return
    try:
        schema = acquire_json_file(ROOT / "schemas/command-response.schema.json")
        Draft202012Validator(schema).validate(response)
    except (OSError, ValueError, TypeError, ValidationError, MemoryError) as error:
        note_issue(
            attempt,
            "errors.response_schema",
            issue("schema_refused", slot["index"], error),
            admission,
        )
        return
    attempt.update(response=response, response_origin="complete_pipe_bytes")
    process = slot["kind"] == "replay_process"
    if process and response.get("command") == "process":
        confirmed = response.get("publication")
        ambiguous = attempt["timed_out"] or attempt["exit_code"] not in {0, 2, 3, 4, 130}
        if confirmed == "completed":
            attempt["publication"] = "completed"
        elif not ambiguous and confirmed in {"not_started", "not_published", "unknown"}:
            attempt["publication"] = confirmed
    expected_command = "process" if process else "verify"
    if response.get("command") != expected_command:
        note_issue(
            attempt, "errors.record_agreement", issue("record_agreement", slot["index"]), admission
        )
        return
    if attempt["exit_code"] in {0, 2, 3, 4, 130} and attempt["exit_code"] != response.get(
        "exit_code"
    ):
        note_issue(
            attempt, "errors.record_agreement", issue("record_agreement", slot["index"]), admission
        )
    if (
        process
        and "output" in response
        and response["output"] != delivered_path(slot["planned_target"], "result.png")
    ):
        note_issue(
            attempt,
            "errors.record_agreement",
            issue("record_agreement", slot["index"]),
            admission,
        )
