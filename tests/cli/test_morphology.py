# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""I02 real-boundary numerical, strict-admission, protection and bundle checks."""

from __future__ import annotations

import json
import math
import sys
import tempfile
from pathlib import Path

from composition_reference import terminal_fixed
from continuous_fixtures import GRAY_ALPHA, Fixture
from illumination_io import LINEAR, require_output, run
from jsonschema import Draft202012Validator
from morphology_reference import background
from test_cli import SCHEMA, call_json, expect
from test_continuous import transfer_decode
from test_denoising import composition
from test_illumination import colored_transport, oriented_protection

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
            light = report["record"]["execution"]["illumination"]
            expect(light["method"] == "I02", "I02 identity")
            validator = Draft202012Validator(json.loads(SCHEMA.read_text()))
            expect(validator.is_valid(report), "actual I02 response passes emitted schema")
            malformed = json.loads(json.dumps(report))
            malformed["record"]["execution"]["illumination"]["auto_predicates"] = {
                "coverage": False
            }
            expect(not validator.is_valid(malformed), "foreign I01 field is closed out")
            detail = light["analysis"]
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
            altered["execution"]["illumination"]["method"] = "I01"
            record_path.write_text(json.dumps(altered), encoding="utf-8")
            call_json(exe, ["verify", str(bundle), "--json"], 3)
            altered = json.loads(original)
            altered["execution"]["illumination"]["analysis"] = None
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
    )
    for options in invalid:
        run(exe, root, fixture, options, 2)
    baseline, _ = run(exe, root, fixture, [])
    for option, value in (("--background-strength", "0"), ("--background-max-gain", "1")):
        output, report = run(exe, root, fixture, ["--illumination", "morph", option, value])
        expect(output == baseline, "I02 algebraic identity")
        expect(
            report["record"]["execution"]["illumination"]["analysis"] is None
            and report["record"]["execution"]["illumination"]["counts"]["evaluated"] == 0,
            "algebraic identity has no fabricated analysis or pixel evaluation",
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
    expect(
        report["record"]["execution"]["illumination"]["reason"] == "no_eligible_samples",
        "all protected reason",
    )
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
        report["record"]["execution"]["interpretation"]["alpha_composited_pixels"]
        == len(alpha.pixels),
        "alpha interpretation precedes I02",
    )
    black = Fixture(8, 4, ((0,),) * 32, metadata=LINEAR)
    output, report = run(exe, root, black, ["--illumination", "morph"])
    expect(
        output is not None
        and report["record"]["execution"]["illumination"]["disposition"] == "no_change",
        "all black complete identity",
    )


def failure_prefix(exe: Path, root: Path) -> None:
    """Eager completed I02 survives a later stage failure; acquisition precedes all stages."""
    fixture = Fixture(9, 5, ((128,),) * 45, metadata=LINEAR)
    illumination = ["--illumination", "morph", "--background-radius", "1"]
    _, healthy = run(exe, root, fixture, illumination)
    completed = healthy["record"]["execution"]["illumination"]
    expect(
        completed["analysis"] is not None
        and completed["counts"]["evaluated"] == len(fixture.pixels),
        "actual healthy I02 completed before the matched refusal",
    )
    _, refused = run(exe, root, fixture, [*illumination, "--denoise", "nlm"], 4)
    expect(refused["error"]["code"] == "E_METHOD_INAPPLICABLE", "actual search refusal survives")
    expect(
        refused["completed"] == {"illumination": completed}
        and refused["failed_stage"] == "denoising"
        and refused["publication"] == "not_started",
        "completed prefix retained without staging",
    )
    psf = root / "malformed-psf.png"
    for zero_blend in (False, True):
        denoising = ["--denoise", "nlm", "--denoise-blend", "0"] if zero_blend else []
        restoration = ["--deblur", "wiener", "--psf", "kernel", "--psf-file"]
        _, absent = run(
            exe,
            root,
            fixture,
            [*illumination, *denoising, *restoration, str(root / "absent-psf.png")],
            3,
        )
        expect(
            absent["error"]["code"] == "E_INPUT"
            and absent["failed_stage"] is None
            and absent["completed"] == {}
            and absent["publication"] == "not_started",
            "failed byte acquisition reports no invented numerical work",
        )
        psf.write_bytes(b"invalid")
        _, malformed = run(
            exe, root, fixture, [*illumination, *denoising, *restoration, str(psf)], 3
        )
        expect(
            malformed["error"]["code"] == "E_INPUT"
            and malformed["failed_stage"] == "restoration"
            and malformed["publication"] == "not_started",
            "actual PSF decode owner failed",
        )
        expected = {"illumination": completed}
        if zero_blend:
            _, identity = run(exe, root, fixture, [*illumination, *denoising])
            expected["denoising"] = identity["record"]["execution"]["denoising"]
            expect(expected["denoising"]["reason"] == "zero_blend", "actual identity completed")
        expect(malformed["completed"] == expected, "all and only actual completed stages survive")


def binary_composition(exe: Path, root: Path) -> None:
    """Independent closing/blur/gain feeds terminal Q16 and physical PNG1."""
    width, height = 9, 5
    values = [0.04 + 0.45 * x / (width - 1) for _ in range(height) for x in range(width)]
    fixture = Fixture(
        width, height, tuple((round(y * 65535),) for y in values), depth=16, metadata=LINEAR
    )
    values = [p[0] / 65535 for p in fixture.pixels]
    field = background(values, width, height, 1)
    expected = terminal_fixed(
        [min(1, y * min(2, max(1, 0.7 / max(b, 0.02)))) for y, b in zip(values, field, strict=True)]
    )
    output, response = run(
        exe,
        root,
        fixture,
        [
            "--illumination",
            "morph",
            "--background-radius",
            "1",
            "--background-target",
            "0.7",
            "--output-mode",
            "bw",
            "--binarize",
            "fixed",
            "--fixed-threshold",
            "0.5",
        ],
    )
    expect(
        require_output(output).depth == 1 and require_output(output).pixels == expected,
        "I02 BW output matches independent terminal logical bits",
    )
    facts = response["record"]["execution"]["binarization"]
    expect(
        facts["black_samples"] == sum(p == (0,) for p in expected)
        and facts["white_samples"] == sum(p == (1,) for p in expected),
        "terminal counts",
    )


def main() -> None:
    """Run all groups against a real production executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-morphology-") as directory:
        root = Path(directory)
        independent(exe, root)
        strict_and_noops(exe, root)
        failure_prefix(exe, root)
        binary_composition(exe, root)
        colored_transport(exe, root, "morph")
        oriented_protection(exe, root, "morph")
        composition(exe, root, "morph")
    print("PASS: I02 numerical, admission, color, orientation and failure controls")


if __name__ == "__main__":
    main()
