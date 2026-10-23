# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bound captured diagnostics and attest reader shutdown before closing its borrows."""

from __future__ import annotations

import contextlib
import os
import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import threading
    from collections.abc import Callable
    from typing import IO


def check_capture_reader(
    failures: list[OSError | ValueError | RuntimeError], monitor: Callable[[], None]
) -> None:
    """A background capture error takes precedence over the next workspace observation."""
    if failures:
        message = "Engine output capture failed"
        raise OSError(message) from failures[0]
    monitor()


def capture_failure(original: BaseException | None, *, complete: bool) -> BaseException | None:
    """Retain a real body failure across owned-process cleanup, ignoring ambient handlers."""
    if complete:
        return None
    current = sys.exception()
    if original is None:
        return current
    if current is not None and current is not original:
        original.add_note(f"Owned process cleanup also failed: {current!r}")
    return original


def _prefer(
    original: BaseException | None, error: BaseException | None, stage: str
) -> BaseException:
    if error is None:
        message = "Capture cleanup exception is unavailable; outcome is unconfirmed"
        raise RuntimeError(message)
    if original is None:
        return error
    if error is not original:
        original.add_note(f"{stage} also failed: {error!r}")
    return original


def _close_streams(
    source: IO[bytes] | None, sink: IO[bytes] | None, original: BaseException | None
) -> None:
    """Close each quiescent borrow once; the first actual error retains its identity."""
    source_closed = False
    try:
        if source is not None:
            source.close()
        source_closed = True
    finally:
        failure = original
        if not source_closed:
            failure = _prefer(original, sys.exception(), "Output pipe close")
        sink_closed = False
        try:
            if sink is not None:
                sink.close()
            sink_closed = True
        finally:
            if not sink_closed:
                raise _prefer(failure, sys.exception(), "Output sink close")
            if not source_closed and failure is not None:
                raise failure


def _join_reader(reader: threading.Thread | None, timeout: float) -> None:
    if reader is None:
        return
    if reader.ident is None:
        message = "Capture reader startup is unconfirmed; pipe and sink are retained"
        raise RuntimeError(message)
    reader.join(timeout=timeout)
    if reader.is_alive():
        message = "Capture reader did not finish; pipe and sink are retained"
        raise RuntimeError(message)


def _failed_reader(
    reader: threading.Thread | None,
    source: IO[bytes] | None,
    sink: IO[bytes] | None,
    original: BaseException,
) -> None:
    inspected = False
    try:
        quiescent = reader is None or (reader.ident is not None and not reader.is_alive())
        inspected = True
    finally:
        if not inspected:
            original.add_note(f"Reader state is unconfirmed: {sys.exception()!r}")
            raise original
    if not quiescent:
        original.add_note("Reader cleanup is unconfirmed; pipe and sink are retained")
        raise original
    _close_streams(source, sink, original)
    raise original


def finish_capture_reader(
    reader: threading.Thread | None,
    source: IO[bytes] | None,
    sink: IO[bytes] | None,
    timeout: float,
    original: BaseException | None = None,
) -> None:
    """Return only after confirmed reader completion and both owned stream closes."""
    joined = False
    try:
        _join_reader(reader, timeout)
        joined = True
    finally:
        if not joined:
            failure = _prefer(original, sys.exception(), "Capture reader cleanup")
            _failed_reader(reader, source, sink, failure)
    _close_streams(source, sink, original)


def _capture_bounded(source: IO[bytes], destination: IO[bytes], limit: int) -> None:
    """Retain startup and final diagnostics without storing repetitive progress forever."""
    head_limit = limit // 2
    tail_limit = limit - head_limit
    head = 0
    tail = bytearray()
    total = 0
    while data := os.read(source.fileno(), 65536):
        total += len(data)
        first = data[: max(0, head_limit - head)]
        if first:
            destination.write(first)
            destination.flush()
            head += len(first)
        tail.extend(data[len(first) :])
        if len(tail) > tail_limit:
            del tail[: len(tail) - tail_limit]
    if total > limit:
        destination.write(f"\n[engine output omitted: {total - limit} bytes]\n".encode("ascii"))
    destination.write(tail)
    destination.flush()


def capture_or_drain(
    pipe: IO[bytes],
    stream: IO[bytes],
    limit: int,
    failures: list[OSError | ValueError | RuntimeError],
) -> None:
    """Report a capture failure while keeping the pipe live until group shutdown."""
    try:
        _capture_bounded(pipe, stream, limit)
    except (OSError, ValueError, RuntimeError) as exc:
        failures.append(exc)
        with contextlib.suppress(OSError):
            while os.read(pipe.fileno(), 65536):
                pass
