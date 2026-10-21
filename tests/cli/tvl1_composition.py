# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent D02 prefix/color composition and terminal representation controls."""

from __future__ import annotations

import json
from typing import TYPE_CHECKING

from composition_reference import terminal_fixed
from continuous_fixtures import RGB, RGBA, Fixture, exif
from illumination_io import LINEAR, require_output, run
from illumination_reference import Plane, surface
from morphology_reference import background
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode
from tvl1_reference import TvSettings, solve

if TYPE_CHECKING:
    from pathlib import Path


WORD_MAX = 65535
COLOR_WORD_TOLERANCE = 4


def composition_and_record(exe: Path, root: Path) -> None:
    """Both illumination alternatives feed TV; final-sample comparison does not rerun the solve."""
    fixture = Fixture(
        16,
        8,
        tuple((18000 + x * 1500 + y * 30,) for y in range(8) for x in range(16)),
        depth=16,
        metadata=LINEAR,
    )
    linear = [p[0] / WORD_MAX for p in fixture.pixels]
    for selection in ("surface", "morph"):
        illum = [
            "--illumination",
            selection,
            "--background-target",
            "0.9",
            "--background-cell" if selection == "surface" else "--background-radius",
            "8",
        ]
        entering, _ = run(exe, root, fixture, illum)
        entering = require_output(entering)
        combined, report = run(
            exe,
            root,
            fixture,
            [*illum, "--denoise", "tvl1", "--tv-lambda", "0.05", "--denoise-blend", "1"],
        )
        combined = require_output(combined)
        # Compose independent floating illumination and TV references. Serializing an
        # intermediate first would introduce quantization before the sensitive L1 proximal step.
        field = (
            surface(Plane(linear, 16, 8, [False] * len(linear)), 8, 1)
            if selection == "surface"
            else background(linear, 16, 8, 8)
        )
        illuminated = [
            min(1, y * min(2, max(1, 0.9 / max(b, 0.02))))
            for y, b in zip(linear, field, strict=True)
        ]
        reference = solve(
            [transfer_encode(y) for y in illuminated], 16, 8, TvSettings(fidelity=0.05)
        )
        expect(
            max(
                abs(p[0] / WORD_MAX - u)
                for p, u in zip(combined.pixels, reference.values, strict=True)
            )
            < 4 / WORD_MAX,
            "TV sees illumination rather than stale input",
        )
        expect(
            combined.pixels != entering.pixels
            and report["record"]["execution"]["denoising"]["counts"]["changed"] > 0,
            "composition fixture must exercise an active TV correction",
        )
        expect(
            report["record"]["execution"]["illumination"]["counts"]["evaluated"]
            == len(fixture.pixels),
            "illumination counted once before TV",
        )
    record_and_constant(exe, root)


def color_alpha_orientation(exe: Path, root: Path) -> None:
    """Independent flattening, rotation and neutral-axis transport on a changed color field."""
    width, height = 9, 5
    pixels = tuple(
        (
            12000 + ((i * 37) % 101 - 50) * 17,
            24000 + ((i * 37) % 101 - 50) * 19,
            42000 + ((i * 37) % 101 - 50) * 23,
            (0, 32768, WORD_MAX)[i % 3],
        )
        for i in range(width * height)
    )
    fixture = Fixture(
        width,
        height,
        pixels,
        color=RGBA,
        depth=16,
        metadata=(*LINEAR, (b"eXIf", exif(6))),
    )
    # Orientation 6 rotates clockwise. Alpha is composited in the declared linear space.
    entering = [
        tuple((c / WORD_MAX) * (p[3] / WORD_MAX) + (1 - p[3] / WORD_MAX) for c in p[:3])
        for x in range(width)
        for y in reversed(range(height))
        for p in [pixels[y * width + x]]
    ]
    luminance = [
        sum(c * w for c, w in zip(p, (0.2126, 0.7152, 0.0722), strict=True)) for p in entering
    ]
    perceptual = [transfer_encode(y) for y in luminance]
    reference = solve(perceptual, height, width, TvSettings(fidelity=0.05))
    blend = 0.75
    output, response = run(
        exe,
        root,
        fixture,
        [
            "--denoise",
            "tvl1",
            "--tv-lambda",
            "0.05",
            "--denoise-blend",
            str(blend),
        ],
    )
    output = require_output(output)
    expected = []
    for rgb, y, f, u in zip(entering, luminance, perceptual, reference.values, strict=True):
        target = transfer_decode((1 - blend) * f + blend * u)
        corrected = (
            tuple(c * target / y for c in rgb)
            if target < y
            else tuple(c + (target - y) / (1 - y) * (1 - c) for c in rgb)
            if y < 1
            else rgb
        )
        expected.append(tuple(round(WORD_MAX * transfer_encode(c)) for c in corrected))
    expect(
        (output.width, output.height, output.color) == (height, width, RGB),
        "orientation precedes solver and alpha is flattened",
    )
    expect(
        max(
            abs(a - b)
            for actual, wanted in zip(output.pixels, expected, strict=True)
            for a, b in zip(actual, wanted, strict=True)
        )
        <= COLOR_WORD_TOLERANCE,
        "independent color luminance transport after alpha and orientation",
    )
    expect(
        response["record"]["execution"]["denoising"]["counts"]["changed"] > 0,
        "color fixture actually changes",
    )
    expect(
        response["record"]["execution"]["interpretation"]["alpha_composited_pixels"]
        == sum(p[3] < WORD_MAX for p in pixels),
        "alpha observations counted once through solver and verification",
    )


def record_and_constant(exe: Path, root: Path) -> None:
    """Actual healthy solution admits; unknown native fields and missing active solver refuse."""
    bundle = max(
        [p for p in root.iterdir() if p.is_dir()], key=lambda p: int(p.name.split("-")[-1])
    )
    call_json(exe, ["verify", str(bundle), "--json"])
    path = bundle / "run.json"
    original = path.read_bytes()
    record = json.loads(original)
    record["execution"]["denoising"]["native_calls"] = 1
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    record = json.loads(original)
    record["execution"]["denoising"]["solver"] = None
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    path.write_bytes(original)
    call_json(exe, ["verify", str(bundle), "--json"])
    color = Fixture(8, 4, ((12000, 22000, 42000),) * 32, depth=16, color=RGB, metadata=LINEAR)
    baseline, _ = run(exe, root, color, [])
    output, _ = run(exe, root, color, ["--denoise", "tvl1"])
    expect(output == baseline, "constant luminance keeps original color")


def binary_composition(exe: Path, root: Path) -> None:
    """TV-L1 floating output feeds terminal Q16 without an integer intermediate image."""
    width, height = 13, 9
    fixture = Fixture(
        width,
        height,
        tuple((12000 + (i * 1733) % 23000,) for i in range(width * height)),
        depth=16,
        metadata=LINEAR,
    )
    entering = [transfer_encode(p[0] / WORD_MAX) for p in fixture.pixels]
    solved = solve(entering, width, height, TvSettings(fidelity=0.05)).values
    expected = terminal_fixed(
        [transfer_decode((f + u) / 2) for f, u in zip(entering, solved, strict=True)]
    )
    output, response = run(
        exe,
        root,
        fixture,
        [
            "--denoise",
            "tvl1",
            "--tv-lambda",
            "0.05",
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
        "independent TV-L1 terminal logical decisions",
    )
    facts = response["record"]["execution"]["binarization"]
    expect(
        facts["black_samples"] == sum(p == (0,) for p in expected)
        and facts["white_samples"] == sum(p == (1,) for p in expected),
        "terminal counts",
    )
