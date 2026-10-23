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
import stat
import tarfile
import tempfile
from pathlib import Path
from typing import TYPE_CHECKING, Any, override

import fuzz_archive_inputs
from fuzz_manifest import targets
from native_journal import journal_identity

if TYPE_CHECKING:
    from collections.abc import Buffer


MAX_ARCHIVE_BYTES = 2 * 1024 * 1024 * 1024
CAMPAIGN_PATH_PARTS = 2


class ArchiveWriter(io.BufferedWriter):
    """Refuse compressed bytes beyond the disk cap, including tar/PAX metadata overhead."""

    @override
    def write(self, data: Buffer, /) -> int:
        """Check before writing so even an oversized compression block cannot cross the cap."""
        if memoryview(data).nbytes > MAX_ARCHIVE_BYTES - self.tell():
            message = f"Fuzz evidence archive exceeds {MAX_ARCHIVE_BYTES} compressed bytes"
            raise ValueError(message)
        return super().write(data)


def describe(
    entries: list[fuzz_archive_inputs.Entry], engine: str, source_commit: str
) -> dict[str, Any]:
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
    table = {entry.member: entry for entry in entries}
    if "fuzz-work" not in table or not stat.S_ISDIR(table["fuzz-work"].observed.st_mode):
        return result
    campaigns = [
        entry.member
        for entry in entries
        if stat.S_ISDIR(entry.observed.st_mode)
        and len(Path(entry.member).parts) == CAMPAIGN_PATH_PARTS
        and entry.member.startswith("fuzz-work/campaign-")
    ]
    if (
        len(campaigns) != 1
        or campaigns[0] + "/campaign.json" not in table
        or not stat.S_ISREG(table[campaigns[0] + "/campaign.json"].observed.st_mode)
    ):
        result["state"] = "missing_or_ambiguous_campaign"
        return result
    campaign = fuzz_archive_inputs.metadata(table[campaigns[0] + "/campaign.json"])
    reports: dict[str, Any] = {}
    valid = True
    for entry in entries:
        if (
            Path(entry.member).parent.parent.as_posix() != campaigns[0]
            or Path(entry.member).name != "result.json"
        ):
            continue
        value = fuzz_archive_inputs.metadata(entry)
        name = value.get("target")
        if name in reports or name not in expected:
            valid = False
        elif isinstance(name, str):
            reports[name] = value
    junit = table.get(campaigns[0] + "/ctest.xml")
    log = table.get(campaigns[0] + "/ctest.log")
    has_junit = junit is not None and stat.S_ISREG(junit.observed.st_mode)
    result.update(
        state="campaign_failed_or_incomplete",
        actual_target_reports=len(reports),
        has_junit=has_junit,
        campaign_passed=campaign.get("passed") is True,
    )
    if (
        campaign.get("passed") is True
        and valid
        and set(reports) == expected
        and has_junit
        and log is not None
        and stat.S_ISREG(log.observed.st_mode)
        and all(value.get("passed") is True for value in reports.values())
    ):
        result["state"] = "complete_campaign"
    return result


def _prepare_entry(
    info: tarfile.TarInfo | None,
    entry: fuzz_archive_inputs.Entry,
    remaining: int,
    observed: os.stat_result,
) -> tarfile.TarInfo:
    """Validate actual foreign tar metadata before admitting any payload copying."""
    directory = stat.S_ISDIR(entry.observed.st_mode)
    if info is None:
        message = "Fuzz archive entry changed its admitted type"
        raise ValueError(message)
    if (directory and not info.isdir()) or (not directory and not info.isreg()):
        message = "Fuzz archive entry changed its admitted type"
        raise ValueError(message)
    if info.size > remaining:
        message = "Fuzz evidence exceeds the admitted archive budget"
        raise ValueError(message)
    same_owner = (
        fuzz_archive_inputs.stamp(observed) == fuzz_archive_inputs.stamp(entry.observed)
        if directory
        else journal_identity(observed) == journal_identity(entry.observed)
    )
    if not same_owner or info.size != (0 if directory else observed.st_size):
        message = "Fuzz archive entry changed after inventory"
        raise ValueError(message)
    info.mode = stat.S_IMODE(entry.observed.st_mode)
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 0
    return info


def _add_entry(archive: tarfile.TarFile, entry: fuzz_archive_inputs.Entry, remaining: int) -> int:
    """Directory metadata and regular descriptor payloads have explicit distinct owners."""
    if stat.S_ISDIR(entry.observed.st_mode):
        info = _prepare_entry(
            archive.gettarinfo(str(entry.path), arcname=entry.member),
            entry,
            remaining,
            entry.path.lstat(),
        )
        archive.addfile(info)
        return info.size
    with fuzz_archive_inputs.regular(entry) as original:
        before = os.fstat(original.fileno())
        info = _prepare_entry(
            archive.gettarinfo(str(entry.path), arcname=entry.member, fileobj=original),
            entry,
            remaining,
            before,
        )
        archive.addfile(info, original)
        if (
            original.read(1)
            or fuzz_archive_inputs.stamp(os.fstat(original.fileno()))
            != fuzz_archive_inputs.stamp(before)
            or fuzz_archive_inputs.stamp(entry.path.lstat())
            != fuzz_archive_inputs.stamp(entry.observed)
        ):
            message = "Fuzz archive entry changed during bounded copying"
            raise ValueError(message)
        return info.size


def archive_evidence(build: Path, engine: str, commit: str, output: Path) -> dict[str, Any]:
    """Always archive a manifest; absent campaign evidence stays visibly unsuccessful."""
    if re.fullmatch("[0-9a-f]{40}", commit) is None:
        message = "Expected an immutable 40-character source SHA"
        raise ValueError(message)
    for source in (build / "fuzz-work", build / "Testing/Temporary"):
        if output.resolve().is_relative_to(source.resolve()):
            message = "Fuzz evidence archive must be outside the evidence source trees"
            raise ValueError(message)
    entries = fuzz_archive_inputs.source_entries(build)
    summary = describe(entries, engine, commit)
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{output.name}-", dir=output.parent)
    temporary = Path(name)
    try:
        with (
            ArchiveWriter(os.fdopen(descriptor, "wb", buffering=0)) as destination,
            gzip.GzipFile(fileobj=destination, mode="wb", mtime=0) as compressed,
            tarfile.open(fileobj=compressed, mode="w", dereference=True) as archive,
        ):
            manifest = (json.dumps(summary, sort_keys=True, indent=2) + "\n").encode("utf-8")
            meta = tarfile.TarInfo("manifest.json")
            meta.size = len(manifest)
            meta.mode = 0o644
            meta.mtime = 0
            archive.addfile(meta, io.BytesIO(manifest))
            source_bytes = 0
            for entry in entries:
                source_bytes += _add_entry(
                    archive, entry, fuzz_archive_inputs.MAX_SOURCE_BYTES - source_bytes
                )
        temporary.replace(output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    with output.open("rb") as archive_bytes:
        summary["archive_sha256"] = hashlib.file_digest(archive_bytes, "sha256").hexdigest()
    summary["archived_entries"] = len(entries) + 1
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
