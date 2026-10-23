# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Admit and sample retained native diagnostics without deleting earlier evidence."""

from __future__ import annotations

import os
import stat
import sys
import time
from contextlib import contextmanager
from typing import TYPE_CHECKING

from cache_lock import exclusive_cache
from native_journal import JUNIT_NAMES, journal_status
from native_output import ARCHIVE_LIMIT
from native_output import RAW_LIMIT as RAW_BYTES
from test_evidence import EvidenceError

if TYPE_CHECKING:
    from collections.abc import Iterator
    from pathlib import Path

MIB = 1024 * 1024
HISTORY_BYTES = 384 * MIB
HISTORY_ENTRIES = 4096
RUN_RESERVE_ENTRIES = 32
ARCHIVE_RESERVE_ENTRIES = 16
METADATA_BYTES = 4 * MIB
ARCHIVE_RESERVE_BYTES = 2 * ARCHIVE_LIMIT + METADATA_BYTES
RUN_RESERVE_BYTES = 2 * RAW_BYTES + ARCHIVE_RESERVE_BYTES
RUN_BYTES = RAW_BYTES + ARCHIVE_RESERVE_BYTES


def regular_directory(path: Path) -> os.stat_result:
    """Inspect the path itself before any resolution or recursive traversal."""
    info = path.lstat()
    if (
        not stat.S_ISDIR(info.st_mode)
        or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    ):
        message = f"Native history directory is redirected or nonregular: {path}"
        raise ValueError(message)
    return info


def regular_entry(child: os.DirEntry[str]) -> os.stat_result | None:
    """Allow CTest journal renames but never traverse a known redirected or special entry."""
    try:
        info = child.stat(follow_symlinks=False)
    except FileNotFoundError:
        return None
    if (
        stat.S_ISLNK(info.st_mode)
        or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
        or not (stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode))
    ):
        message = f"Native history contains a redirected or special entry: {child.path}"
        raise ValueError(message)
    return info


