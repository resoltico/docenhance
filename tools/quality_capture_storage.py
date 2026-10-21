# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Same-descriptor bounded raw storage finalization after its actual writer joined."""

from __future__ import annotations

import hashlib
import os
import stat
from pathlib import Path
from typing import TYPE_CHECKING, Any

from quality_attempt import CHUNK_BYTES, MAX_RESPONSE_BYTES

if TYPE_CHECKING:
    from typing import BinaryIO


IO_ERRORS = (OSError, ValueError, RuntimeError, MemoryError)
STORAGE_ERRORS = (*IO_ERRORS, TypeError)


def _captured_stat(destination: BinaryIO) -> os.stat_result:
    """Bind the actual regular raw descriptor to its still-owned bounded path."""
    initial = os.fstat(destination.fileno())
    if not stat.S_ISREG(initial.st_mode) or not 0 <= initial.st_size <= MAX_RESPONSE_BYTES:
        message = "Captured raw extent is not an owned bounded regular file"
        raise ValueError(message)
    named = Path(destination.name).stat(follow_symlinks=False)
    if not stat.S_ISREG(named.st_mode) or (named.st_dev, named.st_ino) != (
        initial.st_dev,
        initial.st_ino,
    ):
        message = "Captured raw path no longer names its owned file"
        raise ValueError(message)
    return initial


def _captured_hash(destination: BinaryIO, initial: os.stat_result) -> str:
    """Hash only this descriptor's fixed initial extent plus one growth sentinel."""
    destination.seek(0)
    digest = hashlib.sha256()
    total = 0
    while total <= initial.st_size:
        chunk = destination.read(min(CHUNK_BYTES, initial.st_size + 1 - total))
        if chunk == b"":
            break
        total += len(chunk)
        digest.update(chunk)
    final = os.fstat(destination.fileno())
    if total != initial.st_size or (
        initial.st_dev,
        initial.st_ino,
        initial.st_size,
        initial.st_mtime_ns,
        initial.st_ctime_ns,
    ) != (final.st_dev, final.st_ino, final.st_size, final.st_mtime_ns, final.st_ctime_ns):
        message = "Captured raw identity changed while hashing"
        raise ValueError(message)
    return digest.hexdigest()


def finalize_file(stream: dict[str, Any], destination: BinaryIO) -> dict[str, BaseException]:
    """Call only after its writer joined; hash this same descriptor before one final close."""
    failures: dict[str, BaseException] = {}
    try:
        destination.flush()
    except STORAGE_ERRORS as error:
        failures["flush"] = error
    initial = None
    try:
        initial = _captured_stat(destination)
        stream["retained_bytes"] = initial.st_size
    except STORAGE_ERRORS as error:
        failures["stat"] = error
    if initial is not None:
        try:
            stream["sha256"] = _captured_hash(destination, initial)
        except STORAGE_ERRORS as error:
            failures["hash"] = error
    try:
        destination.close()
        stream["closed"] = True
    except STORAGE_ERRORS as error:
        stream["closed"] = None
        failures["close"] = error
    return failures
