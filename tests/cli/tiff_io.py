# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual TIFF source/response publication and refusal boundary shared by concrete cases."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from codec_samples import check_output_metadata
from continuous_fixtures import Fixture, decode_output
from test_cli import RECORD_VERSION, WIRE_VERSION, call_json, expect

DATA = Path(__file__).resolve().parents[1] / "fixtures/tiff"


def process(
    exe: Path, root: Path, encoded: bytes, *options: str
) -> tuple[Fixture, dict[str, Any], bytes]:
    """Identify the exact snapshot and verify a coherent, immutable published bundle."""
    index = len(list(root.iterdir()))
    source = root / f"input-{index}.data"
    output = root / f"bundle-{index}"
    source.write_bytes(encoded)
    response = call_json(
        exe, ["process", str(source), "--out-dir", str(output), *options, "--json"]
    )
    record_bytes = (output / "run.json").read_bytes()
    record = json.loads(record_bytes)
    expect(response["record"] == record, "response embeds the exact complete TIFF record")
    expect(
        response["record_sha256"] == hashlib.sha256(record_bytes).hexdigest(),
        "record byte identity",
    )
    expect(isinstance(response["warnings"], list), "warnings are response-root diagnostics")
    expect(response["publication"] == "completed", "TIFF bundle is published")
    expect(
        response["schema_version"] == WIRE_VERSION
        and record["record"]["version"] == RECORD_VERSION,
        "current closed wire contracts",
    )
    expect(response["record"]["source"]["container"]["format"] == "tiff", "signature selects TIFF")
    expect(record["source"]["sha256"] == hashlib.sha256(encoded).hexdigest(), "snapshot identity")
    expect(record["output"]["verification"] == "decoded_and_compared", "real PNG sample comparison")
    expect(source.read_bytes() == encoded, "source remains untouched")
    result = (output / "result.png").read_bytes()
    check_output_metadata(response, result)
    expect(
        record["source"]["bytes"] == len(encoded) and record["source"]["name"] == source.name,
        "original TIFF size and name",
    )
    expect(
        record["output"]["sha256"] == hashlib.sha256(result).hexdigest()
        and record["output"]["bytes"] == len(result),
        "actual output byte identity",
    )
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
            "only actually selected families own completed Facts",
        )
    return decode_output(result), response, result


def refused(
    exe: Path,
    root: Path,
    encoded: bytes,
    error: str = "E_INPUT",
    *options: str,
    publication: str = "not_started",
) -> None:
    """Refusal reports its actual stage and preserves the source bytes."""
    index = len(list(root.iterdir()))
    source, output = root / f"bad-{index}.tif", root / f"refusal-{index}"
    source.write_bytes(encoded)
    response = call_json(
        exe,
        ["process", str(source), "--out-dir", str(output), *options, "--json"],
        {"E_INPUT": 3, "E_RESOURCE": 4, "E_ARGUMENT": 2}[error],
    )
    expect(response["error"]["code"] == error, "explicit refusal class")
    expect(
        response["publication"] == publication
        and not output.exists()
        and not list(root.glob(f"{output.name}.staging-*")),
        "no published/staged effects",
    )
    expect(source.read_bytes() == encoded, "refused source is untouched")
