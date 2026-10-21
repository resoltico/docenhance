# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual JPEG source/response publication boundary and independent metadata framing."""

from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path
from typing import Any

from codec_samples import check_output_metadata
from test_cli import RECORD_VERSION, WIRE_VERSION, call_json, expect

DATA = Path(__file__).resolve().parents[1] / "fixtures/jpeg"


def marker(code: int, payload: bytes) -> bytes:
    """Frame metadata independently of the native JPEG adapter."""
    return bytes([255, code]) + struct.pack(">H", len(payload) + 2) + payload


def prepend(source: bytes, *markers: bytes) -> bytes:
    """Insert explicit test declarations immediately after SOI."""
    return source[:2] + b"".join(markers) + source[2:]


def process(
    exe: Path, root: Path, name: str, source: bytes, *options: str
) -> tuple[dict[str, Any], bytes]:
    """A misleading PNG suffix must not change JPEG source admission."""
    input_path = root / f"{name}.png"
    input_path.write_bytes(source)
    directory = root / name
    response = call_json(
        exe, ["process", str(input_path), "--out-dir", str(directory), *options, "--json"]
    )
    record_bytes = (directory / "run.json").read_bytes()
    record = json.loads(record_bytes)
    expect(response["record"] == record, "response embeds the exact complete run record")
    expect(
        response["record_sha256"] == hashlib.sha256(record_bytes).hexdigest(),
        "record byte identity",
    )
    expect(record["output"]["verification"] == "decoded_and_compared", "actual output verification")
    expect(isinstance(response["warnings"], list), "warnings remain at response root")
    for family in (
        "illumination",
        "denoising",
        "restoration",
        "contrast",
        "sharpening",
        "binarization",
    ):
        expect(
            (record["request"][family] is None) == (record["execution"][family] is None),
            "successful Facts exist exactly for selected families",
        )
    expect(response["schema_version"] == WIRE_VERSION, "new response wire version is explicit")
    expect(
        record["record"]["version"] == RECORD_VERSION,
        "new records have explicit source observations",
    )
    expect(
        record["source"]["sha256"] == hashlib.sha256(source).hexdigest(),
        "identity is the original encoded JPEG",
    )
    expect(
        response["record"]["source"]["container"]["format"] == "jpeg",
        "signature chooses JPEG regardless of extension",
    )
    data = (directory / "result.png").read_bytes()
    check_output_metadata(response, data)
    expect(
        record["source"]["bytes"] == len(source) and record["source"]["name"] == input_path.name,
        "original source byte count and admitted filename",
    )
    expect(
        record["output"]["sha256"] == hashlib.sha256(data).hexdigest()
        and record["output"]["bytes"] == len(data),
        "actual output byte identity",
    )
    call_json(exe, ["verify", str(directory), "--json"])
    expect(input_path.read_bytes() == source, "processing preserves the source")
    return response, data


def refused(
    exe: Path,
    root: Path,
    name: str,
    source: bytes,
    error: str = "E_INPUT",
    *options: str,
) -> None:
    """Neither invalid input nor unsupported domains may leave a published/staged bundle."""
    status = {"E_INPUT": 3, "E_RESOURCE": 4, "E_ARGUMENT": 2}[error]
    input_path = root / f"{name}.jpg"
    input_path.write_bytes(source)
    directory = root / name
    response = call_json(
        exe, ["process", str(input_path), "--out-dir", str(directory), *options, "--json"], status
    )
    expect(response["error"]["code"] == error, "refusal has the specified error class")
    expect(response["publication"] == "not_started", "decode refusal precedes staging")
    expect(
        not directory.exists() and not list(root.glob(f"{name}.staging-*")),
        "decode failure has no publication",
    )
    expect(input_path.read_bytes() == source, "refusal preserves the source")
