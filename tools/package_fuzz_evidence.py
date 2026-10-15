#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Package real campaign evidence or explicitly identified setup-failure diagnostics."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import tarfile
import tempfile
from pathlib import Path
from typing import TYPE_CHECKING, Any, override

if TYPE_CHECKING:
    from collections.abc import Buffer, Iterator

from fuzz_manifest import targets
from fuzz_storage import MAX_WORK_PARENT_BYTES

MAX_FILES = 50000
MAX_SOURCE_BYTES = MAX_WORK_PARENT_BYTES
MAX_ARCHIVE_BYTES = 2 * 1024 * 1024 * 1024


class ArchiveWriter(io.BufferedWriter):
    """Refuse compressed bytes beyond the disk cap, including tar/PAX metadata overhead."""

    @override
    def write(self, data: Buffer, /) -> int:
        """Check before writing so even an oversized compression block cannot cross the cap."""
        if memoryview(data).nbytes > MAX_ARCHIVE_BYTES - self.tell():
            message = f"Fuzz evidence archive exceeds {MAX_ARCHIVE_BYTES} compressed bytes"
            raise ValueError(message)
        return super().write(data)


def describe(build: Path, engine: str, source_commit: str) -> dict[str, Any]:
    """Do not promote an empty directory or a partial campaign into a success."""
    expected = {target.name for target in targets()}
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": source_commit,
        "engine": engine,
        "expected_targets": len(expected),
        "state": "setup_or_build_failed",
        "campaign_passed": False,
    }
    work = build / "fuzz-work"
    if not work.is_dir():
        return result
    campaigns = sorted(x for x in work.glob("campaign-*") if x.is_dir())
    if len(campaigns) != 1 or not (campaigns[0] / "campaign.json").is_file():
        result["state"] = "missing_or_ambiguous_campaign"
        return result
    campaign = json.loads((campaigns[0] / "campaign.json").read_text(encoding="utf-8"))
    reports: dict[str, Any] = {}
    valid = True
    for path in campaigns[0].glob("*/result.json"):
        value = json.loads(path.read_text(encoding="utf-8"))
        name = value.get("target")
        if name in reports or name not in expected:
            valid = False
        elif isinstance(name, str):
            reports[name] = value
    j = campaigns[0] / "ctest.xml"
    result.update(
        state="campaign_failed_or_incomplete",
        actual_target_reports=len(reports),
        has_junit=j.is_file(),
        campaign_passed=campaign.get("passed") is True,
    )
    if (
        campaign.get("passed") is True
        and valid
        and set(reports) == expected
        and j.is_file()
        and (campaigns[0] / "ctest.log").is_file()
        and all(value.get("passed") is True for value in reports.values())
    ):
        result["state"] = "complete_campaign"
    return result


def source_entries(build: Path) -> list[tuple[Path, str]]:
    """Inventory byte-owned regular data only; never archive symlinks or special files."""
    roots = [build / "fuzz-work", build / "Testing/Temporary"]
    entries: list[tuple[Path, str]] = []
    size = 0
    for root in roots:
        if not root.exists() and not root.is_symlink():
            continue
        for path in _walk_entries(root):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                message = f"Unsafe fuzz evidence entry: {path}"
                raise ValueError(message)
            size += path.stat().st_size if path.is_file() else 0
            if size > MAX_SOURCE_BYTES or len(entries) >= MAX_FILES:
                message = "Fuzz evidence exceeds the admitted archive budget"
                raise ValueError(message)
            entries.append((path, path.relative_to(build).as_posix()))
    return sorted(entries, key=lambda entry: entry[1])


def _walk_entries(root: Path) -> Iterator[Path]:
    """Walk incrementally so the budget is checked before collecting a whole tree."""
    yield root
    pending = [root] if root.is_dir() and not root.is_symlink() else []
    while pending:
        with os.scandir(pending.pop()) as children:
            for child in children:
                path = Path(child.path)
                yield path
                if child.is_dir(follow_symlinks=False):
                    pending.append(path)


def archive_evidence(build: Path, engine: str, commit: str, output: Path) -> dict[str, Any]:
    """Always archive a manifest; absent campaign evidence stays visibly unsuccessful."""
    if re.fullmatch("[0-9a-f]{40}", commit) is None:
        message = "Expected an immutable 40-character source SHA"
        raise ValueError(message)
    for source in (build / "fuzz-work", build / "Testing/Temporary"):
        if output.resolve().is_relative_to(source.resolve()):
            message = "Fuzz evidence archive must be outside the evidence source trees"
            raise ValueError(message)
    summary = describe(build, engine, commit)
    entries = source_entries(build)
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{output.name}-", dir=output.parent)
    temporary = Path(name)
    try:
        with (
            ArchiveWriter(os.fdopen(descriptor, "wb", buffering=0)) as destination,
            gzip.GzipFile(fileobj=destination, mode="wb", mtime=0) as compressed,
            tarfile.open(fileobj=compressed, mode="w") as archive,
        ):
            manifest = (json.dumps(summary, sort_keys=True, indent=2) + "\n").encode("utf-8")
            meta = tarfile.TarInfo("manifest.json")
            meta.size = len(manifest)
            meta.mode = 0o644
            meta.mtime = 0
            archive.addfile(meta, io.BytesIO(manifest))
            for path, member in entries:
                info = archive.gettarinfo(str(path), arcname=member)
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mtime = 0
                if path.is_file():
                    with path.open("rb") as original:
                        archive.addfile(info, original)
                else:
                    archive.addfile(info)
        temporary.replace(output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    with output.open("rb") as archive_bytes:
        summary["archive_sha256"] = hashlib.file_digest(archive_bytes, "sha256").hexdigest()
    summary["archived_entries"] = len(entries)
    return summary


def main() -> int:
    """Validate explicit provenance, retain archive bytes and summarize outcome."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--engine", choices=("afl", "libfuzzer"), required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        summary = archive_evidence(
            args.build.resolve(), args.engine, args.commit, args.output.resolve()
        )
        print(json.dumps(summary, sort_keys=True))
        if path := os.getenv("GITHUB_STEP_SUMMARY"):
            with Path(path).open("a", encoding="utf-8") as report:
                report.write(
                    f"### {args.engine} campaign evidence\n\n"
                    f"State: **{summary['state']}**; "
                    f"SHA-256: `{summary['archive_sha256']}`; "
                    f"commit: `{summary['source_commit']}`.\n"
                )
    except (OSError, ValueError, TypeError, KeyError, tarfile.TarError) as error:
        print(f"Fuzz evidence publication failed: {error}")
        return 1
    else:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
