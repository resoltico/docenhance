# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bounded, identity-owned regular input inventory for fuzz evidence archives."""

from __future__ import annotations

import json
import os
import stat
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any

from fuzz_storage import MAX_WORK_PARENT_BYTES
from native_journal import journal_identity

if TYPE_CHECKING:
    from collections.abc import Iterator
    from typing import BinaryIO


MAX_FILES = 50000
MAX_SOURCE_BYTES = MAX_WORK_PARENT_BYTES


@dataclass(frozen=True)
class Entry:
    """One admitted source entry and its original filesystem observation."""

    path: Path
    member: str
    observed: os.stat_result


def stamp(value: os.stat_result) -> tuple[int, ...]:
    """Compare the admitted identity, extent and content-change timestamps."""
    return (
        value.st_dev,
        value.st_ino,
        value.st_mode,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


@contextmanager
def regular(entry: Entry) -> Iterator[BinaryIO]:
    """Own a nonblocking regular descriptor; replacements cannot redirect payload reads."""
    before = entry.path.lstat()
    if (
        not stat.S_ISREG(before.st_mode)
        or getattr(before, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
        or (before.st_dev, before.st_ino) != (entry.observed.st_dev, entry.observed.st_ino)
    ):
        message = "Fuzz evidence regular file owner changed before acquisition"
        raise ValueError(message)
    flags = getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with open(
        entry.path, "rb", buffering=0, opener=lambda path, mode: os.open(path, mode | flags)
    ) as original:
        observed = os.fstat(original.fileno())
        if not stat.S_ISREG(observed.st_mode) or (observed.st_dev, observed.st_ino) != (
            entry.observed.st_dev,
            entry.observed.st_ino,
        ):
            message = "Fuzz evidence regular file owner changed"
            raise ValueError(message)
        yield original


def metadata(entry: Entry) -> dict[str, Any]:
    """Parse only the same admitted regular file's initial extent, with one growth sentinel."""
    with regular(entry) as original:
        before = os.fstat(original.fileno())
        if journal_identity(before) != journal_identity(entry.observed):
            message = "Fuzz campaign metadata changed after inventory"
            raise ValueError(message)
        data = original.read(before.st_size + 1)
        if len(data) != before.st_size or stamp(os.fstat(original.fileno())) != stamp(before):
            message = "Fuzz campaign metadata changed during bounded acquisition"
            raise ValueError(message)
    value = json.loads(data.decode("utf-8", "strict"))
    if not isinstance(value, dict):
        message = "Fuzz campaign metadata must be an object"
        raise TypeError(message)
    return value


def source_entries(build: Path) -> list[Entry]:
    """Inventory byte-owned regular data only; never archive symlinks or special files."""
    roots = [build / "fuzz-work", build / "Testing/Temporary"]
    entries: list[Entry] = []
    size = 0
    for root in roots:
        if not root.exists() and not root.is_symlink():
            continue
        for path in _walk_entries(root):
            observed = path.lstat()
            if (
                not (stat.S_ISREG(observed.st_mode) or stat.S_ISDIR(observed.st_mode))
                or getattr(observed, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
            ):
                message = f"Unsafe fuzz evidence entry: {path}"
                raise ValueError(message)
            size += observed.st_size if stat.S_ISREG(observed.st_mode) else 0
            if size > MAX_SOURCE_BYTES or len(entries) >= MAX_FILES - 1:
                message = "Fuzz evidence exceeds the admitted archive budget"
                raise ValueError(message)
            entries.append(Entry(path, path.relative_to(build).as_posix(), observed))
    for entry in entries:
        if stat.S_ISDIR(entry.observed.st_mode):
            _check_directory(entry.path, entry.observed)
    return sorted(entries, key=lambda entry: entry.member)


def _check_directory(path: Path, observed: os.stat_result) -> None:
    """Refuse a queued directory whose actual identity or stamp changed before descendants."""
    current = path.lstat()
    if (
        not stat.S_ISDIR(current.st_mode)
        or getattr(current, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
        or stamp(current) != stamp(observed)
    ):
        message = "Fuzz evidence directory changed during inventory"
        raise ValueError(message)


def _walk_entries(root: Path) -> Iterator[Path]:
    """Walk incrementally so the budget is checked before collecting a whole tree."""
    observed = root.lstat()
    yield root
    pending = [(root, observed)] if stat.S_ISDIR(observed.st_mode) else []
    while pending:
        directory, observed = pending.pop()
        _check_directory(directory, observed)
        with os.scandir(directory) as children:
            for child in children:
                _check_directory(directory, observed)
                path = Path(child.path)
                child_state = path.lstat()
                yield path
                _check_directory(directory, observed)
                if stat.S_ISDIR(child_state.st_mode):
                    pending.append((path, child_state))
        _check_directory(directory, observed)
