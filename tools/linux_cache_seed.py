#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Seed only verified locked source entries; original archives stay on a read-only mount."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import stat
import sys
import tempfile
from pathlib import Path
from typing import TYPE_CHECKING

from cache_lock import exclusive_cache
from dep_verify import (
    MAX_ARCHIVE_MEMBERS,
    MAX_UNPACKED_BYTES,
    ROOT,
    Dependency,
    DependencyError,
    load_lock,
    receipt_for,
    verify,
)

if TYPE_CHECKING:
    from collections.abc import Callable

CHUNK_BYTES = 64 * 1024


def source_plan(source: Path) -> dict[str, os.stat_result]:
    """Bound a selected cache entry, including its Git metadata, before copying any bytes."""
    planned = {}
    total = 0
    for entries, path in enumerate(source.rglob("*"), start=1):
        info = path.lstat()
        if stat.S_ISREG(info.st_mode):
            total += info.st_size
        planned[str(path)] = info
        if total > MAX_UNPACKED_BYTES or entries > MAX_ARCHIVE_MEMBERS:
            msg = "Selected source cache entry exceeds existing acquisition safety bounds"
            raise DependencyError(msg)
    return planned


def source_epoch(info: os.stat_result) -> tuple[int, ...]:
    """Reads may change access time; identity, extent and mutation times may not change."""
    return (
        info.st_dev,
        info.st_ino,
        info.st_size,
        info.st_mode,
        info.st_mtime_ns,
        info.st_ctime_ns,
    )


def source_copy(planned: dict[str, os.stat_result]) -> Callable[[str, str], str]:
    """Read exactly the pre-admitted extent, refusing growth, shortening or a changed epoch."""

    def copy(source: str, target: str) -> str:
        before = planned.get(source)
        if before is None or source_epoch(Path(source).lstat()) != source_epoch(before):
            msg = "Source cache changed before seeding"
            raise DependencyError(msg)
        remaining = before.st_size
        with Path(source).open("rb") as reader, Path(target).open("xb") as writer:
            while remaining:
                chunk = reader.read(min(CHUNK_BYTES, remaining))
                if not chunk:
                    msg = "Source cache shortened during seeding"
                    raise DependencyError(msg)
                writer.write(chunk)
                remaining -= len(chunk)
            if reader.read(1) or source_epoch(Path(source).lstat()) != source_epoch(before):
                msg = "Source cache changed during seeding"
                raise DependencyError(msg)
        shutil.copystat(source, target, follow_symlinks=False)
        return target

    return copy


def source_membership(source: Path, planned: dict[str, os.stat_result]) -> None:
    """Re-enumerate only admitted names; directory timestamps cannot prove membership."""
    current: set[str] = set()
    for path in source.rglob("*"):
        name = str(path)
        if name not in planned or len(current) >= len(planned):
            msg = "Source cache membership changed during seeding"
            raise DependencyError(msg)
        current.add(name)
    if current != set(planned):
        msg = "Source cache membership changed during seeding"
        raise DependencyError(msg)


def copy_source(source: Path, target: Path) -> None:
    """Copy only pre-admitted members, refusing any directory or link epoch change."""
    root_info = source.lstat()
    planned = source_plan(source)
    target.mkdir()
    ordered = sorted(planned, key=lambda name: (len(Path(name).parts), name))
    for name in ordered:
        original = Path(name)
        info = planned[name]
        destination = target / original.relative_to(source)
        if source_epoch(original.lstat()) != source_epoch(info):
            msg = "Source cache membership changed before seeding"
            raise DependencyError(msg)
        if stat.S_ISDIR(info.st_mode):
            destination.mkdir()
        elif stat.S_ISREG(info.st_mode):
            source_copy(planned)(name, str(destination))
        elif stat.S_ISLNK(info.st_mode):
            destination.symlink_to(original.readlink())
        else:
            msg = "Source cache contains an unsupported entry kind"
            raise DependencyError(msg)
    for name in reversed(ordered):
        original = Path(name)
        info = planned[name]
        if source_epoch(original.lstat()) != source_epoch(info):
            msg = "Source cache membership changed during seeding"
            raise DependencyError(msg)
        if stat.S_ISDIR(info.st_mode):
            shutil.copystat(original, target / original.relative_to(source), follow_symlinks=False)
    source_membership(source, planned)
    if source_epoch(source.lstat()) != source_epoch(root_info):
        msg = "Source cache root changed during seeding"
        raise DependencyError(msg)
    shutil.copystat(source, target, follow_symlinks=False)


