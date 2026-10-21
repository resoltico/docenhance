# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Reconcile complete restoration allocation shards from bounded, full native journals."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

from native_output import ARCHIVE_LIMIT, RAW_LIMIT
from native_resource_capture import calibrations, number, trace, visibility, whole

PREFIX = "restoration-native-resources-"
CONTROLS = "restoration-native-controls"
RESOURCE_PATTERN = (
    r"^(fft-native-resources|restoration-native-controls|"
    r"restoration-native-resources-(0|[1-9][0-9]*))$"
)
VISIBILITY_FIELDS = 4
EXTENTS = ((1, 1), (1, 409), (419, 1), (129, 97), (503, 211))


def require(condition: object, message: str) -> None:
    """Refuse evidence rather than inferring work from a success exit alone."""
    if not condition:
        raise ValueError(message)


def resource_cases(tests: list[dict[str, Any]], executables: dict[str, str]) -> set[str]:
    """Bind contiguous actual shard discovery to one EXE and exact role arguments."""
    selected = [t for t in tests if t["name"].startswith("restoration-native-")]
    names = [t["name"] for t in selected]
    count = len(selected) - 1
    expected = {CONTROLS, *(PREFIX + str(i) for i in range(count))}
    require(
        count > 1 and len(names) == len(set(names)) and set(names) == expected,
        "Restoration discovery is missing, duplicate or noncontiguous",
    )
    require(
        set(executables) == {"de_fft_resources", "de_restoration_resources"},
        "Both selected resource producer paths are required",
    )
    executable = Path(executables["de_restoration_resources"]).resolve()
    for test in selected:
        name = test["name"]
        suffix = ["--controls"] if name == CONTROLS else ["--shard", name.removeprefix(PREFIX)]
        require(
            Path(test["command"][0]).resolve() == executable and test["command"][1:] == suffix,
            "Resource producer or role command differs",
        )
    fft = [t for t in tests if t["name"].startswith("fft-native")]
    require(
        len(fft) == 1
        and fft[0]["name"] == "fft-native-resources"
        and len(fft[0]["command"]) == 1
        and Path(fft[0]["command"][0]).resolve() == Path(executables["de_fft_resources"]).resolve(),
        "Missing actual FFT resource case",
    )
    return expected | {"fft-native-resources"}


def section(journal: bytes, name: str) -> bytes:
    """Extract exactly one complete raw frame; JUnit stdout is not its substitute."""
    headers = list(
        re.finditer(rb"(?m)^\d+/\d+ Testing: " + re.escape(name.encode()) + rb"\r?$", journal)
    )
    require(len(headers) == 1, "Resource full raw boundary is missing or duplicate")
    tail = journal[headers[0].end() :]
    tail = re.split(rb"(?m)^\d+/\d+ Testing: ", tail, maxsplit=1)[0]
    output = re.search(rb"\nOutput:\r?\n-+\r?\n", tail)
    if output is None:
        message = "Resource output frame is missing"
        raise ValueError(message)
    body, delimiter, _ = tail[output.end() :].partition(b"<end of output>")
    require(
        bool(delimiter) and not re.search(rb"(?m)^\d+/\d+ Testing: ", body),
        "Resource raw frame is incomplete",
    )
    return body


def row_pairs(body: bytes) -> list[tuple[dict[str, Any], bytes]]:
    """Keep exact emitted row bytes while refusing non-object or repeated-key JSON."""

    def unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        value = dict(pairs)
        require(len(value) == len(pairs), "Duplicate resource JSON key")
        return value

    result = []
    for line in body.splitlines(keepends=True):
        require(len(line) <= ARCHIVE_LIMIT, "Native resource row exceeds bounded decoder admission")
        if line.strip():
            row = json.loads(line, object_pairs_hook=unique)
            require(isinstance(row, dict), "Resource row must be an object")
            result.append((row, line))
    return result


def exact(row: dict[str, Any], expected: dict[str, Any]) -> bool:
    """Keep marker types exact as well as key/value equality."""
    return row == expected and all(type(row[k]) is type(v) for k, v in expected.items())


def sweep(pairs: list[tuple[dict[str, Any], bytes]], index: int, count: int) -> dict[str, Any]:
    """Reconcile each extent in original order, with exact local ordinal multiplicities."""
    result = {}
    position = 0
    for width, height in EXTENTS:
        visible = pairs[position : position + 4]
        require(
            len(visible) == VISIBILITY_FIELDS
            and all("source_C_provider_route" in r for r, _ in visible),
            "Missing four payload visibility probes",
        )
        for row, _ in visible:
            visibility(row)
        position += 4
        healthy, raw = pairs[position]
        whole(healthy)
        require(
            (healthy["width"], healthy["height"], healthy["fail_n"]) == (width, height, 0),
            "Healthy extent differs",
        )
        attempts = number(healthy["attempts"])
        assigned = list(range(index + 1, attempts + 1, count))
        require(bool(assigned), "Empty shard work")
        position += 1
        for ordinal in assigned:
            row, _ = pairs[position]
            whole(row)
            require(
                (row["width"], row["height"], row["fail_n"]) == (width, height, ordinal),
                "Missing, duplicate or wrong-residue resource ordinal",
            )
            position += 1
        end, _ = pairs[position]
        require(
            exact(
                end,
                {
                    "restoration_sweep_complete": True,
                    "width": width,
                    "height": height,
                    "healthy_attempts": attempts,
                    "executed_failures": len(assigned),
                },
            ),
            "Resource extent completion differs",
        )
        position += 1
        result[f"{width}x{height}"] = {
            "healthy": raw,
            "visibility": [v for _, v in visible],
            "attempts": attempts,
            "ordinals": assigned,
        }
    require(position == len(pairs), "Unexpected resource sweep rows")
    return result


