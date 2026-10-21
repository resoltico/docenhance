# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed quality observations, finite cause slots and monotonic launch ownership."""

from __future__ import annotations

import copy
import os
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from collections.abc import Mapping


MAX_RESPONSE_BYTES = 1024 * 1024
CHUNK_BYTES = 65536
ATTEMPT_ERRORS = (
    "prepare_directory",
    "prepare_argv",
    "open_stdout",
    "open_stderr",
    "prepared_persist",
    "launch",
    "start_stdout_reader",
    "start_stderr_reader",
    "wait",
    "interpret_stdout",
    "response_schema",
    "record_agreement",
)
CLEANUP_ERRORS = (
    "stop_producer",
    "reap_producer",
    "join_stdout_reader",
    "join_stderr_reader",
    "finished_persist",
)
STREAM_ERRORS = ("read", "write", "flush", "close", "stat", "hash", "overflow")
VALIDATION_ERRORS = (
    "raw_stdout",
    "raw_stderr",
    "delivery",
    "source",
    "build",
    "record",
    "role_identity",
    "repetition",
    "comparator",
    "geometry",
    "mask",
    "fidelity",
    "prior_reviews",
)
RUN_ERRORS = (
    "plan",
    "referenced_evidence",
    "work_index",
    "serialization_allowance",
    "storage_admission",
    "no_child_admission",
    "header_write",
    "header_flush",
    "append_write",
    "append_flush",
    "final_write",
    "final_flush",
    "final_close",
)
ISSUE_CODES = tuple(
    sorted(
        set(
            ATTEMPT_ERRORS
            + CLEANUP_ERRORS
            + STREAM_ERRORS
            + VALIDATION_ERRORS
            + RUN_ERRORS
            + (
                "metadata_unavailable",
                "unexpected_exception",
                "admission_closed",
                "not_eligible",
                "deadline_exceeded",
                "identity_mismatch",
                "schema_refused",
            )
        )
    )
)
ATTEMPT_KINDS = ("scored_verify", "replay_process", "replay_verify")
LAUNCH_STATES = ("not_started", "may_have_started", "child_returned", "joined")
DIRECT_JOIN_STATES = ("not_started", "unconfirmed", "confirmed")
DESCENDANT_STATES = ("not_applicable", "unconfirmed", "confirmed")
READ_STATES = ("not_started", "eof", "cut_short", "unavailable")
RETENTION_STATES = ("not_started", "complete", "prefix", "unavailable")
EXTENT_STATES = ("exact", "at_least", "unavailable")
RESPONSE_ORIGINS = ("complete_pipe_bytes", "identity_bound_file")


def primary_references() -> tuple[str, ...]:
    """The only 31 locations an actual attempt can name as its first cause."""
    return (
        tuple("errors." + key for key in ATTEMPT_ERRORS)
        + tuple("cleanup_errors." + key for key in CLEANUP_ERRORS)
        + tuple(name + ".errors." + key for name in ("stdout", "stderr") for key in STREAM_ERRORS)
    )


def issue(code: str, subject: int | None, error: BaseException | None = None) -> dict[str, Any]:
    """Keep bounded native cause facts, never arbitrary localized exception representations."""
    if code not in ISSUE_CODES or (
        subject is not None and (type(subject) is not int or subject < 0)
    ):
        message = "Invalid quality issue domain"
        raise ValueError(message)
    known_errors = (OSError, ValueError, TypeError, KeyError, RuntimeError, MemoryError)
    if error is not None and not isinstance(error, known_errors) and code != "schema_refused":
        code = "unexpected_exception"
    native_errno, native_winerror = getattr(error, "errno", None), getattr(error, "winerror", None)
    if native_errno is not None and (
        type(native_errno) is not int or not -(2**31) <= native_errno < 2**31
    ):
        code, native_errno = "metadata_unavailable", None
    if native_winerror is not None and (
        type(native_winerror) is not int or not 0 <= native_winerror < 2**32
    ):
        code, native_winerror = "metadata_unavailable", None
    return {"code": code, "errno": native_errno, "winerror": native_winerror, "subject": subject}


