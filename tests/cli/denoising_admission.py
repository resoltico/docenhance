# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Concrete D01 CLI source/publication and retained-record admission controls."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING, Any

from continuous_fixtures import Fixture, decode_output
from test_cli import RECORD_VERSION, call_json, expect

if TYPE_CHECKING:
    from pathlib import Path

SIDE = 64
OPTIONS = ["--denoise", "nlm", "--nlm-patch", "3", "--nlm-search", "7"]
D01_VERSION = 2


def process(
    exe: Path, root: Path, source: Path, options: list[str]
) -> tuple[Fixture, dict[str, Any], Path]:
    """Process to a fresh bundle, validating response and complete standalone verification."""
    out = root / f"out-{len(list(root.iterdir()))}"
    original = source.read_bytes()
    response = call_json(exe, ["process", str(source), "--out-dir", str(out), *options, "--json"])
    expect(source.read_bytes() == original, "D01 preserves exact acquired source bytes")
    call_json(exe, ["verify", str(out), "--json"])
    record = json.loads((out / "run.json").read_text())
    expect(record["record"]["version"] == RECORD_VERSION, "new record version")
    expect(record == response["record"], "response and full persistent observations agree")
    if record["request"]["denoising"] is not None:
        expect(record["execution"]["denoising"] is not None, "selected D01 actually completed")
    else:
        expect(record["execution"]["denoising"] is None, "disabled stage has no invented facts")
    return decode_output((out / "result.png").read_bytes()), response, out


def source_fixture(root: Path, fixture: Fixture) -> Path:
    """Create an independent source fixture with a unique name."""
    path = root / f"source-{len(list(root.iterdir()))}.png"
    path.write_bytes(fixture.encoded())
    return path


def record_validation(exe: Path, root: Path) -> None:
    """Only the current closed record is admitted; old versions and malformed claims are refused."""
    source = source_fixture(root, Fixture(SIDE, SIDE, ((210,),) * (SIDE * SIDE)))
    _, _, output = process(exe, root, source, OPTIONS)
    path = output / "run.json"
    record = json.loads(path.read_text())
    for version in (*range(1, RECORD_VERSION), RECORD_VERSION + 1):
        old = json.loads(json.dumps(record))
        old["record"]["version"] = version
        path.write_text(json.dumps(old))
        call_json(exe, ["verify", str(output), "--json"], 3)
    for field, value in (
        ("native_h", 100),
        ("progress", "prepared"),
        ("tile_width", 512),
        ("changed_samples", 1),
        ("preparation_charge_peak", (1 << 30) + 1),
    ):
        bad = json.loads(json.dumps(record))
        bad["execution"]["denoising"][field] = value
        path.write_text(json.dumps(bad))
        call_json(exe, ["verify", str(output), "--json"], 3)
    for field, value in (("weight_table_entries", 0), ("corrected_samples", SIDE * SIDE + 1)):
        bad = json.loads(json.dumps(record))
        bad["execution"]["denoising"][field] = value
        path.write_text(json.dumps(bad))
        call_json(exe, ["verify", str(output), "--json"], 3)
    bad = json.loads(json.dumps(record))
    expect(record["request"]["denoising"]["version"] == D01_VERSION, "healthy current D01 version")
    bad["request"]["denoising"]["version"] = 1
    path.write_text(json.dumps(bad))
    call_json(exe, ["verify", str(output), "--json"], 3)
    path.write_text(json.dumps(record))
    call_json(exe, ["verify", str(output), "--json"])
