# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""The two concrete capture readers own their pipes, raw files and bounded stdout candidate."""

from __future__ import annotations

import copy
import os
import threading
from typing import TYPE_CHECKING, Any

from quality_attempt import (
    CHUNK_BYTES,
    MAX_RESPONSE_BYTES,
    STREAM_ERRORS,
    CaptureAdmission,
    attempt_index,
    issue,
    note_issue,
)
from quality_capture_storage import IO_ERRORS, finalize_file

if TYPE_CHECKING:
    from typing import BinaryIO


READER_ERRORS = (*IO_ERRORS, TypeError, OverflowError)


class CaptureReader:
    """One actual pipe reader retains its thread and destination until ownership is closed."""

    def __init__(self, name: str, pipe: BinaryIO, destination: BinaryIO) -> None:
        """Prepare fixed cause and scalar state before starting the reader."""
        self.name, self.pipe, self.destination = name, pipe, destination
        self.lock = threading.Lock()
        self.teardown = threading.Event()
        self.retention_stopped = threading.Event()
        self.failures: dict[str, BaseException | None] = dict.fromkeys(STREAM_ERRORS)
        self.thread: threading.Thread | None = None
        self.started = False
        self.start_failed = False
        self.retained = 0
        self.prefix = 0
        self.extent = "at_least"
        self.read_state = "not_started"
        self.frozen = False
        self.candidate: bytearray | None = bytearray() if name == "stdout" else None
        self.complete: bytes | None = None
        self.published: dict[str, Any] = {
            "read_state": self.read_state,
            "observed_prefix_bytes": self.prefix,
            "observed_extent": self.extent,
        }
        self._publish()

    def _publish(self) -> None:
        with self.lock:
            self.published["read_state"] = self.read_state
            self.published["observed_prefix_bytes"] = self.prefix
            self.published["observed_extent"] = self.extent

    def _failure(self, phase: str, error: BaseException) -> None:
        with self.lock:
            if self.failures[phase] is None:
                self.failures[phase] = error
        self.frozen = True
        self.extent = "at_least"
        self._publish()

    def start(self) -> None:
        """Record actual startup even if thread construction or startup raises."""
        self.thread = threading.Thread(target=self._read, daemon=True)
        try:
            self.thread.start()
        except BaseException:
            self.start_failed = True
            raise
        finally:
            self.started = self.thread.ident is not None or self.thread.is_alive()

    def _keep(self, chunk: bytes) -> None:
        part = chunk[: max(0, MAX_RESPONSE_BYTES - self.retained)]
        if not part or self.failures["write"] is not None:
            return
        try:
            written = self.destination.write(part)
            if type(written) is not int or not 0 <= written <= len(part):
                message = "Capture write extent is unavailable"
                raise OSError(message)
            self.retained += written
            if written != len(part):
                message = "Capture short write"
                raise OSError(message)
        except IO_ERRORS as error:
            self._failure("write", error)

    def _consume(self, chunk: bytes) -> None:
        if self.retention_stopped.is_set():
            self.frozen = True
            self.extent = "at_least"
        if not self.frozen:
            self.prefix += len(chunk)
        candidate_overflow = (
            self.candidate is not None and len(self.candidate) + len(chunk) > MAX_RESPONSE_BYTES
        )
        if self.candidate is not None:
            if candidate_overflow:
                self.candidate = None
            else:
                self.candidate.extend(chunk)
        overflow = not self.frozen and self.prefix > MAX_RESPONSE_BYTES
        if not self.frozen:
            self._keep(chunk)
        if candidate_overflow or overflow:
            message = "Capture retained stream ceiling exceeded"
            self._failure("overflow", ValueError(message))
        self._publish()

    def _eof(self) -> None:
        self.read_state = "eof"
        if self.candidate is not None:
            self.prefix = len(self.candidate)
            self.extent = "exact"
            with self.lock:
                self.complete = bytes(self.candidate)
        elif not self.frozen:
            self.extent = "exact"
        self._publish()

    def _read(self) -> None:
        self.read_state = "cut_short"
        self._publish()
        try:
            while True:
                if self.frozen:
                    self.teardown.wait()
                chunk = os.read(self.pipe.fileno(), CHUNK_BYTES)
                if not chunk:
                    self._eof()
                    return
                self._consume(chunk)
        except READER_ERRORS as error:
            self._failure("read", error)

    def notice(self, attempt: dict[str, Any], admission: CaptureAdmission) -> bool:
        """Publish pending causes on main with a fixed per-stream tie order."""
        with self.lock:
            failures = dict(self.failures)
        for phase in STREAM_ERRORS:
            if failures[phase] is not None:
                note_issue(
                    attempt,
                    self.name + ".errors." + phase,
                    issue(phase, attempt_index(attempt), failures[phase]),
                    admission,
                )
        return any(error is not None for error in failures.values())

    def snapshot(self, stream: dict[str, Any]) -> bytes | None:
        """Copy only synchronized scalar facts and an actually complete stdout candidate."""
        with self.lock:
            stream.update(copy.deepcopy(self.published))
            return self.complete

    def join(self, attempt: dict[str, Any], admission: CaptureAdmission) -> bool:
        """Join only a returned thread, retaining real owners if that cannot be confirmed."""
        self.teardown.set()
        if self.started and self.thread is not None:
            try:
                self.thread.join(timeout=10)
                if self.thread.is_alive():
                    message = "Capture reader did not join"
                    raise RuntimeError(message)
            except IO_ERRORS as error:
                self.retention_stopped.set()
                note_issue(
                    attempt,
                    "cleanup_errors.join_" + self.name + "_reader",
                    issue("join_" + self.name + "_reader", attempt_index(attempt), error),
                    admission,
                )
                admission.retained_live.append(self)
                return False
        elif self.start_failed:
            note_issue(
                attempt,
                "cleanup_errors.join_" + self.name + "_reader",
                issue("metadata_unavailable", attempt_index(attempt)),
                admission,
            )
            admission.retained_live.append(self)
            return False
        return True


def finish_reader(
    reader: CaptureReader, attempt: dict[str, Any], admission: CaptureAdmission
) -> bytes | None:
    """Finalize a destination only after its actual reader has stopped."""
    joined = reader.join(attempt, admission)
    reader.notice(attempt, admission)
    stream = attempt[reader.name]
    complete = reader.snapshot(stream)
    if not joined:
        stream["retention_state"] = "unavailable"
        return complete
    if reader.pipe is not None:
        try:
            reader.pipe.close()
        except IO_ERRORS as error:
            note_issue(
                attempt,
                "cleanup_errors.join_" + reader.name + "_reader",
                issue("close", attempt_index(attempt), error),
                admission,
            )
    failures = finalize_file(stream, reader.destination)
    for phase, failure in failures.items():
        note_issue(
            attempt,
            reader.name + ".errors." + phase,
            issue(phase, attempt_index(attempt), failure),
            admission,
        )
    coherent = (
        stream["read_state"] == "eof"
        and stream["observed_extent"] == "exact"
        and stream["retained_bytes"] == stream["observed_prefix_bytes"]
        and stream["sha256"] is not None
        and stream["closed"] is True
        and not any(stream["errors"].values())
    )
    stream["retention_state"] = "complete" if coherent else "prefix"
    return complete