@dataclass
class CaptureAdmission:
    """All capture types share one gate; uncertain readers retain their real resource owners."""

    is_open: bool = True
    closure_reason: dict[str, Any] | None = None
    retained_live: list[object] = field(default_factory=list)
    environment: Mapping[str, str] | None = None

    def close(self, cause: dict[str, Any]) -> None:
        """Close once without replacing the first admission cause."""
        if self.is_open:
            self.is_open = False
            self.closure_reason = cause


def attempt_index(observation: dict[str, Any]) -> int:
    """Each attempt's only permitted cause subject is its frozen local work index."""
    role = observation["role_index"]
    if type(role) is not int:
        message = "Attempt role index is unavailable"
        raise TypeError(message)
    return 3 * role + ATTEMPT_KINDS.index(observation["kind"])


def note_issue(
    observation: dict[str, Any], reference: str, cause: dict[str, Any], admission: CaptureAdmission
) -> None:
    """Only main mutates serialized causes; preserve first notification before cleanup."""
    subject = cause["subject"]
    if "primary_error" in observation:
        expected = attempt_index(observation)
    elif "observed_publication" in observation:
        expected = observation["role_index"]
    else:
        expected = subject
    if subject is not None and subject != expected:
        admission.close(issue("unexpected_exception", None))
        message = "Quality cause subject differs from its actual observation owner"
        raise ValueError(message)
    if "primary_error" in observation and reference not in primary_references():
        admission.close(issue("unexpected_exception", None))
        message = "Unknown quality primary cause reference"
        raise ValueError(message)
    target = observation
    keys = reference.split(".")
    for key in keys[:-1]:
        if key not in target or not isinstance(target[key], dict):
            admission.close(issue("unexpected_exception", None))
            message = "Unknown quality cause container"
            raise ValueError(message)
        target = target[key]
    if keys[-1] not in target:
        admission.close(issue("unexpected_exception", None))
        message = "Unknown quality cause slot"
        raise ValueError(message)
    if target[keys[-1]] is None:
        target[keys[-1]] = cause
    if "primary_error" in observation and observation["primary_error"] is None:
        observation["primary_error"] = reference
    admission.close(cause)


def delivered_path(destination: str, name: str) -> str:
    """Preserve admitted identity bytes using the product's platform separator rule."""
    separator = "/"
    if os.name == "nt":
        index = max(destination.rfind("/"), destination.rfind("\\"))
        separator = destination[index] if index >= 0 else "\\"
    return destination + separator + name


def new_stream(path: str | None = None) -> dict[str, Any]:
    """EOF, persistence and retained-file identity have separate unavailable states."""
    return {
        "path": path,
        "read_state": "not_started",
        "retention_state": "not_started",
        "observed_prefix_bytes": None,
        "observed_extent": "unavailable",
        "retained_bytes": None,
        "sha256": None,
        "closed": None,
        "errors": dict.fromkeys(STREAM_ERRORS),
    }


def new_attempt(slot: dict[str, Any]) -> dict[str, Any]:
    """Planned names do not manufacture actual preparation, PID, joins or effects."""
    return {
        "kind": slot["kind"],
        "role_index": slot["role_index"],
        "source_slot": slot["source_slot"],
        "requested_argv": copy.deepcopy(slot["requested_argv"]),
        "argv": None,
        "artifact_directory": None,
        "launch_state": "not_started",
        "pid": None,
        "exit_code": None,
        "timed_out": False,
        "direct_join": "not_started",
        "descendant_cleanup": "not_applicable",
        "stdout": new_stream(),
        "stderr": new_stream(),
        "response": None,
        "response_origin": None,
        "publication": "not_started" if slot["kind"] == "replay_process" else None,
        "outcome": "failed",
        "primary_error": None,
        "errors": dict.fromkeys(ATTEMPT_ERRORS),
        "cleanup_errors": dict.fromkeys(CLEANUP_ERRORS),
    }


def new_original(role_index: int, declared: dict[str, Any]) -> dict[str, Any]:
    """Seven original-role fields retain declarations without inventing our launch facts."""
    execution = declared.get("execution") or {}
    return {
        "role_index": role_index,
        "declared": copy.deepcopy(declared),
        "stdout": new_stream((execution.get("stdout") or {}).get("path")),
        "stderr": new_stream((execution.get("stderr") or {}).get("path")),
        "response": None,
        "observed_publication": "unknown",
        "errors": dict.fromkeys(VALIDATION_ERRORS),
    }
