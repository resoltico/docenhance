# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real I01 execution: independent model oracle, protection and preservation fixtures."""

from __future__ import annotations

import dataclasses
import json
import statistics
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any

from continuous_fixtures import GRAY_ALPHA, RGB, Fixture, decode_output, exif
from illumination_reference import quantile, surface
from test_cli import call_json, expect
from test_continuous import transfer_decode

WORD_MAX = 65535
OPAQUE_ALPHA = 255
FIRST_MARK_ROW = 3
DEPTH = 16
ORIENTATIONS = 8
QUALITY_WIDTH = 192
QUALITY_HEIGHT = 96
MODEL_TOLERANCE = 3e-5
BACKGROUND_REDUCTION = 0.40
CONTRAST_RETENTION = 0.75
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
    expect(source.read_bytes() == fixture.encoded(), "I01 never overwrites source")
    if code:
        expect(set(root.iterdir()) == before, "I01 refusal leaves no final or staging path")
        return None, result
    expect(result["conversion"]["verified"], "illumination output verified before commit")
    expect(result["illumination"]["complete"], "reported illumination is complete")
    return decode_output((output / "result.png").read_bytes()), result


def gray_fixture(width: int, height: int, values: list[float]) -> Fixture:
    """Quantized high-precision source with declared linear-gray samples."""
    return Fixture(
        width, height, tuple((round(v * WORD_MAX),) for v in values), depth=DEPTH, metadata=LINEAR
    )


def source_values(fixture: Fixture) -> list[float]:
    """Match the documented float color-engine ingress, independently of the production adapter."""
    return [
        struct.unpack("f", struct.pack("f", pixel[0] / WORD_MAX))[0] for pixel in fixture.pixels
    ]


def identity_and_mask_validation(exe: Path, root: Path) -> None:
    """Off, zero strength, unit gain and all-protected paths equal the no-filter output."""
    fixture = Fixture(16, 8, tuple((10000 + i,) for i in range(128)), depth=DEPTH)
    baseline, _ = run(exe, root, fixture, [])
    expect(baseline is not None, "baseline exists")
    mask = root / "mask.png"
    mask.write_bytes(Fixture(16, 8, ((1,),) * 128, depth=1).encoded())
    for options in (
        ["--illumination", "off"],
        ["--illumination", "surface", "--background-strength", "0"],
        ["--illumination", "surface", "--background-max-gain", "1"],
        ["--illumination", "surface", "--protect-mask", str(mask)],
    ):
        output, _ = run(exe, root, fixture, options)
        expect(output == baseline, "exact output samples under stage no-op")
    for depth in (2, 4, 16):
        mask.write_bytes(Fixture(16, 8, ((1,),) * 128, depth=depth).encoded())
        run(exe, root, fixture, ["--protect-mask", str(mask)], 3)
    for alpha in (255, 128):
        mask.write_bytes(Fixture(16, 8, ((1, alpha),) * 128, color=GRAY_ALPHA).encoded())
        run(exe, root, fixture, ["--protect-mask", str(mask)], 0 if alpha == OPAQUE_ALPHA else 3)
    mask.write_bytes(Fixture(8, 16, ((1,),) * 128).encoded())
    run(
        exe,
        root,
        fixture,
        ["--illumination", "surface", "--background-strength", "0", "--protect-mask", str(mask)],
        3,
    )


def admission_and_failures(exe: Path, root: Path) -> None:
    """Cross-operation fields, empty values and method failures remain honest failures."""
    fixture = gray_fixture(16, 8, [0.5] * 128)
    for options in (
        ["--illumination", "surface", "--background-cell", ""],
        ["--illumination", "surface", "--background-target", "0"],
        ["--illumination", "surface", "--background-quantile", "0.5"],
        ["--illumination", "surface", "--background-cell", "1e2"],
        ["--illumination", "off", "--background-strength", "0"],
        ["--illumination", "surface", "--output-mode", "bw"],
        ["--illumination", "unknown"],
    ):
        run(exe, root, fixture, options, 2)
    black = gray_fixture(16, 8, [0.0] * 128)
    _, failed = run(exe, root, black, ["--illumination", "surface"], 4)
    expect(failed["error"]["code"] == "E_METHOD_INAPPLICABLE", "explicit applicability failure")
    expect(failed["illumination"]["status"] == "failed", "failed stage diagnostics retained")
    _, skipped = run(exe, root, black, ["--illumination", "auto"])
    expect(skipped["illumination"]["status"] == "skipped", "automatic inapplicability skips")


