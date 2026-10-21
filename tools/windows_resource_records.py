# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read actual current-build CRT commands, maps and retained native calibration rows."""

from __future__ import annotations

import gzip
import hashlib
import json
import re
import stat
from pathlib import Path
from typing import Any

from test_evidence import report_xml
from windows_resource_artifacts import TARGETS, archive_map, identity, map_routes

CASES = ("fft-native-resources", "restoration-native-resources")
RAW_BYTES = 64 * 1024 * 1024


def calibrations(body: bytes) -> list[dict[str, Any]]:
    """Require all four actual allocation controls and the native CRT contract result."""
    rows = [
        json.loads(line)
        for line in body.splitlines()
        if line.startswith((b'{"calibration_family":', b'{"release_reservation_protocol":'))
    ]
    expected = []
    for measured in (True, False):
        for family, size in (("c", 137), ("cpp", 211)):
            expected.append(
                {
                    "calibration_family": family,
                    "measured": measured,
                    "allocated": True,
                    "acquired": size if measured else 0,
                    "backing_valid": True,
                    "attempts": int(measured),
                    "c_attempts": int(measured and family == "c"),
                    "peak": size if measured else 0,
                    "live": 0,
                }
            )
    contract = {
        "release_reservation_protocol": True,
        "native_c_zero_errno_and_injected_refusal": True,
        "provider_exception_attainment": "unavailable",
    }
    if rows != [*expected, contract]:
        message = "Actual C137/CPP211 or CRT contract rows differ"
        raise ValueError(message)
    return rows


def retained_bodies(directory: Path) -> dict[str, bytes]:
    """Admit and verify the two bounded lossless archives before parsing their payloads."""
    metadata = json.loads((directory / "native-output.json").read_text())
    if metadata["errors"]:
        message = "Native output archive has errors"
        raise ValueError(message)
    artifacts = metadata["artifacts"]
    if artifacts["journal"]["status"] != "diagnostic_only_complete":
        message = "Native journal incomplete"
        raise ValueError(message)
    bodies = {}
    for key in ("journal", "junit"):
        record = artifacts[key]
        path = directory / Path(record["archive"]).name
        if identity(path)["sha256"] != record["archive_sha256"]:
            message = "Native gzip identity differs"
            raise ValueError(message)
        with gzip.open(path, "rb") as stream:
            data = stream.read(RAW_BYTES + 1)
        if (
            len(data) > RAW_BYTES
            or len(data) != record["raw_bytes"]
            or hashlib.sha256(data).hexdigest() != record["raw_sha256"]
        ):
            message = "Native full archive identity or admission differs"
            raise ValueError(message)
        bodies[key] = data
    return bodies


def native_records(directory: Path, expected_owner: list[int]) -> dict[str, Any]:
    """Inspect full archived raw diagnostics; truncated JUnit stdout cannot replace them."""
    owner = directory.lstat()
    if not stat.S_ISDIR(owner.st_mode) or [owner.st_dev, owner.st_ino] != expected_owner:
        message = "Native evidence is not a regular owned directory"
        raise ValueError(message)
    bodies = retained_bodies(directory)
    document = report_xml(bodies["junit"].decode("utf-8"))
    found = [case for case in document.findall("testcase") if case.get("name") in CASES]
    if document.tag != "testsuite" or sorted(case.get("name", "") for case in found) != sorted(
        CASES
    ):
        message = "Expected both actual Windows resource cases exactly once"
        raise ValueError(message)
    result = {}
    for case in found:
        if case.get("status") != "run" or any(
            case.find(key) is not None for key in ("failure", "error", "skipped")
        ):
            message = "Windows resource case did not pass execution"
            raise ValueError(message)
        name = case.get("name", "")
        headers = list(
            re.finditer(
                rb"(?m)^\d+/\d+ Testing: " + re.escape(name.encode()) + rb"\r?$", bodies["journal"]
            )
        )
        if len(headers) != 1:
            message = "Resource case full raw boundary is absent or duplicate"
            raise ValueError(message)
        section = bodies["journal"][headers[0].end() :].split(b"<end of output>", 1)[0]
        result[name] = calibrations(section)
    after = directory.lstat()
    if (owner.st_dev, owner.st_ino) != (after.st_dev, after.st_ino):
        message = "Native evidence directory identity changed"
        raise ValueError(message)
    return {
        "directory": str(directory),
        "directory_identity": [owner.st_dev, owner.st_ino],
        "metadata": identity(directory / "native-output.json"),
        "cases": result,
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
