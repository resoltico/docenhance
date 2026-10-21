# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Bound retained Linux gate diagnostics and preserve their original failure."""

from __future__ import annotations

import os
import stat
import uuid
from typing import TYPE_CHECKING

from docker_execution import LOG_LIMIT_BYTES

if TYPE_CHECKING:
    from pathlib import Path

COMMAND_LIMIT_BYTES = 64 * 1024
RETAINED_LIMIT_BYTES = 64 * 1024 * 1024
RETAINED_LIMIT_ENTRIES = 4096


def retain_preflight_failure(log: Path, error: BaseException) -> None:
    """A diagnostic write cannot replace the actual failed gate outcome."""
    try:
        if not log.exists():
            value = f"{type(error).__name__}: {error}\n".encode("utf-8", errors="backslashreplace")
            with log.open("xb") as stream:
                stream.write(value[:LOG_LIMIT_BYTES])
    except OSError as secondary:
        error.add_note(f"Linux gate diagnostic retention also failed: {secondary!r}")


def publish_latest(log: Path, latest: Path, primary: BaseException | None) -> None:
    """Publish an immutable convenience hard link without copying or following an old symlink."""
    if not log.is_file():
        return
    temporary = latest.with_name(".latest-" + uuid.uuid4().hex)
    try:
        os.link(log, temporary)
        temporary.replace(latest)
    except OSError as error:
        if primary is None:
            raise
        primary.add_note(f"Latest diagnostic link also failed: {error!r}")


def admit_diagnostics(logs: Path) -> None:
    """Reserve one bounded run without deleting historical or unrelated evidence."""
    identities = set()
    total = 0
    for entries, path in enumerate(logs.rglob("*"), start=1):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            msg = "Linux gate diagnostics contain a redirected entry; inspect before retrying"
            raise ValueError(msg)
        identity = (info.st_dev, info.st_ino)
        if stat.S_ISREG(info.st_mode) and identity not in identities:
            identities.add(identity)
            total += info.st_size
        if (
            total + LOG_LIMIT_BYTES + COMMAND_LIMIT_BYTES > RETAINED_LIMIT_BYTES
            or entries + 4 > RETAINED_LIMIT_ENTRIES
        ):
            msg = "Linux gate retained diagnostics are full; preserve/dispose evidence explicitly"
            raise ValueError(msg)


def retain_command(path: Path, metadata: bytes) -> None:
    """Keep command metadata inside the same per-run reservation as its bounded log."""
    if len(metadata) > COMMAND_LIMIT_BYTES:
        msg = "Linux gate command metadata exceeds its retained diagnostic bound"
        raise ValueError(msg)
    path.write_bytes(metadata)
