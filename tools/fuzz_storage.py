# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Allocated-byte accounting and cooperative disk thresholds for owned fuzz work."""

from __future__ import annotations

import os
import stat
from pathlib import Path

from fuzz_manifest import FuzzError

MAX_RUN_DISK_BYTES = 256 * 1024 * 1024
MAX_WORK_PARENT_BYTES = 1536 * 1024 * 1024
MAX_WORK_ENTRIES = 49152


def entry_bytes(path: Path, *, directory: bool) -> int:
    """Inspect one racing engine entry without following or counting foreign links."""
    try:
        state = path.lstat()
    except FileNotFoundError:
        return 0  # An engine may remove or rename a queue item during the scan.
    valid = stat.S_ISDIR(state.st_mode) if directory else stat.S_ISREG(state.st_mode)
    if not valid:
        message = f"Unsafe fuzz work entry: {path}"
        raise FuzzError(message)
    return getattr(state, "st_blocks", (state.st_size + 511) // 512) * 512


def occupied_bytes(root: Path) -> int:
    """Measure allocated owned work without following links into foreign storage."""
    if not root.exists() and not root.is_symlink():
        return 0
    if root.is_symlink() or not root.is_dir():
        message = f"Unsafe fuzz work root: {root}"
        raise FuzzError(message)

    total = 0
    entries = 1
    pending = [root]
    while pending:
        directory = pending.pop()
        total += entry_bytes(directory, directory=True)
        try:
            with os.scandir(directory) as children:
                for child in children:
                    entries += 1
                    if entries > MAX_WORK_ENTRIES:
                        message = (
                            f"Fuzz work exceeds the {MAX_WORK_ENTRIES}-entry disk stop threshold"
                        )
                        raise FuzzError(message)
                    path = Path(child.path)
                    if child.is_dir(follow_symlinks=False):
                        entry_bytes(path, directory=True)
                        pending.append(path)
                    else:
                        total += entry_bytes(path, directory=False)
        except FileNotFoundError:
            continue  # A peer may finish removing its joined engine's disposable queue.
    return total


def admit_work_parent(work: Path, reserve_bytes: int = MAX_RUN_DISK_BYTES) -> None:
    """Refuse silent accumulation of previous runs; never discard their evidence."""
    consumed = occupied_bytes(work)
    if type(reserve_bytes) is not int or reserve_bytes <= 0:
        message = "Invalid fuzz work disk reservation"
        raise FuzzError(message)
    if consumed + reserve_bytes > MAX_WORK_PARENT_BYTES:
        message = (
            f"Fuzz work parent uses {consumed} bytes plus {reserve_bytes} reserved, "
            f"above {MAX_WORK_PARENT_BYTES}; "
            "review and remove obsolete run evidence before starting another campaign"
        )
        raise FuzzError(message)


def check_disk(work: Path, directory: Path) -> None:
    """Stop an owned engine after either cooperative disk threshold is exceeded."""
    consumed = occupied_bytes(directory)
    if consumed > MAX_RUN_DISK_BYTES:
        message = (
            f"Fuzz run uses {consumed} bytes, above the "
            f"{MAX_RUN_DISK_BYTES}-byte disk stop threshold"
        )
        raise FuzzError(message)
    cumulative = occupied_bytes(work)
    if cumulative > MAX_WORK_PARENT_BYTES:
        message = (
            f"Fuzz work parent uses {cumulative} bytes, above the "
            f"{MAX_WORK_PARENT_BYTES}-byte disk stop threshold"
        )
        raise FuzzError(message)


def remove_successful_scratch(directory: Path, engine: str) -> list[str]:
    """Remove joined, completed runs' disposable inputs; validate all paths before deleting any."""
    candidates = [directory / "corpus"]
    ancestors = [directory]
    if engine == "afl":
        ancestors.extend([directory / "afl", directory / "afl/default"])
    for ancestor in ancestors:
        if not stat.S_ISDIR(ancestor.lstat().st_mode):
            msg = f"Scratch ancestor is not a regular directory: {ancestor}"
            raise FuzzError(msg)
    queue = directory / "afl/default/queue"
    if engine == "afl" and (queue.exists() or queue.is_symlink()):
        candidates.append(queue)
    nodes = [path for candidate in candidates for path in [candidate, *candidate.rglob("*")]]
    identities = {}
    for path in nodes:
        state = path.lstat()
        if not (
            stat.S_ISDIR(state.st_mode) or (path not in candidates and stat.S_ISREG(state.st_mode))
        ):
            msg = f"Scratch entry is not a regular file or directory: {path}"
            raise FuzzError(msg)
        identities[path] = (state.st_dev, state.st_ino, state.st_mode)
    for path in sorted(nodes, key=lambda path: len(path.parts), reverse=True):
        state = path.lstat()
        if identities[path] != (state.st_dev, state.st_ino, state.st_mode):
            msg = f"Scratch entry changed before removal: {path}"
            raise FuzzError(msg)
        if stat.S_ISDIR(state.st_mode):
            path.rmdir()
        else:
            path.unlink()
    return [path.relative_to(directory).as_posix() for path in candidates]
