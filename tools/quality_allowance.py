# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Finite per-observation canonical byte allowances from the actual domains."""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

from quality_attempt import (
    ATTEMPT_ERRORS,
    CHUNK_BYTES,
    CLEANUP_ERRORS,
    DESCENDANT_STATES,
    DIRECT_JOIN_STATES,
    EXTENT_STATES,
    ISSUE_CODES,
    LAUNCH_STATES,
    MAX_RESPONSE_BYTES,
    READ_STATES,
    RESPONSE_ORIGINS,
    RETENTION_STATES,
    STREAM_ERRORS,
    VALIDATION_ERRORS,
    attempt_index,
    new_attempt,
    new_original,
    new_stream,
    primary_references,
)

if TYPE_CHECKING:
    from collections.abc import Callable


NULL_JSON_BYTES = len(b"null")
RESPONSE_JSON_EXPANSION = 32


def issue_allowance(subject: int, serialize: Callable[[object], bytes]) -> int:
    """Charge the widest actual closed Issue domains and this observation subject."""
    return len(
        serialize(
            {
                "code": max(ISSUE_CODES, key=len),
                "errno": -(2**31),
                "winerror": 2**32 - 1,
                "subject": subject if len(serialize(subject)) >= NULL_JSON_BYTES else None,
            }
        )
    )


def stream_allowance(
    path: str | None, subject: int, serialize: Callable[[object], bytes], *, original: bool = False
) -> int:
    """Count the same stream projection with independent nullable and finite domain maxima."""
    stream = new_stream(path)
    size = len(serialize(stream))
    domains = {
        "path": (None, path),
        "read_state": READ_STATES,
        "retention_state": RETENTION_STATES,
        "observed_extent": EXTENT_STATES,
        "closed": (None, False, True),
        "sha256": (None, "f" * 64),
        "observed_prefix_bytes": (
            None,
            2**63 - 1 if original else MAX_RESPONSE_BYTES + CHUNK_BYTES,
        ),
        "retained_bytes": (None, 2**63 - 1 if original else MAX_RESPONSE_BYTES),
    }
    for key, values in domains.items():
        size += max(len(serialize(value)) for value in values) - len(serialize(stream[key]))
    return size + len(STREAM_ERRORS) * (issue_allowance(subject, serialize) - len(serialize(None)))


def attempt_allowance(slot: dict[str, Any], serialize: Callable[[object], bytes]) -> int:
    """Count this SAME projection, all finite branches and one 32L response occurrence."""
    attempt = new_attempt(slot)
    size = len(serialize(attempt))
    subject = attempt_index(attempt)
    domains = {
        "argv": (None, slot["argv"]),
        "artifact_directory": (None, slot["planned_directory"]),
        "launch_state": LAUNCH_STATES,
        "pid": (None, 2**32 - 1),
        "exit_code": (None, -(2**31), 2**32 - 1),
        "timed_out": (False, True),
        "direct_join": DIRECT_JOIN_STATES,
        "descendant_cleanup": DESCENDANT_STATES,
        "response_origin": (None, *RESPONSE_ORIGINS),
        "publication": (None, "not_started", "not_published", "completed", "unknown"),
        "outcome": ("completed", "failed"),
        "primary_error": (None, *primary_references()),
    }
    for key, values in domains.items():
        size += max(len(serialize(value)) for value in values) - len(serialize(attempt[key]))
    for name in ("stdout", "stderr"):
        path = slot["planned_" + name]
        size += stream_allowance(path, subject, serialize) - len(serialize(attempt[name]))
    size += (len(ATTEMPT_ERRORS) + len(CLEANUP_ERRORS)) * (
        issue_allowance(subject, serialize) - NULL_JSON_BYTES
    )
    return (
        size + max(NULL_JSON_BYTES, RESPONSE_JSON_EXPANSION * MAX_RESPONSE_BYTES) - NULL_JSON_BYTES
    )


def original_allowance(observation: dict[str, Any], serialize: Callable[[object], bytes]) -> int:
    """Retain this exact role declaration and count both original stream observations."""
    original = new_original(observation["role_index"], observation["declared"])
    role_index = original["role_index"]
    size = len(serialize(original))
    for name in ("stdout", "stderr"):
        size += stream_allowance(
            original[name]["path"], role_index, serialize, original=True
        ) - len(serialize(original[name]))
    size += len(VALIDATION_ERRORS) * (issue_allowance(role_index, serialize) - NULL_JSON_BYTES)
    size += max(
        len(serialize(value)) for value in ("unknown", "completed", "not_started", "not_published")
    ) - len(serialize("unknown"))
    return (
        size + max(NULL_JSON_BYTES, RESPONSE_JSON_EXPANSION * MAX_RESPONSE_BYTES) - NULL_JSON_BYTES
    )