def cache_entries(cache: Path) -> set[str]:
    """A cache seed never follows or adopts a foreign cache root or partial staging tree."""
    if cache.is_symlink() or not cache.is_dir():
        msg = "Linux source cache must be its existing owned volume directory"
        raise DependencyError(msg)
    names = {path.name for path in cache.iterdir()}
    for name in names.intersection({"sources", "receipts", "archives"}):
        if (cache / name).is_symlink() or not (cache / name).is_dir():
            msg = "Linux cache directories may not redirect to another cache"
            raise DependencyError(msg)
    if names - {"sources", "receipts", "archives", "acquisition.active"}:
        msg = "Linux source cache has unowned or incomplete entries; inspect it before retrying"
        raise DependencyError(msg)
    return names


def stage_entry(dep: Dependency, host: Path, stage: Path) -> None:
    """Verify the original artifact first, then the exact copied source and receipt."""
    expected = verify(dep, host)
    source = host / "sources" / dep["name"]
    target = stage / "sources" / dep["name"]
    copy_source(source, target)
    receipt = stage / "receipts" / f"{dep['name']}.json"
    original = host / "receipts" / receipt.name
    info = original.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_UNPACKED_BYTES:
        msg = "Source receipt exceeds existing acquisition bounds or is not a regular file"
        raise DependencyError(msg)
    source_copy({str(original): info})(str(original), str(receipt))
    if receipt_for(dep, target) != expected or json.loads(receipt.read_bytes()) != expected:
        msg = f"Verified source or receipt changed while seeding {dep['name']}"
        raise DependencyError(msg)


def publish_entry(dep: Dependency, stage: Path, cache: Path) -> None:
    """Publish new names exclusively; the atomic receipt link is the readiness commit."""
    name = dep["name"]
    # Hard links share the one staged payload; no second complete source copy is allocated.
    shutil.copytree(
        stage / "sources" / name, cache / "sources" / name, copy_function=os.link, symlinks=True
    )
    os.link(stage / "receipts" / f"{name}.json", cache / "receipts" / f"{name}.json")


def discard_stage(stage: Path, primary: BaseException | None) -> None:
    """Clean only the staging tree created here without replacing an earlier refusal."""
    cleaned = False
    try:
        shutil.rmtree(stage)
        cleaned = True
    finally:
        if not cleaned and primary is not None:
            primary.add_note(f"Owned seed staging cleanup also failed: {sys.exception()!r}")
            raise primary


def seed(dependencies: list[Dependency], host: Path, cache: Path) -> bool:
    """Reuse a complete verified cache or publish one selected, exclusively owned seed."""
    cache_entries(cache)
    for path in (host, host / "sources", host / "receipts", host / "archives"):
        if path.is_symlink() or not path.is_dir():
            msg = "Host source cache and original archives must be existing owned directories"
            raise DependencyError(msg)
    if (host / "acquisition.active").exists() or (host / "acquisition.active").is_symlink():
        msg = "Host dependency acquisition is active; no source seed may race its writer"
        raise DependencyError(msg)
    with exclusive_cache(cache / "acquisition.active"):
        names = cache_entries(cache)
        if names.intersection({"sources", "receipts"}):
            for dep in dependencies:
                verify(dep, cache)
            return False
        stage = Path(tempfile.mkdtemp(prefix=".seed-", dir=cache))
        finished = False
        try:
            (stage / "sources").mkdir()
            (stage / "receipts").mkdir()
            for dep in dependencies:
                stage_entry(dep, host, stage)
            if (host / "acquisition.active").exists() or (host / "acquisition.active").is_symlink():
                msg = "Host source acquisition began during seeding"
                raise DependencyError(msg)
            (cache / "sources").mkdir()
            (cache / "receipts").mkdir()
            for dep in dependencies:
                publish_entry(dep, stage, cache)
            for dep in dependencies:
                verify(dep, cache)
            finished = True
        finally:
            discard_stage(stage, None if finished else sys.exception())
    return True


def main() -> int:
    """Do offline verification/selected seeding only, never fetch or overwrite a cache."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    try:
        dependencies = load_lock(ROOT / "deps/lock.json")["dependencies"]
        seeded = seed(dependencies, args.host, args.cache)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"FAIL: Linux locked source cache: {error}")
        return 1
    print(f"{'Seeded' if seeded else 'Verified'} {len(dependencies)} locked source entries offline")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
