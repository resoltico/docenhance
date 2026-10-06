# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""I02 real-boundary numerical, strict-admission, protection and bundle checks."""

from __future__ import annotations

import json
import math
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import GRAY_ALPHA, Fixture
from jsonschema import Draft202012Validator
from morphology_reference import background
from test_cli import SCHEMA, call_json, expect
from test_continuous import transfer_decode
from test_denoising import composition
from test_illumination import LINEAR, colored_transport, oriented_protection, require_output, run

FIELD_TOLERANCE = 1e-12


def independent(exe: Path, root: Path) -> None:
    """Compare 8/16-bit outputs and field diagnostics to independently enumerated math."""
    width, height = 9, 5
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        pixels = tuple(
            (round(maximum * (0.2 + 0.6 * x / (width - 1))),)
            for _y in range(height)
            for x in range(width)
        )
        source = Fixture(width, height, pixels, depth=depth, metadata=LINEAR)
        values = [p[0] / maximum for p in pixels]
        for radius in (1, 3):
            field = background(values, width, height, radius)
            target = sorted(field)[math.ceil(0.9 * len(field)) - 1]
            output, report = run(
                exe, root, source, ["--illumination", "morph", "--background-radius", str(radius)]
            )
            output = require_output(output)
            light = report["illumination"]
            expect(light["method"] == {"id": "I02", "method_version": 1}, "I02 identity")
            validator = Draft202012Validator(json.loads(SCHEMA.read_text()))
            for schema_field, value in (
                ("morphology", None),
                ("auto_predicates", light["auto_predicates"] | {"coverage": False}),
            ):
                malformed = report | {"illumination": light | {schema_field: value}}
                expect(
                    not validator.is_valid(malformed),
                    "I02 rejects incomplete and foreign schema fields",
                )
            detail = light["morphology"]
            expect(abs(detail["target"] - target) < FIELD_TOLERANCE, "independent source target")
            expect(
                abs(detail["background_min"] - min(field)) < FIELD_TOLERANCE,
                "independent field minimum",
            )
            expect(
                abs(detail["background_max"] - max(field)) < FIELD_TOLERANCE,
                "independent field maximum",
            )
            actual = [transfer_decode(p[0] / maximum) for p in output.pixels]
            expected = [
                min(1, y * min(2, max(1, target / max(b, 0.02))))
                for y, b in zip(values, field, strict=True)
            ]
            expect(
                max(abs(a - b) for a, b in zip(actual, expected, strict=True)) < 3 / maximum,
                "independent I02 output",
            )
            # Locate the actual completed bundle through its result artifact.
            outputs = [p for p in root.iterdir() if p.is_dir() and (p / "result.png").is_file()]
            expect(bool(outputs), "completed bundle exists")
            bundle = max(outputs, key=lambda p: int(p.name.split("-")[-1]))
            verified = call_json(exe, ["verify", str(bundle), "--json"])
            expect(verified["exit_code"] == 0, "I02 bundle round-trip")
            record_path = bundle / "run.json"
            original = record_path.read_bytes()
            altered = json.loads(original)
            altered["execution"]["illumination"]["method"]["id"] = "I01"
            record_path.write_text(json.dumps(altered), encoding="utf-8")
            call_json(exe, ["verify", str(bundle), "--json"], 3)
            record_path.write_bytes(original)


def strict_and_noops(exe: Path, root: Path) -> None:
    """Presence is strict; no-op stages still validate masks and serialize complete facts."""
    fixture = Fixture(8, 4, ((100,),) * 32)
    invalid = (
        ["--background-radius", "1"],
        ["--illumination", "surface", "--background-radius", "1"],
        ["--illumination", "auto", "--background-radius", "auto"],
        ["--illumination", "morph", "--background-cell", "auto"],
        ["--illumination", "morph", "--background-quantile", "0.9"],
        ["--illumination", "morph", "--background-smooth", "2"],
        ["--illumination", "morph", "--background-radius", "0"],
        ["--illumination", "morph", "--background-radius", "257"],
        ["--illumination", "morph", "--background-radius", "1e2"],
        ["--illumination", "morph", "--background-radius", ""],
        ["--illumination", "morph", "--output-mode", "bw"],
    )
    for options in invalid:
        run(exe, root, fixture, options, 2)
    baseline, _ = run(exe, root, fixture, [])
    for option, value in (("--background-strength", "0"), ("--background-max-gain", "1")):
        output, report = run(exe, root, fixture, ["--illumination", "morph", option, value])
        expect(output == baseline, "I02 algebraic identity")
        expect(
            report["illumination"]["morphology"]["field_bytes"] == 0, "no full field for identity"
        )
    mask = root / "mask.png"
    mask.write_bytes(Fixture(8, 4, ((1,),) * 32, depth=1).encoded())
    before = mask.read_bytes()
    output, report = run(
        exe, root, fixture, ["--illumination", "morph", "--protect-mask", str(mask)]
    )
    expect(
        output == baseline and mask.read_bytes() == before,
        "all protected identity and immutable mask",
    )
    expect(report["illumination"]["reason"] == "no_eligible_samples", "all protected reason")
    mask.write_bytes(Fixture(4, 8, ((1,),) * 32, depth=1).encoded())
    run(
        exe,
        root,
        fixture,
        ["--illumination", "morph", "--background-strength", "0", "--protect-mask", str(mask)],
        3,
    )
    alpha = Fixture(8, 4, ((64, 128),) * 32, color=GRAY_ALPHA, metadata=LINEAR)
    output, report = run(exe, root, alpha, ["--illumination", "morph", "--background-target", "1"])
    output = require_output(output)
    expect(
        all(pixel == (255,) for pixel in output.pixels), "I02 analyzes composited alpha luminance"
    )
    expect(
        report["conversion"]["alpha_flattened_pixels"] == len(alpha.pixels),
        "alpha interpretation precedes I02",
    )
    black = Fixture(8, 4, ((0,),) * 32, metadata=LINEAR)
    output, report = run(exe, root, black, ["--illumination", "morph"])
    expect(
        output is not None and report["illumination"]["status"] == "no_change",
        "all black complete identity",
    )


def main() -> None:
    """Run all groups against a real production executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-morphology-") as directory:
        root = Path(directory)
        independent(exe, root)
        strict_and_noops(exe, root)
        colored_transport(exe, root, "morph")
        oriented_protection(exe, root, "morph")
        composition(exe, root, "morph")
    print("PASS: five I02 numerical, admission, color and orientation groups")


if __name__ == "__main__":
    main()
