# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Identify one fresh CTest journal and recognize its bounded structural byte framing."""

from __future__ import annotations

import re
import stat
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import os
    from pathlib import Path

FRAGMENT_BYTES = 256
METADATA_STAGE = 2
OUTPUT_STAGE = 4

JOURNALS = ("LastTest.log", "LastTest.log.tmp")


class OutputError(ValueError):
    """Full diagnostics could not be retained without ambiguity or loss."""


@dataclass(frozen=True)
class Journal:
    """A regular journal identity and mutation epoch, without following a symlink."""

    device: int
    inode: int
    size: int
    modified: int
    changed: int


Snapshot = dict[str, Journal | None]


def journal_identity(info: os.stat_result) -> Journal:
    """Return the immutable regular-file epoch without following a path."""
    if not stat.S_ISREG(info.st_mode):
        msg = "CTest journal must be a regular file, never a symlink"
        raise OutputError(msg)
    return Journal(info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def identify_journal(path: Path) -> Journal | None:
    """Observe a regular pathname itself, permitting only an absent journal."""
    try:
        return journal_identity(path.lstat())
    except FileNotFoundError:
        return None


def snapshot_ctest_output(build: Path) -> Snapshot:
    """Snapshot both possible journals before launching the owned CTest group."""
    directory = build / "Testing/Temporary"
    for parent in (build / "Testing", directory):
        try:
            info = parent.lstat()
        except FileNotFoundError:
            continue
        if not stat.S_ISDIR(info.st_mode):
            msg = "CTest journal parent must be an owned directory, never a symlink"
            raise OutputError(msg)
    return {name: identify_journal(directory / name) for name in JOURNALS}


def select_journal(build: Path, before: Snapshot) -> tuple[Path, Journal, str]:
    """Require exactly one changed journal after the owned child group is reaped."""
    if set(before) != set(JOURNALS):
        msg = "CTest journal snapshot is incomplete"
        raise OutputError(msg)
    after = snapshot_ctest_output(build)
    fresh = [name for name in JOURNALS if after[name] is not None and after[name] != before[name]]
    if len(fresh) != 1:
        msg = "Expected exactly one fresh CTest journal, never stale or ambiguous"
        raise OutputError(msg)
    name = fresh[0]
    identity = after[name]
    if not isinstance(identity, Journal):
        msg = "CTest journal identity is absent"
        raise OutputError(msg)
    status = "partial" if name.endswith(".tmp") else "complete"
    return build / "Testing/Temporary" / name, identity, status


class JournalFrame:
    """Recognize CTest's ordered outer framing while retaining no arbitrary output lines."""

    order = (
        "case",
        "title",
        "start",
        "output",
        "output_end",
        "duration",
        "result",
        "end",
        "elapsed",
    )

    def __init__(self) -> None:
        """Start with no admitted suite or completed case framing."""
        self.stage = 0
        self.cases = 0
        self.started = False
        self.finished = False
        self.invalid = False
        self.prefix = b""
        self.tail = b""
        self.length = 0

    @staticmethod
    def fixed_event(prefix: bytes, length: int) -> str | None:
        """Recognize only complete fixed delimiter lines."""
        exact = {
            b"Output:": "output",
            b"<end of output>": "output_end",
            b"Test Passed.": "result",
            b"Test Failed.": "result",
        }
        line = prefix.rstrip(b"\r")
        if length <= FRAGMENT_BYTES and line in exact:
            return exact[line]
        if length <= FRAGMENT_BYTES and line and line.strip(b"-") == b"":
            return "separator"
        return None

    @staticmethod
    def event(prefix: bytes, tail: bytes, length: int) -> str | None:
        """Recognize the concrete CTest header and trailer byte markers."""
        fixed = JournalFrame.fixed_event(prefix, length)
        if fixed is not None:
            return fixed
        patterns = (
            (rb"Start testing: .+", "begin"),
            (rb"End testing: .+", "finish"),
            (rb"[0-9]+/[0-9]+ Testing: .+", "case"),
            (rb"[0-9]+/[0-9]+ Test: .+", "title"),
            (rb"Test time = .+", "duration"),
        )
        for pattern, name in patterns:
            if re.match(pattern, prefix):
                return name
        for marker, name in (
            (b'" start time: ', "start"),
            (b'" end time: ', "end"),
            (b'" time elapsed: ', "elapsed"),
        ):
            if prefix.startswith(b'"') and marker in tail:
                return name
        return None

    def advance(self, event: str) -> None:
        """Require ordered outer case boundaries and a terminal suite footer."""
        if event == "separator":
            if self.stage == len(self.order):
                self.stage = 0
                self.cases += 1
        elif event == "finish":
            self.finished = self.stage == 0 and self.cases > 0
            self.invalid = self.invalid or not self.finished
        elif self.stage < len(self.order) and event == self.order[self.stage]:
            self.stage += 1
        else:
            self.invalid = True

    def line(self) -> None:
        """Keep output opaque except for its actual CTest end delimiter."""
        event = self.event(self.prefix, self.tail, self.length)
        if self.length == 0 or self.prefix == b"\r":
            return
        if self.stage == OUTPUT_STAGE:
            if event == "output_end":
                self.stage += 1
            return
        if not self.started:
            self.started = event == "begin"
            self.invalid = self.invalid or not self.started
        elif self.finished:
            self.invalid = True
        elif event is None:
            metadata = self.prefix.startswith((b"Command:", b"Directory:"))
            allowed = self.stage == METADATA_STAGE and metadata
            self.invalid = self.invalid or not allowed
        else:
            self.advance(event)

    def feed(self, chunk: bytes) -> None:
        """Retain at most two bounded metadata fragments across arbitrary raw lines."""
        parts = chunk.split(b"\n")
        for index, part in enumerate(parts):
            self.prefix = (self.prefix + part)[:FRAGMENT_BYTES]
            self.tail = (self.tail + part)[-FRAGMENT_BYTES:]
            self.length += len(part)
            if index < len(parts) - 1:
                self.line()
                self.prefix = b""
                self.tail = b""
                self.length = 0

    def complete(self) -> bool:
        """Require a nonempty balanced newline-terminated CTest container."""
        return self.started and self.finished and not self.invalid and self.length == 0