def controls(pairs: list[tuple[dict[str, Any], bytes]]) -> list[dict[str, Any]]:
    """Require all extent controls and the original one/two/seven-caller overlap traces."""
    require(len(pairs) == len(EXTENTS) + 15, "Unexpected controls rows")
    position = 0
    overlaps = []
    for width, height in EXTENTS:
        require(
            exact(
                pairs[position][0],
                {"restoration_controls_extent": True, "width": width, "height": height},
            ),
            "Missing, duplicate or reordered control extent",
        )
        position += 1
    for callers in (1, 2, 7):
        require(
            all("source_C_provider_route" in row for row, _ in pairs[position : position + 4]),
            "Missing overlap visibility probes",
        )
        for probe, _ in pairs[position : position + 4]:
            visibility(probe)
        row, _ = pairs[position + 4]
        require(
            set(row)
            == {
                "whole_method_callers",
                "arrivals",
                "simultaneous_provider_live",
                "simultaneous_charge",
                "resident",
                "heap_peak",
                "charged_peak",
                "events",
            },
            "Unknown or missing overlap fields",
        )
        for key, value in row.items():
            if key != "events":
                number(value)
        require(
            row["whole_method_callers"] == callers and row["arrivals"] == callers,
            "Missing, duplicate or invalid overlap",
        )
        trace(row, callers)
        require(
            row["simultaneous_provider_live"] <= row["heap_peak"]
            and row["resident"] <= row["simultaneous_charge"] <= row["charged_peak"],
            "Overlap observation bounds differ",
        )
        overlaps.append(row)
        position += 5
    return overlaps


def _reconcile(
    tests: list[dict[str, Any]], journal: bytes, executables: dict[str, str]
) -> dict[str, Any]:
    """Reconcile the full compiled-role inventory and disjoint complete calibrated sweep union."""
    names = resource_cases(tests, executables)
    require(len(journal) <= RAW_LIMIT, "Native journal exceeds raw cap")
    observed = [
        m.group(1).decode()
        for m in re.finditer(rb"(?m)^\d+/\d+ Testing: ([^\r\n]+)", journal)
        if m.group(1).startswith((b"restoration-native", b"fft-native"))
    ]
    require(
        len(observed) == len(names) and set(observed) == names,
        "Mixed, unknown or duplicate resource raw cases",
    )
    count = len(names) - 2
    reference: dict[str, Any] | None = None
    overlap_rows = []
    for name in sorted(names):
        body = section(journal, name)
        calibrations(body)
        if name == "fft-native-resources":
            continue
        pairs = row_pairs(body)[5:]
        index = 0 if name == CONTROLS else int(name.removeprefix(PREFIX))
        role = "controls" if name == CONTROLS else "sweep"
        require(
            exact(
                pairs[0][0],
                {"restoration_resource_role": role, "shard_index": index, "shard_count": count},
            ),
            "Resource compiled role/count differs",
        )
        require(
            exact(
                pairs[-1][0],
                {"restoration_resource_complete": True, "shard_index": index, "shard_count": count},
            ),
            "Resource process completion differs",
        )
        if name == CONTROLS:
            overlap_rows = controls(pairs[1:-1])
        else:
            actual = sweep(pairs[1:-1], index, count)
            if reference is None:
                reference = actual
            for extent, data in actual.items():
                baseline = reference[extent]
                require(
                    all(data[k] == baseline[k] for k in ("healthy", "visibility", "attempts")),
                    "Serial healthy allocation mapping or visibility differs across shards",
                )
    if reference is None:
        message = "Missing restoration sweeps"
        raise ValueError(message)
    for row in overlap_rows:
        baseline = json.loads(reference["503x211"]["healthy"])
        callers = row["whole_method_callers"]
        require(
            trace(row, callers) == callers * baseline["attempts"]
            and row["heap_peak"]
            <= callers * (baseline["native_capacity"] + baseline["observed_core_payload_envelope"])
            and row["charged_peak"]
            <= row["resident"] + callers * baseline["source_scratch_peak_candidate"],
            "Overlap coverage or source envelopes differ",
        )
    return {
        "shard_count": count,
        "cases": sorted(names),
        "healthy_attempts": {k: v["attempts"] for k, v in reference.items()},
    }


def reconcile_restoration_records(
    tests: list[dict[str, Any]], journal: bytes, executables: dict[str, str]
) -> dict[str, Any]:
    """Reject missing, malformed or mixed full native records without accidental success."""
    try:
        return _reconcile(tests, journal, executables)
    except (IndexError, KeyError, TypeError, UnicodeError) as error:
        message = "Malformed or incomplete restoration resource evidence"
        raise ValueError(message) from error
