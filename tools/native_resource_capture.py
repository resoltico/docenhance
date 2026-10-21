# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read bounded native resource archives and their actual provider calibration facts."""

from __future__ import annotations

import gzip
import hashlib
import io
import json
import os
from pathlib import Path
from typing import Any

from native_journal import identify_journal, journal_identity
from native_output import ARCHIVE_LIMIT, RAW_LIMIT

RAW_BYTES = RAW_LIMIT


def compressed_snapshot(path: Path) -> bytes:
    """Read one bounded immutable compressed epoch; hash and decode these exact bytes."""
    before = identify_journal(path)
    if before is None or before.size > ARCHIVE_LIMIT:
        message = "Native compressed archive is missing or exceeds its bound"
        raise ValueError(message)
    with path.open("rb") as stream:
        opened = os.fstat(stream.fileno())
        if journal_identity(opened) != before:
            message = "Archive identity changed while opening"
            raise ValueError(message)
        packed = stream.read(ARCHIVE_LIMIT + 1)
        after = os.fstat(stream.fileno())
        if (
            len(packed) != before.size
            or journal_identity(after) != before
            or after.st_ctime_ns != opened.st_ctime_ns
        ):
            message = "Archive identity changed during bounded snapshot"
            raise ValueError(message)
    if identify_journal(path) != before:
        message = "Archive pathname changed during snapshot"
        raise ValueError(message)
    return packed


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
        packed = compressed_snapshot(path)
        if hashlib.sha256(packed).hexdigest() != record["archive_sha256"]:
            message = "Native gzip identity differs"
            raise ValueError(message)
        with gzip.GzipFile(fileobj=io.BytesIO(packed)) as stream:
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


EVENT_FIELDS = 2
WHOLE_KEYS = frozenset(
    [
        "whole_method",
        "mode",
        "width",
        "height",
        "fft_width",
        "fft_height",
        "native_lanes",
        "row_capacity",
        "column_capacity",
        "fail_n",
        "attained",
        "attempts",
        "c_attempts",
        "observed_peak",
        "live_after",
        "observed_core_payload_envelope",
        "unobserved_core_payload_envelope",
        "payload_envelope",
        "native_capacity",
        "source_scratch_peak_candidate",
        "entering_charged",
        "ledger_peak_before",
        "ledger_peak_after",
        "events",
    ]
)


def require(condition: object, message: str) -> None:
    """Refuse malformed native allocation facts."""
    if not condition:
        raise ValueError(message)


def number(value: object) -> int:
    """Admit a nonnegative event integer without Boolean coercion."""
    if type(value) is not int or not 0 <= value <= (1 << 64) - 1:
        message = "Resource integer is invalid"
        raise ValueError(message)
    return value


def trace(row: dict[str, Any], callers: int = 1) -> int:
    """Validate every original event and its full refund, with the native logical bound."""
    events = row["events"]
    require(
        isinstance(events, list) and 0 < len(events) <= callers * 8192,
        "Resource event sequence is empty or exceeds its native bound",
    )
    live = peak = attempts = 0
    for event in events:
        require(isinstance(event, list) and len(event) == EVENT_FIELDS, "Invalid resource event")
        size, kind = map(number, event)
        require(kind in (0, 1, 2), "Unknown resource event kind")
        attempts += int(kind != 1)
        live += size if kind == 0 else -size if kind == 1 else 0
        require(live >= 0, "Resource event prefix is not owned")
        peak = max(peak, live)
    require(
        live == 0 and peak == row.get("observed_peak", row.get("heap_peak")),
        "Resource event refund/peak differs",
    )

    return attempts


def whole(row: dict[str, Any]) -> None:
    """Require attained whole-method resource refusal and the original finite envelopes."""
    require(set(row) == WHOLE_KEYS, "Unknown or missing whole-method fields")
    for key, value in row.items():
        if key not in {"whole_method", "mode", "events"}:
            number(value)
    require(
        row["whole_method"] is True and row["mode"] == "ordinary_cpp_and_registered_c",
        "Wrong whole-method mode",
    )
    require(
        row["live_after"] == 0
        and (row["fail_n"] != 0 or row["c_attempts"] > 0)
        and row["attempts"] >= row["fail_n"]
        and row["attained"] == int(row["fail_n"] != 0),
        "Unattained or unrefunded resource row",
    )
    require(
        row["observed_peak"] <= row["native_capacity"] + row["observed_core_payload_envelope"]
        and row["observed_peak"] + row["unobserved_core_payload_envelope"]
        <= row["payload_envelope"] + row["native_capacity"],
        "Resource bounds exceeded",
    )
    require(trace(row) == row["attempts"], "Resource events do not cover allocation attempts")


def visibility(row: dict[str, Any]) -> None:
    """Validate actual per-size payload probes, not arbitrary matching JSON blobs."""
    numeric = {"actual_core_payload_bytes", "observed_core_heap_bytes", "attempts", "c_attempts"}
    require(
        set(row) == numeric | {"source_C_provider_route", "core_route", "events"},
        "Unknown or missing visibility fields",
    )
    for key in numeric:
        number(row[key])
    acquired = row["observed_core_heap_bytes"]
    require(
        row["source_C_provider_route"]
        in {
            "static_debug_CRT_malloc_dbg_free_dbg",
            "static_CRT_malloc_base_free_base",
            "ELF_wrap_real_malloc_free",
            "dyld_typed_interpose_original_malloc_free",
        }
        and row["core_route"] == "Budget_allocate_scalar_aligned_nothrow64",
        "Unqualified payload provider",
    )
    require(
        row["actual_core_payload_bytes"] > 0
        and acquired in (0, row["actual_core_payload_bytes"])
        and row["attempts"] == int(acquired != 0)
        and row["c_attempts"] <= row["attempts"],
        "Visibility is neither excluded nor fully observed",
    )
    if acquired:
        require(
            trace({"events": row["events"], "observed_peak": acquired}) == 1,
            "Visibility allocation is incomplete",
        )
    else:
        require(row["events"] == [], "Excluded visibility has heap events")
