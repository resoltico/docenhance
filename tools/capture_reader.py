# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Attest bounded output-reader shutdown before closing its pipe and sink borrows."""

from __future__ import annotations

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
