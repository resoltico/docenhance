# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual shared illumination CLI boundary and independent linear-gray fixture helpers."""

from __future__ import annotations

import json
import struct
from typing import TYPE_CHECKING, Any

from continuous_fixtures import Fixture, decode_output
from test_cli import call_json, expect

if TYPE_CHECKING:
    from pathlib import Path

WORD_MAX = 65535
DEPTH = 16
LINEAR = ((b"gAMA", struct.pack(">I", 100000)),)


def require_output(output: Fixture | None) -> Fixture:
    """Return a nonoptional result; never let optimized Python erase an assertion."""
    if output is None:
        msg = "Expected a completed output fixture"
        raise AssertionError(msg)
    return output


def run(
    exe: Path, root: Path, fixture: Fixture, options: list[str], code: int = 0
) -> tuple[Fixture | None, dict[str, Any]]:
    """Execute the real host, validate its response, and protect source bytes and existing paths."""
    source = root / "source.png"
    source.write_bytes(fixture.encoded())
    before = set(root.iterdir())
    output = root / f"out-{len(before)}"
    result = call_json(
        exe, ["process", str(source), "--out-dir", str(output), *options, "--json"], code
    )
    expect(source.read_bytes() == fixture.encoded(), "illumination never overwrites source")
    if code:
        expect(
            set(root.iterdir()) == before, "illumination refusal leaves no final or staging path"
        )
        return None, result
    record = json.loads((output / "run.json").read_bytes())
    expect(result["record"] == record, "response retains the actual complete run record")
    expect(
        record["output"]["verification"] == "decoded_and_compared",
        "output independently compared before commit",
    )
    expect(result["publication"] == "completed", "complete bundle publication")
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
            "only actual selected completed facts appear",
        )
    call_json(exe, ["verify", str(output), "--json"])
    return decode_output((output / "result.png").read_bytes()), result


def gray_fixture(width: int, height: int, values: list[float]) -> Fixture:
    """Quantized high-precision source with declared linear-gray samples."""
    return Fixture(
        width, height, tuple((round(v * WORD_MAX),) for v in values), depth=DEPTH, metadata=LINEAR
    )


def source_values(fixture: Fixture) -> list[float]:
    """Normalize declared linear-gray codes directly in the independent scalar reference."""
    return [pixel[0] / WORD_MAX for pixel in fixture.pixels]
