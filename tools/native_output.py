# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Retain one fresh complete CTest journal as verified bounded diagnostic evidence."""

from __future__ import annotations

import gzip
import hashlib
import io
import json
import os
import stat
import sys
import time
import zlib
from dataclasses import dataclass
from typing import TYPE_CHECKING, BinaryIO, TypedDict, override

from native_journal import (
    Journal,
    JournalFrame,
    OutputError,
    Snapshot,
    identify_journal,
    journal_identity,
    select_journal,
)

if TYPE_CHECKING:
    from collections.abc import Buffer
    from pathlib import Path


RAW_LIMIT = 64 * 1024 * 1024
ARCHIVE_LIMIT = 2 * 1024 * 1024
BUFFER_BYTES = 64 * 1024


class OutputReceipt(TypedDict):
    """Diagnostic provenance; never a test qualification or passing result."""

    status: str
    source: str
    raw_bytes: int
    raw_sha256: str
    archive: str
    archive_bytes: int
    archive_sha256: str
    completed_raw_case_count: int


def _deadline(deadline: float | None) -> None:
    if deadline is not None and time.monotonic() >= deadline:
        msg = "CTest diagnostic archival exhausted the owning deadline"
        raise OutputError(msg)


class _BoundedWriter(io.RawIOBase):
    """Bound every compressed write, including the gzip header and trailer."""

    def __init__(self, stream: BinaryIO, deadline: float | None) -> None:
        super().__init__()
        self.stream = stream
        self.deadline = deadline
        self.written = 0

    @override
    def write(self, data: Buffer) -> int:
        _deadline(self.deadline)
        view = memoryview(data)
        if view.nbytes > ARCHIVE_LIMIT - self.written:
            msg = "CTest compressed journal exceeds its hard writer bound"
            raise OutputError(msg)
        count = self.stream.write(view)
        if count != view.nbytes:
            msg = "Incomplete CTest compressed artifact write"
            raise OutputError(msg)
        self.written += count
        return count

    @override
    def flush(self) -> None:
        if not self.stream.closed:
            self.stream.flush()


def _check_source(descriptor: int, path: Path, identity: Journal) -> None:
    """Require the opened descriptor and current pathname to retain one source epoch."""
    if journal_identity(os.fstat(descriptor)) != identity or identify_journal(path) != identity:
        msg = "CTest journal changed while opening its owned source"
        raise OutputError(msg)


def _close_failed_source(descriptor: int, original: BaseException) -> None:
    """Close once; an opening failure keeps precedence over a later close failure."""
    closed = False
    try:
        os.close(descriptor)
        closed = True
    finally:
        if not closed:
            error = sys.exception()
            original.add_note(f"Owned source descriptor close also failed: {error!r}")
            raise original


def _source(path: Path, identity: Journal) -> BinaryIO:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0)
    descriptor = os.open(path, flags)
    try:
        _check_source(descriptor, path, identity)
        return os.fdopen(descriptor, "rb")
    except BaseException as original:
        _close_failed_source(descriptor, original)
        raise


def _compress(
    source: BinaryIO, target: _BoundedWriter, deadline: float | None
) -> tuple[int, str, bool, int]:
    total = 0
    digest = hashlib.sha256()
    frame = JournalFrame()
    with gzip.GzipFile(filename="", mode="wb", compresslevel=1, mtime=0, fileobj=target) as archive:
        while chunk := source.read(BUFFER_BYTES):
            _deadline(deadline)
            total += len(chunk)
            if total > RAW_LIMIT:
                msg = "CTest raw journal grew beyond its admission bound"
                raise OutputError(msg)
            digest.update(chunk)
            frame.feed(chunk)
            archive.write(chunk)
    return total, digest.hexdigest(), frame.complete(), frame.cases


def _write_archive(
    path: Path, identity: Journal, partial: Path, deadline: float | None
) -> tuple[int, str, bool, int]:
    with _source(path, identity) as source, partial.open("xb") as target:
        result = _compress(source, _BoundedWriter(target, deadline), deadline)
        if (
            journal_identity(os.fstat(source.fileno())) != identity
            or identify_journal(path) != identity
        ):
            msg = "CTest journal changed during archival"
            raise OutputError(msg)
    if result[0] != identity.size:
        msg = "CTest source byte count differs from its stable journal identity"
        raise OutputError(msg)
    return result