def independent_model(exe: Path, root: Path) -> None:
    """Every output sample agrees with a dense direct solve, including clipped edge cells."""
    width, height, cell = 19, 17, 8
    values = [0.3 + 0.6 * x / (width - 1) for _y in range(height) for x in range(width)]
    fixture = gray_fixture(width, height, values)
    protected = [i % 11 == 0 for i in range(width * height)]
    mask = root / "sparse.png"
    mask.write_bytes(Fixture(width, height, tuple((int(p),) for p in protected), depth=1).encoded())
    output, response = run(
        exe,
        root,
        fixture,
        [
            "--illumination",
            "surface",
            "--background-cell",
            str(cell),
            "--background-strength",
            "1",
            "--background-max-gain",
            "3",
            "--protect-mask",
            str(mask),
        ],
    )
    output = require_output(output)
    linear = source_values(fixture)
    background = surface(linear, width, height, cell, protected)
    target = quantile([b for b, p in zip(background, protected, strict=True) if not p], 0.9)
    expected = [
        y if p else min(1, y * min(3, max(1, target / max(b, 0.02))))
        for y, b, p in zip(linear, background, protected, strict=True)
    ]
    actual = [transfer_decode(p[0] / WORD_MAX) for p in output.pixels]
    expect(
        max(abs(a - b) for a, b in zip(actual, expected, strict=True)) < MODEL_TOLERANCE,
        "all independently fitted samples",
    )
    report = response["illumination"]
    expect(report["protected_samples"] == sum(protected), "mask count")
    expect(
        report["application"]["evaluated_samples"] == len(values) - sum(protected),
        "verification does not double-count",
    )
    expect(report["solver"]["residual"] <= report["solver"]["tolerance"], "true solver residual")
    baseline, _ = run(exe, root, fixture, [])
    baseline = require_output(baseline)
    expect(
        all(a == b for a, b, p in zip(output.pixels, baseline.pixels, protected, strict=True) if p),
        "protected encoded samples equal baseline",
    )


def oriented_protection(exe: Path, root: Path) -> None:
    """A mask is in already-oriented coordinates; it must not be oriented a second time."""
    width, height = 16, 8
    source = gray_fixture(width, height, [0.3] * (width * height))
    mask = root / "oriented-mask.png"
    fixture = source
    mask_pixels = ((1,),) + ((0,),) * (width * height - 1)
    for orientation in range(1, ORIENTATIONS + 1):
        fixture = dataclasses.replace(source, metadata=(*LINEAR, (b"eXIf", exif(orientation))))
        baseline, _ = run(exe, root, fixture, [])
        baseline = require_output(baseline)
        mask.write_bytes(Fixture(baseline.width, baseline.height, mask_pixels).encoded())
        output, _ = run(
            exe,
            root,
            fixture,
            [
                "--illumination",
                "surface",
                "--background-target",
                "1",
                "--background-strength",
                "1",
                "--protect-mask",
                str(mask),
            ],
        )
        output = require_output(output)
        expect(output.pixels[0] == baseline.pixels[0], "oriented protected sample unchanged")
        expect(output.pixels[1][0] > baseline.pixels[1][0], "eligible oriented sample changed")
    mask.write_bytes(Fixture(height, width, mask_pixels, metadata=((b"eXIf", exif(6)),)).encoded())
    run(exe, root, fixture, ["--protect-mask", str(mask)], 3)


