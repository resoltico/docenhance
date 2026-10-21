# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Owned live capture resources and post-producer reader/raw-file completion."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any

from quality_attempt import CaptureAdmission, attempt_index, issue, note_issue
from quality_capture_storage import IO_ERRORS, finalize_file
from quality_capture_stream import finish_reader

if TYPE_CHECKING:
    import subprocess
    from typing import BinaryIO

    from quality_capture_stream import CaptureReader


@dataclass
class InvocationCapture:
    """Caller-owned resources survive exceptions before a callee can return its readers."""

    files: dict[str, BinaryIO]
    readers: list[CaptureReader] = field(default_factory=list)
    process: subprocess.Popen[bytes] | None = None


def _finish_unrepresented(
    resources: InvocationCapture, attempt: dict[str, Any], admission: CaptureAdmission
) -> None:
    """Close only destinations with no represented reader after the producer joined."""
    represented = {reader.name for reader in resources.readers}
    for name, destination in resources.files.items():
        if name in represented:
            continue
        pipe = getattr(resources.process, name, None)
        if pipe is not None:
            try:
                pipe.close()
            except IO_ERRORS as error:
                note_issue(
                    attempt,
                    "cleanup_errors.join_" + name + "_reader",
                    issue("close", attempt_index(attempt), error),
                    admission,
                )
        for phase, failure in finalize_file(attempt[name], destination).items():
            note_issue(
                attempt,
                name + ".errors." + phase,
                issue(phase, attempt_index(attempt), failure),
                admission,
            )


def _finish(
    resources: InvocationCapture, attempt: dict[str, Any], admission: CaptureAdmission
) -> bytes | None:
    """Unconfirmed producer ownership cannot finalize or close a possibly live writer's file."""
    payload = None
    if resources.process is not None and attempt["direct_join"] != "confirmed":
        for reader in resources.readers:
            reader.retention_stopped.set()
            reader.teardown.set()
            reader.notice(attempt, admission)
            candidate = reader.snapshot(attempt[reader.name])
            attempt[reader.name]["retention_state"] = "unavailable"
            if reader.name == "stdout":
                payload = candidate
        admission.retained_live.append(resources)
        return payload
    for reader in resources.readers:
        candidate = finish_reader(reader, attempt, admission)
        if reader.name == "stdout":
            payload = candidate
    _finish_unrepresented(resources, attempt, admission)
    return payload
