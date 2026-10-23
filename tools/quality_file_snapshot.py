# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One finite initial-extent regular descriptor acquisition and truthful retained facts."""

from __future__ import annotations

import hashlib
import os
import stat
from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any, Protocol

from native_journal import journal_identity
from quality_attempt import CHUNK_BYTES

if TYPE_CHECKING:
    from pathlib import Path


@dataclass
class AcquiredFile:
    """The descriptor closes before return; only bounded observed facts and same bytes escape."""

    payload: bytes | None = None
    bytes: int | None = None
    sha256: str | None = None
    read_state: str = "unavailable"
    extent: str = "unavailable"
    closed: bool | None = None
    identity_bound: bool = False
    stable: bool = False
    failures: dict[str, BaseException] = field(default_factory=dict)


class ReadSource(Protocol):
    """The actual initial-extent reader needs only bytes and its retained descriptor."""

    def fileno(self) -> int:
        """Return the same open descriptor whose initial extent was admitted."""
        ...

    def read(self, size: int, /) -> bytes | None:
        """Observe bounded bytes or EOF without a second pathname acquisition."""
        ...


def _handle_identity(info: os.stat_result) -> tuple[int, ...]:
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


def read_extent(
    stream: ReadSource, initial: os.stat_result, retain_limit: int | None, result: AcquiredFile
) -> None:
    """Initial B is not a live EOF budget: consume at most B+1 actual bytes."""
    extent = initial.st_size
    retained = bytearray() if retain_limit is None or extent <= retain_limit else None
    digest = hashlib.sha256()
    total = 0
    result.read_state, result.extent, result.bytes = "cut_short", "at_least", 0
    while total <= extent:
        chunk = stream.read(min(CHUNK_BYTES, extent + 1 - total))
        if chunk is None:
            message = "Evidence read did not observe bytes or EOF"
            raise OSError(message)
        if chunk == b"":
            result.read_state = "eof"
            break
        total += len(chunk)
        digest.update(chunk)
        result.bytes = total if total < 2**63 else None
        if retained is not None:
            retained.extend(chunk)
    result.bytes = total if total < 2**63 else None
    result.extent = (
        ("exact" if result.read_state == "eof" else "at_least")
        if result.bytes is not None
        else "unavailable"
    )
    result.sha256 = digest.hexdigest()
    final = os.fstat(stream.fileno())
    if (
        total != extent
        or journal_identity(initial) != journal_identity(final)
        or _handle_identity(initial) != _handle_identity(final)
    ):
        message = "Evidence extent or handle changed during acquisition"
        raise ValueError(message)
    result.payload = bytes(retained) if retained is not None else None


def _acquire_regular(
    path: Path, reference: dict[str, Any], retain_limit: int | None
) -> AcquiredFile:
    """Hash exactly the same acquired bytes later parsed, never hash then reopen."""
    result = AcquiredFile()
    stream = None
    descriptor = None
    phase = "stat"
    try:
        initial_path = _regular_path(path)
        flags = (
            os.O_RDONLY
            | getattr(os, "O_BINARY", 0)
            | getattr(os, "O_NONBLOCK", 0)
            | getattr(os, "O_NOFOLLOW", 0)
        )
        descriptor = os.open(path, flags)
        stream = os.fdopen(descriptor, "rb", buffering=0)
        descriptor = None
        initial = os.fstat(stream.fileno())
        _descriptor_extent(initial_path, initial, reference)
        phase = "read"
        read_extent(stream, initial, retain_limit, result)
        phase = "stat"
        _current_path(path, initial)
        result.stable = True
        phase = "hash"
        _declared_identity(result, reference)
        result.identity_bound = True
    except (OSError, ValueError, TypeError, KeyError, MemoryError) as error:
        if phase == "read" and result.bytes is None and isinstance(error, ValueError):
            phase = "stat"
        result.failures[phase] = error
        result.payload = None
    finally:
        if stream is not None:
            try:
                stream.close()
                result.closed = True
            except (OSError, ValueError, RuntimeError, MemoryError) as error:
                result.closed = None
                result.failures["close"] = error
        if descriptor is not None:
            try:
                os.close(descriptor)
                result.closed = True
            except OSError as error:
                result.failures["close"] = error
    return result


def _regular_path(path: Path) -> os.stat_result:
    initial = path.stat(follow_symlinks=False)
    if not stat.S_ISREG(initial.st_mode):
        message = "Evidence is not a regular file"
        raise ValueError(message)
    return initial


def _descriptor_extent(
    before: os.stat_result, initial: os.stat_result, reference: dict[str, Any]
) -> None:
    if journal_identity(before) != journal_identity(initial):
        message = "Evidence path changed before acquisition"
        raise ValueError(message)
    if type(initial.st_size) is not int or not 0 <= initial.st_size < 2**63:
        message = "Unsupported evidence initial extent"
        raise ValueError(message)
    if "bytes" in reference and initial.st_size != reference["bytes"]:
        message = "Evidence initial extent differs from declared length"
        raise ValueError(message)


def _current_path(path: Path, initial: os.stat_result) -> None:
    if journal_identity(path.stat(follow_symlinks=False)) != journal_identity(initial):
        message = "Evidence path changed during acquisition"
        raise ValueError(message)


def _declared_identity(result: AcquiredFile, reference: dict[str, Any]) -> None:
    if "sha256" in reference and result.sha256 != reference["sha256"]:
        message = "Evidence payload differs from declared identity"
        raise ValueError(message)
