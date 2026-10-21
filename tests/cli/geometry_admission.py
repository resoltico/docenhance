# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Concrete G02 command and current-record refusal controls."""

from __future__ import annotations

import copy
import json
from typing import TYPE_CHECKING

from test_cli import call_json, expect

if TYPE_CHECKING:
    from pathlib import Path


def refused(exe: Path, root: Path, options: list[str], code: int = 2) -> None:
    """Rejected syntax/source configuration must not create a result directory."""
    directory = root / "refused"
    result = call_json(
        exe,
        ["process", str(root / "source.png"), "--out-dir", str(directory), *options, "--json"],
        code,
    )
    expect(
        result["publication"] == "not_started" and not directory.exists(),
        "G02 refusal precedes publication",
    )


def strict_options(exe: Path, root: Path) -> None:
    """Only canonical whole quarter-turns are admitted; duplicates never silently win."""
    for value in ("", "45", "360", "-90", "+90", "090", "90.0", "9e1", " 90", "90 ", "nan", "0x5a"):
        refused(exe, root, ["--rotate", value])
    refused(exe, root, ["--rotate", "0", "--rotate", "90"])


def malformed_records(exe: Path, directory: Path) -> None:
    """Forged rotation, obsolete version and request/report disagreement are rejected."""
    path = directory / "run.json"
    original = path.read_bytes()
    record = json.loads(original)
    for section, key, value in (
        ("record", "version", 11),
        ("request", "rotation", 45),
        ("request", "rotation", "270"),
        ("request", "rotation", True),
        ("request", "rotation", 0),
    ):
        altered = copy.deepcopy(record)
        altered[section][key] = value
        path.write_text(json.dumps(altered), encoding="utf-8")
        call_json(exe, ["verify", str(directory), "--json"], 3)
    altered = copy.deepcopy(record)
    del altered["request"]["rotation"]
    path.write_text(json.dumps(altered), encoding="utf-8")
    call_json(exe, ["verify", str(directory), "--json"], 3)
    altered = copy.deepcopy(record)
    altered["execution"]["interpretation"]["rotation"] = 0
    path.write_text(json.dumps(altered), encoding="utf-8")
    call_json(exe, ["verify", str(directory), "--json"], 3)
    path.write_bytes(original)
    call_json(exe, ["verify", str(directory), "--json"])
