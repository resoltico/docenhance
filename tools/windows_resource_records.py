# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read actual current-build CRT commands, maps and retained native calibration rows."""

from __future__ import annotations

import stat
from pathlib import Path
from typing import Any

from native_resource_capture import retained_bodies
from restoration_resource_cases import resource_cases
from restoration_resource_records import reconcile_restoration_records
from test_evidence import report_xml
from windows_resource_artifacts import TARGETS, archive_map, identity, map_routes


def executable_paths(producer: dict[str, Any]) -> dict[str, str]:
    """Use the selected producer's actual EXE receipts, never infer an ambient suffix."""
    return {target["target"]: target["executable"]["path"] for target in producer["targets"]}


def native_records(
    directory: Path,
    expected_owner: list[int],
    tests: list[dict[str, Any]],
    executables: dict[str, str],
) -> dict[str, Any]:
    """Inspect full archived raw diagnostics; truncated JUnit stdout cannot replace them."""
    owner = directory.lstat()
    if not stat.S_ISDIR(owner.st_mode) or [owner.st_dev, owner.st_ino] != expected_owner:
        message = "Native evidence is not a regular owned directory"
        raise ValueError(message)
    names = resource_cases(tests, executables)
    bodies = retained_bodies(directory)
    document = report_xml(bodies["junit"].decode("utf-8"))
    found = [case for case in document.findall("testcase") if case.get("name") in names]
    if document.tag != "testsuite" or sorted(case.get("name", "") for case in found) != sorted(
        names
    ):
        message = "Expected all actual Windows resource cases exactly once"
        raise ValueError(message)
    for case in found:
        if case.get("status") != "run" or any(
            case.find(key) is not None for key in ("failure", "error", "skipped")
        ):
            message = "Windows resource case did not pass execution"
            raise ValueError(message)
    result = reconcile_restoration_records(tests, bodies["journal"], executables)
    after = directory.lstat()
    if (owner.st_dev, owner.st_ino) != (after.st_dev, after.st_ino):
        message = "Native evidence directory identity changed"
        raise ValueError(message)
    return {
        "directory": str(directory),
        "directory_identity": [owner.st_dev, owner.st_ino],
        "metadata": identity(directory / "native-output.json"),
        "cases": sorted(names),
        "restoration_reconciliation": result,
    }


def capture_maps(
    build: Path, directory: Path, records: list[dict[str, Any]], *, debug: bool
) -> None:
    """Retain only actual maps; EXE and proprietary CRT/object bytes remain at producer."""
    for target in TARGETS:
        data, record = archive_map(build / "app/tests" / (target + ".map"), directory)
        records.append(record)
        record["routes"] = map_routes(data.decode("utf-8", errors="strict"), debug=debug)


def stable_producer(record: dict[str, Any]) -> None:
    """Source, produced objects, EXEs and maps must retain the recorded epoch's bytes."""
    producer = record["producer"]
    retained = [producer[key] for key in ("config_identity", "compiler", "linker")]
    retained.extend(
        target[key] for target in producer["targets"] for key in ("object", "executable")
    )
    retained.extend(item["source"] for item in record["maps"])
    for item in retained:
        if identity(Path(item["path"])) != item:
            message = "Windows resource producer bytes changed during proof"
            raise ValueError(message)


def release_directories(root: Path) -> dict[str, list[int]]:
    """Record actual regular directory identities, never choose the newest stale report."""
    directories = {}
    for path in (root / "out/release/app").glob("native-*"):
        info = path.lstat()
        if getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            message = "Native evidence owner is a Windows reparse point"
            raise ValueError(message)
        if stat.S_ISREG(info.st_mode):
            continue
        if not stat.S_ISDIR(info.st_mode):
            message = "Native evidence owner is not a regular directory"
            raise ValueError(message)
        directories[str(path.resolve())] = [info.st_dev, info.st_ino]
    return directories


def retained_owner(owner: dict[str, Any]) -> None:
    """The selected evidence directory remains the same object through completion."""
    info = Path(owner["path"]).lstat()
    if (
        getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
        or not stat.S_ISDIR(info.st_mode)
        or [info.st_dev, info.st_ino] != owner["identity"]
    ):
        message = "Selected evidence directory owner changed"
        raise ValueError(message)
