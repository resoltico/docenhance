# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual separate-pipe producer control, timeout, join and response observation."""

from __future__ import annotations

import subprocess
import time
from typing import TYPE_CHECKING, Any

from process_execution import owned_process
from quality_attempt import CaptureAdmission, attempt_index, issue, note_issue
from quality_capture_prepare import _prepare
from quality_capture_resources import InvocationCapture, _finish
from quality_capture_response import observe_response
from quality_capture_storage import IO_ERRORS
from quality_capture_stream import CaptureReader

if TYPE_CHECKING:
    from pathlib import Path


def _notice(
    readers: list[CaptureReader], attempt: dict[str, Any], admission: CaptureAdmission
) -> bool:
    failures = False
    for reader in readers:
        failures = reader.notice(attempt, admission) or failures
    return failures


def _wait(
    process: subprocess.Popen[bytes],
    attempt: dict[str, Any],
    readers: list[CaptureReader],
    admission: CaptureAdmission,
    timeout: int,
) -> None:
    deadline = time.monotonic() + timeout
    while True:
        if _notice(readers, attempt, admission):
            return
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            attempt["timed_out"] = True
            note_issue(
                attempt,
                "errors.wait",
                issue("deadline_exceeded", attempt_index(attempt)),
                admission,
            )
            return
        try:
            process.wait(timeout=min(0.05, remaining))
        except subprocess.TimeoutExpired:
            continue
        else:
            _notice(readers, attempt, admission)
            return


def _start_readers(
    child: subprocess.Popen[bytes],
    attempt: dict[str, Any],
    admission: CaptureAdmission,
    resources: InvocationCapture,
) -> None:
    """Only actually returned threads enter the later join; partial startup retains its owner."""
    for name in ("stdout", "stderr"):
        pipe = getattr(child, name)
        if pipe is None:
            note_issue(
                attempt,
                "errors.start_" + name + "_reader",
                issue("metadata_unavailable", attempt_index(attempt)),
                admission,
            )
            return
        reader = CaptureReader(name, pipe, resources.files[name])
        resources.readers.append(reader)
        try:
            reader.start()
        except (OSError, ValueError, RuntimeError, MemoryError) as error:
            note_issue(
                attempt,
                "errors.start_" + name + "_reader",
                issue("start_" + name + "_reader", attempt_index(attempt), error),
                admission,
            )
            return


def _reap(
    process: subprocess.Popen[bytes],
    attempt: dict[str, Any],
    slot: dict[str, Any],
    admission: CaptureAdmission,
) -> None:
    """Retain actual direct-process polling status after the existing owner exits."""
    try:
        code = process.poll()
        if type(code) is int:
            attempt.update(direct_join="confirmed", launch_state="joined")
            if -(2**31) <= code < 2**32:
                attempt["exit_code"] = code
            else:
                note_issue(
                    attempt,
                    "cleanup_errors.reap_producer",
                    issue("metadata_unavailable", slot["index"]),
                    admission,
                )
        else:
            note_issue(
                attempt,
                "cleanup_errors.reap_producer",
                issue("metadata_unavailable", slot["index"]),
                admission,
            )
    except IO_ERRORS as error:
        note_issue(
            attempt,
            "cleanup_errors.reap_producer",
            issue("reap_producer", slot["index"], error),
            admission,
        )


def _run(
    attempt: dict[str, Any],
    slot: dict[str, Any],
    root: Path,
    admission: CaptureAdmission,
    resources: InvocationCapture,
) -> None:
    readers = resources.readers
    process = None
    entered = False
    attempt.update(
        launch_state="may_have_started", direct_join="unconfirmed", descendant_cleanup="unconfirmed"
    )
    if attempt["kind"] == "replay_process":
        attempt["publication"] = "unknown"
    try:
        with owned_process(
            attempt["argv"],
            _launch_environment(admission),
            (subprocess.PIPE, subprocess.PIPE),
            cwd=root,
        ) as child:
            process, entered = child, True
            resources.process = child
            attempt["launch_state"] = "child_returned"
            if type(child.pid) is int and 0 < child.pid < 2**32:
                attempt["pid"] = child.pid
            else:
                note_issue(
                    attempt,
                    "errors.launch",
                    issue("metadata_unavailable", slot["index"]),
                    admission,
                )
            try:
                _start_readers(child, attempt, admission, resources)
                if admission.is_open and len(readers) == len(("stdout", "stderr")):
                    _wait(
                        child,
                        attempt,
                        readers,
                        admission,
                        600 if slot["kind"] == "replay_process" else 120,
                    )
            except BaseException as error:
                _notice(readers, attempt, admission)
                note_issue(attempt, "errors.wait", issue("wait", slot["index"], error), admission)
                raise
            finally:
                _notice(readers, attempt, admission)
                for reader in readers:
                    reader.teardown.set()
    except (OSError, ValueError, RuntimeError, MemoryError, subprocess.SubprocessError) as error:
        _notice(readers, attempt, admission)
        reference = "cleanup_errors.stop_producer" if entered else "errors.launch"
        note_issue(
            attempt, reference, issue(reference.rsplit(".", 1)[-1], slot["index"], error), admission
        )
    finally:
        if process is not None:
            _reap(process, attempt, slot, admission)


def _launch_environment(admission: CaptureAdmission) -> dict[str, str]:
    """Copy only this invocation's already admitted immutable child environment."""
    environment = admission.environment
    if environment is None:
        message = "Capture has no admitted child environment"
        raise ValueError(message)
    return dict(environment)


def capture_attempt(
    attempt: dict[str, Any], planned_slot: dict[str, Any], root: Path, admission: CaptureAdmission
) -> None:
    """Caller persists preparation first; only an explicit admitted environment can launch."""
    if admission.environment is None:
        note_issue(
            attempt,
            "errors.prepare_argv",
            issue("metadata_unavailable", planned_slot["index"]),
            admission,
        )
        return
    if not admission.is_open:
        note_issue(
            attempt,
            "errors.prepare_directory",
            issue("admission_closed", planned_slot["index"]),
            admission,
        )
        return
    resources = InvocationCapture({})
    payload = None
    try:
        if _prepare(attempt, planned_slot, root, admission, resources.files):
            _run(attempt, planned_slot, root, admission, resources)
    except BaseException as error:
        note_issue(
            attempt,
            "errors.wait",
            issue("unexpected_exception", planned_slot["index"], error),
            admission,
        )
        raise
    finally:
        payload = _finish(resources, attempt, admission)
        if payload is not None:
            observe_response(attempt, planned_slot, payload, admission)
    complete = (
        attempt["exit_code"] == 0
        and not attempt["timed_out"]
        and attempt["direct_join"] == "confirmed"
        and attempt["response"] is not None
        and attempt["response"].get("exit_code") == 0
        and all(attempt[name]["retention_state"] == "complete" for name in ("stdout", "stderr"))
        and attempt["stderr"]["retained_bytes"] == 0
        and attempt["primary_error"] is None
    )
    if complete and (attempt["kind"] != "replay_process" or attempt["publication"] == "completed"):
        attempt["outcome"] = "completed"
