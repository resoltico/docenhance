# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Allocated-byte accounting and cooperative disk thresholds for owned fuzz work."""

from __future__ import annotations

import os
import stat
from pathlib import Path

from fuzz_manifest import FuzzError

MAX_RUN_DISK_BYTES = 128 * 1024 * 1024
MAX_WORK_PARENT_BYTES = 1024 * 1024 * 1024


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

    def fail_walk(error: OSError) -> None:
        raise error

    total = 0
    for base, directories, files in os.walk(root, followlinks=False, onerror=fail_walk):
        total += entry_bytes(Path(base), directory=True)
        for name in directories:
            entry_bytes(Path(base) / name, directory=True)
        for name in files:
            total += entry_bytes(Path(base) / name, directory=False)
    return total


def admit_work_parent(work: Path, reserve_bytes: int = MAX_RUN_DISK_BYTES) -> None:
    """Refuse silent accumulation of previous runs; never discard their evidence."""
    consumed = occupied_bytes(work)
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