def _verify_archive(path: Path, expected: tuple[int, str], deadline: float | None) -> None:
    total = 0
    digest = hashlib.sha256()
    with gzip.open(path, "rb") as archive:
        while chunk := archive.read(min(BUFFER_BYTES, RAW_LIMIT - total + 1)):
            _deadline(deadline)
            total += len(chunk)
            if total > RAW_LIMIT:
                msg = "CTest archive roundtrip exceeds its raw admission bound"
                raise OutputError(msg)
            digest.update(chunk)
    _deadline(deadline)
    if (total, digest.hexdigest()) != expected:
        msg = "CTest gzip roundtrip differs from the complete source bytes"
        raise OutputError(msg)


def _artifact_digest(path: Path, deadline: float | None) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(BUFFER_BYTES):
            _deadline(deadline)
            digest.update(chunk)
    return digest.hexdigest()


@dataclass(frozen=True)
class _Output:
    path: Path
    identity: Journal
    status: str
    stem: str
    expected_case_count: int | None = None


def _archive_one(source: _Output, directory: Path, deadline: float | None) -> OutputReceipt:
    if source.identity.size > RAW_LIMIT:
        msg = "CTest raw artifact exceeds its admission bound"
        raise OutputError(msg)
    partial = directory / (source.stem + ".gz.partial")
    final = directory / (source.stem + ".gz")
    result = _write_archive(source.path, source.identity, partial, deadline)
    _verify_archive(partial, (result[0], result[1]), deadline)
    status = source.status
    count_mismatch = (
        source.expected_case_count is not None and result[3] != source.expected_case_count
    )
    if status == "complete" and (not result[2] or count_mismatch):
        status = "partial"
        final = directory / "ctest-output-partial.gz"
    os.link(partial, final)  # Atomic exclusive publication; never replaces an existing artifact.
    partial.unlink()
    return {
        "status": "diagnostic_only_" + status,
        "source": str(source.path),
        "raw_bytes": result[0],
        "raw_sha256": result[1],
        "archive": str(final),
        "archive_bytes": final.stat().st_size,
        "archive_sha256": _artifact_digest(final, deadline),
        "completed_raw_case_count": result[3],
    }


def _journal_output(build: Path, before: Snapshot, expected_case_count: int) -> _Output:
    path, identity, status = select_journal(build, before)
    stem = "ctest-output-partial" if status == "partial" else "ctest-output"
    return _Output(path, identity, status, stem, expected_case_count)


def _junit_output(directory: Path) -> _Output:
    path = directory / "ctest.xml"
    identity = identify_journal(path)
    if identity is None:
        msg = "Fresh owned JUnit is absent"
        raise OutputError(msg)
    return _Output(path, identity, "unvalidated_junit", "ctest.xml")


def archive_ctest_output(
    build: Path, directory: Path, before: Snapshot, deadline: float | None, expected_case_count: int
) -> dict[str, OutputReceipt]:
    """After group reaping, retain journal and raw JUnit without claiming either passed."""
    _deadline(deadline)
    if expected_case_count < 1:
        msg = "Expected journal count must come from nonempty actual discovery"
        raise OutputError(msg)
    if not stat.S_ISDIR(directory.lstat().st_mode):
        msg = "CTest archive destination must be its owned evidence directory"
        raise OutputError(msg)
    archived: dict[str, OutputReceipt] = {}
    failures: list[str] = []
    for name in ("journal", "junit"):
        try:
            source = (
                _journal_output(build, before, expected_case_count)
                if name == "journal"
                else _junit_output(directory)
            )
            archived[name] = _archive_one(source, directory, deadline)
        except (OSError, ValueError, EOFError, zlib.error) as error:
            failures.append(f"{name}: {type(error).__name__}: {error}")
    metadata = {"status": "diagnostic_only", "artifacts": archived, "errors": failures}
    metadata_name = "native-output-error.json" if failures else "native-output.json"
    try:
        with (directory / metadata_name).open("x", encoding="utf-8") as stream:
            json.dump(metadata, stream, indent=2)
            stream.write("\n")
    except OSError as error:
        failures.append(f"metadata: {error}")
    if failures:
        msg = "; ".join(failures)
        raise OutputError(msg)
    _deadline(deadline)
    return archived
