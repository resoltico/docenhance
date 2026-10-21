# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Literal complete allocation-shard evidence shared by concrete native tooling controls."""

from __future__ import annotations

import gzip
import hashlib
import json
from pathlib import Path
from typing import Any

from tools_path import prepare_tool_imports

import restoration_resource_records as records

prepare_tool_imports()
RESOURCE_BUILD = Path("/build")


def resource_executables(build: Path = RESOURCE_BUILD) -> dict[str, str]:
    """Own the two literal expected fixture producers independently of observed commands."""
    return {
        name: str(build / "tests" / name)
        for name in ("de_fft_resources", "de_restoration_resources")
    }


RESOURCE_EXECUTABLES = resource_executables()


def calibration_rows() -> list[dict[str, Any]]:
    """Author the four literal provider facts and one C ABI contract independently."""
    return [
        {
            "calibration_family": family,
            "measured": measured,
            "allocated": 1,
            "acquired": size if measured else 0,
            "backing_valid": 1,
            "attempts": measured,
            "c_attempts": int(measured and family == "c"),
            "peak": size if measured else 0,
            "live": 0,
        }
        for measured in (1, 0)
        for family, size in (("c", 137), ("cpp", 211))
    ] + [
        {
            "release_reservation_protocol": 1,
            "native_c_zero_errno_and_injected_refusal": 1,
            "provider_exception_attainment": "unavailable",
        }
    ]


def visibility_rows() -> list[dict[str, Any]]:
    """Use four actual row shapes; these are synthetic unobserved payloads."""
    return [
        {
            "source_C_provider_route": "ELF_wrap_real_malloc_free",
            "core_route": "Budget_allocate_scalar_aligned_nothrow64",
            "actual_core_payload_bytes": size,
            "observed_core_heap_bytes": 0,
            "attempts": 0,
            "c_attempts": 0,
            "events": [],
        }
        for size in (16, 32, 48, 64)
    ]


def whole_row(width: int, height: int, ordinal: int) -> dict[str, Any]:
    """A four-attempt healthy fixture admits two independent nonempty residue classes."""
    return {
        "whole_method": True,
        "mode": "ordinary_cpp_and_registered_c",
        "width": width,
        "height": height,
        "fft_width": 128,
        "fft_height": 128,
        "native_lanes": 1,
        "row_capacity": 20,
        "column_capacity": 20,
        "fail_n": ordinal,
        "attained": int(ordinal != 0),
        "attempts": ordinal or 4,
        "c_attempts": 0 if ordinal else 2,
        "observed_peak": 0 if ordinal == 1 else 10,
        "live_after": 0,
        "observed_core_payload_envelope": 0,
        "unobserved_core_payload_envelope": 64,
        "payload_envelope": 64,
        "native_capacity": 20,
        "source_scratch_peak_candidate": 84,
        "entering_charged": 16,
        "ledger_peak_before": 16,
        "ledger_peak_after": 100,
        "events": ([[10, 0], [10, 1]] * (ordinal - 1) + [[10, 2]])
        if ordinal
        else [[10, 0], [10, 1]] * 4,
    }


def protocol_rows(index: int | None) -> list[dict[str, Any]]:
    """Literal two-shard protocol covers the original five extents and three overlaps."""
    extents = ((1, 1), (1, 409), (419, 1), (129, 97), (503, 211))
    rows = [
        *calibration_rows(),
        {
            "restoration_resource_role": "controls" if index is None else "sweep",
            "shard_index": index or 0,
            "shard_count": 2,
        },
    ]
    for width, height in extents:
        if index is None:
            rows.append({"restoration_controls_extent": True, "width": width, "height": height})
        else:
            rows.extend(visibility_rows())
            rows.append(whole_row(width, height, 0))
            rows.extend(whole_row(width, height, i) for i in (1 + index, 3 + index))
            rows.append(
                {
                    "restoration_sweep_complete": True,
                    "width": width,
                    "height": height,
                    "healthy_attempts": 4,
                    "executed_failures": 2,
                }
            )
    if index is None:
        for callers in (1, 2, 7):
            rows.extend(visibility_rows())
            rows.append(
                {
                    "whole_method_callers": callers,
                    "arrivals": callers,
                    "simultaneous_provider_live": 10 * callers,
                    "simultaneous_charge": 20 * callers,
                    "resident": 16 * callers,
                    "heap_peak": 10 * callers,
                    "charged_peak": 30 * callers,
                    "events": ([[10, 0]] * callers + [[10, 1]] * callers) * 4,
                }
            )
    rows.append(
        {"restoration_resource_complete": True, "shard_index": index or 0, "shard_count": 2}
    )
    return rows


def framed(rows: dict[str, list[dict[str, Any]]]) -> bytes:
    """Emit complete real CTest frame shapes without running an executable."""
    result = b"Start testing: fixed\n----------------------------------------------------------\n"
    for i, (name, values) in enumerate(rows.items(), 1):
        result += (
            f"{i}/{len(rows)} Testing: {name}\n{i}/{len(rows)} Test: {name}\n"
            f'Command: fixed\nDirectory: .\n"{name}" start time: fixed\nOutput:\n'
            "----------------------------------------------------------\n"
        ).encode()
        result += b"\n".join(json.dumps(row, separators=(",", ":")).encode() for row in values)
        result += (
            f"\n<end of output>\nTest time = 0.01 sec\n"
            "----------------------------------------------------------\nTest Passed.\n"
            f'"{name}" end time: fixed\n"{name}" time elapsed: 00:00:00\n'
            "----------------------------------------------------------\n\n"
        ).encode()
    return result + b"End testing: fixed\n"


def resource_fixture(build: Path = RESOURCE_BUILD) -> tuple[list[dict[str, Any]], bytes, bytes]:
    """Share one complete synthetic two-shard journal and its deliberately clipped JUnit stdout."""
    rows = {
        "fft-native-resources": calibration_rows(),
        records.CONTROLS: protocol_rows(None),
        records.PREFIX + "0": protocol_rows(0),
        records.PREFIX + "1": protocol_rows(1),
    }
    executables = resource_executables(build)
    tests = [
        {
            "name": name,
            "command": [executables["de_fft_resources"]]
            if name == "fft-native-resources"
            else [executables["de_restoration_resources"], "--controls"]
            if name == records.CONTROLS
            else [executables["de_restoration_resources"], "--shard", name[-1]],
        }
        for name in rows
    ]
    junit = (
        '<testsuite tests="4" failures="0" errors="0" skipped="0">'
        + "".join(
            f'<testcase name="{name}" status="run"><system-out>clipped</system-out></testcase>'
            for name in rows
        )
        + "</testsuite>"
    ).encode()
    return tests, framed(rows), junit


def write_archives(directory: Path, journal: bytes, junit: bytes) -> dict[str, Any]:
    """Write two complete synthetic archives with independently computed metadata."""
    artifacts = {}
    for name, body in (("journal", journal), ("junit", junit)):
        packed = gzip.compress(body)
        (directory / (name + ".gz")).write_bytes(packed)
        artifacts[name] = {
            "archive": name + ".gz",
            "archive_sha256": hashlib.sha256(packed).hexdigest(),
            "raw_bytes": len(body),
            "raw_sha256": hashlib.sha256(body).hexdigest(),
            "status": "diagnostic_only_complete",
        }
    metadata = {"errors": [], "artifacts": artifacts}
    (directory / "native-output.json").write_text(json.dumps(metadata))
    return metadata
