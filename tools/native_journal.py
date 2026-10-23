# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Identify one fresh CTest journal and recognize its bounded structural byte framing."""

from __future__ import annotations

import os
import re
import stat
import sys
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

FRAGMENT_BYTES = 256
METADATA_STAGE = 2
OUTPUT_STAGE = 4

JOURNAL_NAME = "LastTest.log"
JUNIT_NAMES = ("ctest.xml", "ctest.xml.tmp")
JOURNAL_DIRECTORY_ENTRIES = 4096


def journal_status(name: str) -> str | None:
    """Recognize the exact final or default five-hex generated CTest journal name."""
    if name == JOURNAL_NAME:
        return "complete"
    if re.fullmatch(re.escape(JOURNAL_NAME) + r"\.tmp[0-9a-f]{5}", name):
        return "partial"
    return None


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
    """Use a shared path/fd epoch: Windows creation time, POSIX metadata change time."""
    if not stat.S_ISREG(info.st_mode) or (
        getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    ):
        msg = "CTest journal must be a regular file, never a symlink"
        raise OutputError(msg)
    changed = info.st_ctime_ns
    if sys.platform == "win32":
        try:
            changed = info.st_birthtime_ns
        except AttributeError as error:
            msg = "Windows file identities require nanosecond birth time"
            raise OutputError(msg) from error
    return Journal(info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, changed)


def identify_journal(path: Path) -> Journal | None:
    """Observe a regular pathname itself, permitting only an absent journal."""
    try:
        info = path.lstat()
    except FileNotFoundError:
        return None
    return journal_identity(info)


def snapshot_ctest_output(build: Path) -> Snapshot:
    """Snapshot canonical and retained generated journals through a bounded namespace walk."""
    directory = build / "Testing/Temporary"
    for parent in (build / "Testing", directory):
        try:
            info = parent.lstat()
        except FileNotFoundError:
            continue
        if not stat.S_ISDIR(info.st_mode) or (
            getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
        ):
            msg = "CTest journal parent must be an owned directory, never a symlink"
            raise OutputError(msg)
    result: Snapshot = {JOURNAL_NAME: None}
    try:
        with os.scandir(directory) as children:
            for count, child in enumerate(children, 1):
                if count > JOURNAL_DIRECTORY_ENTRIES:
                    msg = "CTest journal namespace exceeds its entry bound"
                    raise OutputError(msg)
                if journal_status(child.name) is not None:
                    result[child.name] = identify_journal(directory / child.name)
    except FileNotFoundError:
        return result
    return result


def select_journal(build: Path, before: Snapshot) -> tuple[Path, Journal, str]:
    """Require exactly one changed journal after the owned child group is reaped."""
    if (
        JOURNAL_NAME not in before
        or len(before) > JOURNAL_DIRECTORY_ENTRIES + 1
        or any(journal_status(name) is None for name in before)
        or any(value is not None and not isinstance(value, Journal) for value in before.values())
    ):
        msg = "CTest journal snapshot is incomplete"
        raise OutputError(msg)
    after = snapshot_ctest_output(build)
    fresh = [
        name for name, value in after.items() if value is not None and value != before.get(name)
    ]
    if len(fresh) != 1:
        msg = "Expected exactly one fresh CTest journal, never stale or ambiguous"
        raise OutputError(msg)
    name = fresh[0]
    identity = after[name]
    if not isinstance(identity, Journal):
        msg = "CTest journal identity is absent"
        raise OutputError(msg)
    status = journal_status(name)
    if status is None:
        msg = "CTest journal name is not admitted"
        raise OutputError(msg)
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