class NativeHistory:
    """One serialized native run owns the original deadline and its history reservation."""

    def __init__(self, build: Path, deadline: float, directory: Path | None = None) -> None:
        """Keep only the concrete native build, current evidence owner and original clock."""
        self.build = build
        self.deadline = deadline
        self.directory = directory
        self.samples = self.peak_bytes = self.peak_entries = 0
        self._entries = 0
        self._identities: dict[tuple[int, int], int] = {}
        self._files: dict[Path, tuple[tuple[int, int], int]] = {}

    def _count(self) -> None:
        if time.monotonic() >= self.deadline:
            message = "Native history exhausted the original execution deadline"
            raise EvidenceError(message)
        self._entries += 1
        if self._entries > HISTORY_ENTRIES:
            message = "Native history exceeds4096 entries; preserve/dispose explicitly"
            raise ValueError(message)

    @staticmethod
    def _selected(name: str, scope: str) -> bool:
        if scope == "root":
            return name.startswith("native-") or name == "Testing"
        return scope != "testing" or name == "Temporary"

    def _record(self, path: Path, info: os.stat_result) -> None:
        if info.st_ino == 0:
            message = "Native history file has no usable inode identity"
            raise ValueError(message)
        identity = (info.st_dev, info.st_ino)
        self._identities[identity] = max(self._identities.get(identity, 0), info.st_size)
        self._files[path] = (identity, info.st_size)

    def _walk(self, directory: Path, scope: str) -> None:
        before = regular_directory(directory)
        with os.scandir(directory) as children:
            for child in children:
                self._count()
                if not self._selected(child.name, scope):
                    continue
                info = regular_entry(child)
                if info is None:
                    continue
                path = directory / child.name
                if (
                    scope == "root"
                    and child.name == "native-tests.json"
                    and stat.S_ISREG(info.st_mode)
                ):
                    continue
                if stat.S_ISDIR(info.st_mode):
                    if scope == "history":
                        message = f"Native evidence owners must be flat directories: {path}"
                        raise ValueError(message)
                    next_scope = (
                        "testing" if scope == "root" and child.name == "Testing" else "history"
                    )
                    self._walk(path, next_scope)
                elif scope != "history":
                    message = f"Native history owner must be a directory: {path}"
                    raise ValueError(message)
                else:
                    self._record(path, info)
        after = regular_directory(directory)
        if (after.st_dev, after.st_ino) != (before.st_dev, before.st_ino):
            message = f"Native history directory identity changed during traversal: {directory}"
            raise ValueError(message)

    def _kind(self, path: Path) -> str | None:
        if self.directory is not None and path.is_relative_to(self.directory):
            if path.name in JUNIT_NAMES:
                return "raw"
            return "archive" if path.name.endswith((".gz", ".gz.partial")) else "metadata"
        if path.is_relative_to(self.build / "Testing/Temporary"):
            return "raw" if journal_status(path.name) is not None else "metadata"
        return None

    def _current(self) -> None:
        owned: dict[tuple[int, int], int] = {}
        metadata: dict[tuple[int, int], int] = {}
        for path, (identity, size) in self._files.items():
            if self.directory is not None and path.is_relative_to(self.directory):
                owned[identity] = max(owned.get(identity, 0), size)
            kind = self._kind(path)
            if kind == "raw" and size > RAW_BYTES:
                message = f"Current native raw JUnit/journal exceeds64MiB: {path}"
                raise ValueError(message)
            if kind == "metadata":
                metadata[identity] = max(metadata.get(identity, 0), size)
        if sum(metadata.values()) > METADATA_BYTES or sum(owned.values()) > RUN_BYTES:
            message = "Current native metadata exceeds4MiB or run owner exceeds72MiB"
            raise ValueError(message)

    def _external(self, directory: Path) -> None:
        try:
            regular_directory(directory.parent)
            regular_directory(directory)
        except FileNotFoundError:
            return
        self._count()
        self._walk(directory, "history")

    def _check(self, reserve_bytes: int, reserve_entries: int) -> None:
        self._entries = 0
        self._count()
        self._identities.clear()
        self._files.clear()
        self._walk(self.build, "root")
        external = set()
        if self.build.name == "app" and self.build.parent.name == "release":
            # The same retained owner remains charged during the later full Release suite.
            external.add(self.build.parent.parent / "windows-resource-proof/release-preflight")
        if self.directory is not None and not self.directory.is_relative_to(self.build):
            external.add(self.directory)
        for directory in sorted(external):
            self._external(directory)
        total = sum(self._identities.values())
        self.samples += 1
        self.peak_bytes = max(self.peak_bytes, total)
        self.peak_entries = max(self.peak_entries, self._entries)
        if (
            total + reserve_bytes > HISTORY_BYTES
            or self._entries + reserve_entries > HISTORY_ENTRIES
        ):
            message = (
                f"Native retained history is full: {total} bytes/{self._entries} entries; "
                f"reserve{reserve_bytes} bytes/{reserve_entries} entries within384MiB/4096; "
                "preserve/dispose evidence explicitly"
            )
            raise ValueError(message)
        self._current()

    def admit(self) -> None:
        """Reserve the complete next run before creating any evidence owner or child."""
        self._check(RUN_RESERVE_BYTES, RUN_RESERVE_ENTRIES)

    def check(self) -> None:
        """Sample child growth, leaving bounded archive/error-metadata headroom."""
        self._check(ARCHIVE_RESERVE_BYTES, ARCHIVE_RESERVE_ENTRIES)

    def final(self) -> None:
        """Observe all retained raw/compressed outputs before releasing the writer claim."""
        self._check(0, 0)


@contextmanager
def owned_native_history(
    build: Path, deadline: float, directory: Path | None = None
) -> Iterator[NativeHistory]:
    """Serialize standard suites and the concrete external FFT preflight through qualification."""
    regular_directory(build)
    with exclusive_cache(build / ".native-history-active"):
        history = NativeHistory(build, deadline)
        history.admit()
        if directory is not None:
            regular_directory(directory.parent)
            directory.mkdir()
            history.directory = directory
            history.check()
        try:
            yield history
        finally:
            primary = sys.exception()
            try:
                history.final()
            except (OSError, ValueError) as error:
                if primary is None:
                    raise
                primary.add_note(f"Final native history guard also failed: {error!r}")
