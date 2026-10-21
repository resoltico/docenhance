# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual original and captured invocation stream/effect relationships."""

from __future__ import annotations

from typing import Any

from quality_attempt import CHUNK_BYTES, MAX_RESPONSE_BYTES, primary_references
from quality_observations import require, validate_product_response


def causes(value: dict[str, Any]) -> list[Any]:
    """Enumerate every fixed cause in this original or invocation observation."""
    result = list(value["errors"].values())
    result.extend(value.get("cleanup_errors", {}).values())
    for stream in ("stdout", "stderr"):
        if stream in value:
            result.extend(value[stream]["errors"].values())
    return result


def indexed_causes(values: list[Any], index: int) -> None:
    """Bind observation causes to their exact original or invocation owner."""
    for cause in values:
        if cause is not None:
            require(
                cause["subject"] is None or cause["subject"] == index,
                "Observation cause subject differs from its actual owner index",
            )


def complete_stream(stream: dict[str, Any]) -> bool:
    """Require complete observed input and successful separate raw persistence."""
    return (
        stream["read_state"] == "eof"
        and stream["retention_state"] == "complete"
        and stream["closed"] is True
        and stream["sha256"] is not None
        and stream["observed_extent"] == "exact"
        and stream["retained_bytes"] is not None
        and stream["observed_prefix_bytes"] == stream["retained_bytes"]
        and not any(stream["errors"].values())
    )


def stream_relationships(stream: dict[str, Any], *, captured: bool) -> None:
    """Validate independent extent, identity and close observations without inventing success."""
    if stream["observed_extent"] == "exact":
        require(stream["observed_prefix_bytes"] is not None, "Exact extent needs an observed count")
    if stream["retention_state"] == "complete":
        require(complete_stream(stream), "Complete raw evidence lacks EOF or persistence facts")
    if stream["sha256"] is not None:
        require(
            stream["path"] is not None
            and stream["retained_bytes"] is not None
            and (stream["closed"] is True or stream["errors"]["close"] is not None),
            "Retained digest needs identified bytes and truthful close facts",
        )
    if captured:
        observed, retained = stream["observed_prefix_bytes"], stream["retained_bytes"]
        require(
            observed is None or observed <= MAX_RESPONSE_BYTES + CHUNK_BYTES,
            "New capture observed count exceeds the bounded over-read",
        )
        require(retained is None or retained <= MAX_RESPONSE_BYTES, "New raw capture exceeds L")


def original_relationships(value: dict[str, Any]) -> None:
    """Bind an original confirmed commit to complete same-identity bounded stdout."""
    indexed_causes(causes(value), value["role_index"])
    for stream in ("stdout", "stderr"):
        stream_relationships(value[stream], captured=False)
    if value["observed_publication"] == "completed":
        execution = value["declared"]["execution"]
        response = value["response"]
        require(
            execution is not None
            and response is not None
            and response.get("command") == "process"
            and response.get("publication") == "completed",
            "Original completed publication needs attributed coherent delivery",
        )
        validate_product_response(response)
        stdout = value["stdout"]
        require(
            stdout["read_state"] == "eof"
            and stdout["observed_extent"] == "exact"
            and stdout["observed_prefix_bytes"] is not None
            and stdout["observed_prefix_bytes"] <= MAX_RESPONSE_BYTES
            and stdout["sha256"] == execution["stdout"]["sha256"],
            "Original commit cannot be attributed to an unbound prefix",
        )


def attempt_relationships(value: dict[str, Any], slot: dict[str, Any]) -> None:
    """Check fixed work identity, actual child facts, causes and complete command claims."""
    for key in ("kind", "role_index", "source_slot", "requested_argv"):
        require(value[key] == slot[key], "Attempt differs from its fixed work identity")
    for key, planned in (("argv", "argv"), ("artifact_directory", "planned_directory")):
        require(
            value[key] is None or value[key] == slot[planned], "Attempt prepared identity drift"
        )
    for stream in ("stdout", "stderr"):
        require(
            value[stream]["path"] is None or value[stream]["path"] == slot["planned_" + stream],
            "Attempt stream path differs from its fixed plan",
        )
        stream_relationships(value[stream], captured=True)
    errors = causes(value)
    indexed_causes(errors, slot["index"])
    primary = value["primary_error"]
    require(
        (primary is not None) == any(cause is not None for cause in errors),
        "Attempt primary cause must name an actual fixed failure slot",
    )
    if primary is not None:
        require(primary in primary_references(), "Unknown attempt primary cause reference")
        cause = value
        for key in primary.split("."):
            cause = cause[key]
        require(cause is not None, "Primary cause names an empty fixed slot")
    _child_relationships(value)
    _response_relationships(value, errors)


def _child_relationships(value: dict[str, Any]) -> None:
    """Validate attained child and parsed-response facts independently of outcome."""
    if value["launch_state"] == "not_started":
        require(
            value["pid"] is None
            and value["exit_code"] is None
            and value["direct_join"] == "not_started",
            "Unstarted attempt invents a child",
        )
    if value["launch_state"] == "joined":
        require(value["direct_join"] == "confirmed", "Joined state needs actual direct reaping")
    if value["response"] is not None:
        stdout = value["stdout"]
        require(
            value["response_origin"] is not None
            and stdout["read_state"] == "eof"
            and stdout["observed_extent"] == "exact"
            and stdout["observed_prefix_bytes"] is not None
            and stdout["observed_prefix_bytes"] <= MAX_RESPONSE_BYTES
            and value["launch_state"] in ("child_returned", "joined"),
            "Parsed response needs complete bounded stdout and an actual child",
        )


def _response_relationships(value: dict[str, Any], errors: list[Any]) -> None:
    """Retain read-only effects and complete-command evidence requirements."""
    if value["kind"] != "replay_process":
        require(value["publication"] is None, "Read-only verifier cannot own publication")
    elif value["publication"] == "completed":
        response = value["response"]
        require(
            response is not None
            and response.get("command") == "process"
            and response.get("publication") == "completed",
            "Commit lacks coherent stdout",
        )
    if value["outcome"] == "completed" or value["publication"] == "completed":
        validate_product_response(value["response"])
    if value["outcome"] == "completed":
        require(
            value["launch_state"] == "joined"
            and value["pid"] is not None
            and value["exit_code"] == 0
            and not value["timed_out"]
            and value["response"] is not None
            and value["response"].get("exit_code") == 0
            and value["response"].get("command")
            == ("process" if value["kind"] == "replay_process" else "verify")
            and value["stderr"]["retained_bytes"] == 0
            and not any(errors)
            and all(complete_stream(value[name]) for name in ("stdout", "stderr")),
            "Completed attempt lacks complete command and raw evidence",
        )
