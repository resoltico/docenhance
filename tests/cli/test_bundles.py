#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent record-schema and adversarial artifact tests against the real executable."""

from __future__ import annotations

import copy
import hashlib
import json
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any

from continuous_fixtures import GRAY, RGB, Fixture, gamma_profile, profile_from_output
from jsonschema import Draft202012Validator, FormatChecker
from test_cli import call_json, expect

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = json.loads((ROOT / "schemas/run-record.schema.json").read_text(encoding="utf-8"))
VALIDATOR = Draft202012Validator(SCHEMA, format_checker=FormatChecker())


def produced(exe: Path, root: Path, name: str, options: list[str]) -> Path:
    """Publish a real bundle from independently encoded input, then validate its record."""
    source = root / f"{name}.png"
    source.write_bytes(Fixture(8, 4, ((128,),) * 32).encoded())
    directory = root / name
    response = call_json(
        exe, ["process", str(source), "--out-dir", str(directory), *options, "--json"]
    )
    raw = (directory / "run.json").read_bytes()
    record = json.loads(raw)
    VALIDATOR.validate(record)
    expect(response["record"]["sha256"] == hashlib.sha256(raw).hexdigest(), "exact manifest digest")
    expect(
        record["source"]["sha256"] == hashlib.sha256(source.read_bytes()).hexdigest(),
        "consumed source",
    )
    call_json(exe, ["verify", str(directory), "--json"])
    return directory


def refused(exe: Path, directory: Path) -> None:
    """Require input refusal without changing any bundle bytes."""
    before = {
        str(p.relative_to(directory)): p.read_bytes() for p in directory.rglob("*") if p.is_file()
    }
    response = call_json(exe, ["verify", str(directory), "--json"], 3)
    expect(response["error"]["code"] == "E_INPUT", "bundle disagreement is an input error")
    after = {
        str(p.relative_to(directory)): p.read_bytes() for p in directory.rglob("*") if p.is_file()
    }
    expect(before == after, "verification never modifies artifacts")


def malformed_records(exe: Path, directory: Path) -> None:
    """Each mutation breaks one record contract while leaving artifact bytes unchanged."""
    path = directory / "run.json"
    original = path.read_bytes()
    good = json.loads(original)
    for key in good:
        altered = copy.deepcopy(good)
        del altered[key]
        expect(not VALIDATOR.is_valid(altered), f"schema requires {key}")
        path.write_text(json.dumps(altered), encoding="utf-8")
        refused(exe, directory)
    for route, value in (
        (("record", "version"), 4294967297),
        (("record", "run"), "wrong"),
        (("record", "recorded"), "2026-02-30T00:00:00Z"),
        (("request", "operation", "parameters", "threshold"), -1),
        (("request", "operation", "method", "method_version"), 2),
        (("output", "bit_depth"), 16),
        (("source", "bytes"), -1),
        (("build", "dependency_lock_sha256"), "wrong"),
        (("request", "protection_supplied"), True),
    ):
        altered = copy.deepcopy(good)
        parent: Any = altered
        for key in route[:-1]:
            parent = parent[key]
        parent[route[-1]] = value
        path.write_text(json.dumps(altered), encoding="utf-8")
        refused(exe, directory)
    for section in (None, *good):
        altered = copy.deepcopy(good)
        if section is None:
            altered["unknown"] = 1
        elif isinstance(altered[section], dict):
            altered[section]["unknown"] = 1
        else:
            continue
        expect(not VALIDATOR.is_valid(altered), "closed schema rejects unknown fields")
        path.write_text(json.dumps(altered), encoding="utf-8")
        refused(exe, directory)
    duplicate = original.replace(b'"version": 6', b'"version": 1, "version": 6', 1)
    path.write_bytes(duplicate)
    refused(exe, directory)
    path.write_bytes(original)


def artifact_claims(exe: Path, directory: Path) -> None:
    """Correct artifact hashes cannot excuse false dimensions or profile claims."""
    path = directory / "run.json"
    original = path.read_bytes()
    good = json.loads(original)
    for field, value in (
        ("width", 9),
        ("height", 5),
        ("bit_depth", 16),
        ("channels", 3),
        ("profile_embedded", not good["output"]["profile_embedded"]),
    ):
        altered = copy.deepcopy(good)
        altered["output"][field] = value
        if field in {"width", "height", "bit_depth", "channels"}:
            altered["execution"]["conversion"]["encoded_output"][field] = value
            altered["execution"]["conversion"]["decoded_input"][field] = value
            altered["execution"]["illumination"]["eligible_samples"] = (
                altered["output"]["width"] * altered["output"]["height"]
            )
        if field != "profile_embedded":
            VALIDATOR.validate(altered)
        else:
            expect(
                not VALIDATOR.is_valid(altered),
                "continuous output must declare its canonical profile",
            )
        path.write_text(json.dumps(altered), encoding="utf-8")
        refused(exe, directory)
    path.write_bytes(original)