def colored_transport(exe: Path, root: Path) -> None:
    """Color transport lifts linear luminance, not three independently equalized channels."""
    color = (6000, 20000, 42000)
    fixture = Fixture(8, 8, (color,) * 64, depth=DEPTH, color=RGB, metadata=LINEAR)
    mask = root / "color-protection.png"
    mask.write_bytes(Fixture(8, 8, ((1,),) + ((0,),) * 63).encoded())
    baseline, _ = run(exe, root, fixture, [])
    baseline = require_output(baseline)
    rgb = [struct.unpack("f", struct.pack("f", c / WORD_MAX))[0] for c in color]
    y = sum(c * w for c, w in zip(rgb, (0.2126, 0.7152, 0.0722), strict=True))
    target = min(1.0, 2 * y)
    expected = [c + ((target - y) / (1 - y)) * (1 - c) for c in rgb]
    options = [
        "--illumination",
        "surface",
        "--background-target",
        "1",
        "--background-strength",
        "1",
        "--background-max-gain",
        "2",
        "--protect-mask",
        str(mask),
    ]
    output, response = run(exe, root, fixture, options)
    output = require_output(output)
    expect(output.pixels[0] == baseline.pixels[0], "protected color preserved")
    actual = [transfer_decode(c / WORD_MAX) for c in output.pixels[1]]
    expect(
        max(abs(a - b) for a, b in zip(actual, expected, strict=True)) < MODEL_TOLERANCE,
        "neutral-axis color transport",
    )
    expect(
        response["illumination"]["application"]["evaluated_samples"] == len(fixture.pixels) - 1,
        "color diagnostics count once",
    )
    gray, _ = run(exe, root, fixture, [*options, "--output-mode", "gray"])
    gray = require_output(gray)
    expect(
        abs(transfer_decode(gray.pixels[1][0] / WORD_MAX) - target) < MODEL_TOLERANCE,
        "enhancement precedes gray output conversion",
    )


def quality_cases(exe: Path, root: Path) -> dict[str, float]:
    """P01/P02 are synthetic preservation/conformance gates, not a real-document benchmark."""
    width, height = QUALITY_WIDTH, QUALITY_HEIGHT
    marks = [y % 12 in (3, 4) and x % 8 in (1, 2, 3) for y in range(height) for x in range(width)]
    uniform = gray_fixture(width, height, [0.8 * (0.15 if mark else 1) for mark in marks])
    baseline, _ = run(exe, root, uniform, [])
    output, response = run(exe, root, uniform, ["--illumination", "auto"])
    expect(
        response["illumination"]["status"] == "skipped" and output == baseline,
        "P01 already-good no-op",
    )
    background = [0.45 + 0.5 * x / (width - 1) for _y in range(height) for x in range(width)]
    values = [b * (0.15 if p else 1) for b, p in zip(background, marks, strict=True)]
    fixture = gray_fixture(width, height, values)
    output, response = run(
        exe,
        root,
        fixture,
        [
            "--illumination",
            "auto",
            "--background-strength",
            "1",
            "--background-max-gain",
            "3",
            "--background-cell",
            "16",
        ],
    )
    output = require_output(output)
    expect(response["illumination"]["status"] == "applied", "P02 auto applicability")
    actual = [transfer_decode(p[0] / WORD_MAX) for p in output.pixels]
    before = [v for v, p in zip(values, marks, strict=True) if not p]
    after = [v for v, p in zip(actual, marks, strict=True) if not p]
    cv_before = statistics.pstdev(before) / statistics.mean(before)
    cv_after = statistics.pstdev(after) / statistics.mean(after)
    reduction = 1 - cv_after / cv_before
    # Same-column paper on a neighboring row isolates local mark contrast from the shadow field.
    contrasts = [
        (actual[(i // width - 1) * width + i % width] - actual[i])
        / actual[(i // width - 1) * width + i % width]
        for i, p in enumerate(marks)
        if p and (i // width) % 12 == FIRST_MARK_ROW
    ]
    retention = min(contrasts) / (1 - 0.15)
    expect(reduction >= BACKGROUND_REDUCTION, "P02 background variation reduction")
    expect(retention >= CONTRAST_RETENTION, "P02 normalized mark contrast retained")
    return {
        "background_cv_before": cv_before,
        "background_cv_after": cv_after,
        "background_reduction": reduction,
        "minimum_contrast_retention": retention,
    }


def main() -> None:
    """Run against the executable; an isolated temporary directory owns every output."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-illumination-") as directory:
        root = Path(directory)
        identity_and_mask_validation(exe, root)
        admission_and_failures(exe, root)
        independent_model(exe, root)
        oriented_protection(exe, root)
        colored_transport(exe, root)
        quality = quality_cases(exe, root)
    print("PASS: six illumination groups; " + json.dumps(quality, sort_keys=True))


if __name__ == "__main__":
    main()