def false_binary(exe: Path, directory: Path) -> None:
    """A gray PNG with nonbinary samples remains invalid even with its correct digest."""
    image = directory / "result.png"
    original_image = image.read_bytes()
    path = directory / "run.json"
    original_record = path.read_bytes()
    raw = Fixture(8, 4, ((128,),) * 32, color=GRAY).encoded()
    image.write_bytes(raw)
    record = json.loads(original_record)
    record["output"].update(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw))
    VALIDATOR.validate(record)
    path.write_text(json.dumps(record), encoding="utf-8")
    refused(exe, directory)
    image.write_bytes(original_image)
    path.write_bytes(original_record)


def false_profile(exe: Path, directory: Path) -> None:
    """A supported but noncanonical ICC profile cannot satisfy continuous output's contract."""
    image = directory / "result.png"
    original_image = image.read_bytes()
    path = directory / "run.json"
    original_record = path.read_bytes()
    changed = gamma_profile(profile_from_output(original_image), 2.2)
    raw = Fixture(
        8, 4, ((128,),) * 32, metadata=((b"iCCP", b"DocEnhance\0\0" + zlib.compress(changed)),)
    ).encoded()
    image.write_bytes(raw)
    record = json.loads(original_record)
    record["output"].update(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw))
    VALIDATOR.validate(record)
    path.write_text(json.dumps(record), encoding="utf-8")
    refused(exe, directory)
    image.write_bytes(original_image)
    path.write_bytes(original_record)


def false_mask(exe: Path, directory: Path) -> None:
    """A stored mask must use canonical samples and agree with the observed protection count."""
    path = directory / "run.json"
    original_record = path.read_bytes()
    mask = directory / "assets/protect-mask.png"
    original_mask = mask.read_bytes()
    for value in (0, 255):
        raw = Fixture(8, 4, ((value,),) * 32).encoded()
        mask.write_bytes(raw)
        record = json.loads(original_record)
        record["protection"]["stored"].update(
            sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw)
        )
        VALIDATOR.validate(record)
        path.write_text(json.dumps(record), encoding="utf-8")
        refused(exe, directory)
    mask.write_bytes(original_mask)
    path.write_bytes(original_record)


def main() -> None:
    """Exercise all supported alternatives, then break records independently of the writer."""
    Draft202012Validator.check_schema(SCHEMA)
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-bundles-") as temporary:
        root = Path(temporary)
        binary = produced(exe, root, "binary", ["--output-mode", "bw", "--binarize", "fixed"])
        produced(exe, root, "sauvola", ["--output-mode", "bw"])
        continuous = produced(exe, root, "continuous", [])
        produced(
            exe, root, "illumination", ["--illumination", "surface", "--background-strength", "0"]
        )
        produced(exe, root, "automatic", ["--illumination", "auto"])
        mask = root / "mask.png"
        mask.write_bytes(Fixture(8, 4, ((255,),) * 32).encoded())
        protected = produced(exe, root, "protected", ["--protect-mask", str(mask)])
        source = root / "color.png"
        source.write_bytes(Fixture(2, 1, ((10, 20, 30),) * 2, color=RGB, depth=16).encoded())
        color = root / "color"
        call_json(exe, ["process", str(source), "--out-dir", str(color), "--json"])
        VALIDATOR.validate(json.loads((color / "run.json").read_bytes()))
        call_json(exe, ["verify", str(color), "--json"])
        shaded = root / "shaded.png"
        width, height = 64, 64
        shaded.write_bytes(
            Fixture(
                width, height, tuple((100 + x,) for _y in range(height) for x in range(width))
            ).encoded()
        )
        illuminated = root / "applied"
        response = call_json(
            exe,
            [
                "process",
                str(shaded),
                "--out-dir",
                str(illuminated),
                "--illumination",
                "surface",
                "--background-target",
                "0.8",
                "--json",
            ],
        )
        expect(response["illumination"]["status"] == "applied", "actual I01 application")
        VALIDATOR.validate(json.loads((illuminated / "run.json").read_bytes()))
        call_json(exe, ["verify", str(illuminated), "--json"])
        malformed_records(exe, binary)
        artifact_claims(exe, continuous)
        false_profile(exe, continuous)
        false_binary(exe, binary)
        false_mask(exe, protected)
    print("PASS: complete bundle records, artifact properties and independent schema validation")


if __name__ == "__main__":
    main()
