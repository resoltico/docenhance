# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent R01 color/ordered-prefix and terminal-representation cases."""

from __future__ import annotations

from typing import TYPE_CHECKING

from composition_reference import final_codes, linear, luminance, terminal_fixed, transport
from continuous_fixtures import RGB, Fixture
from illumination_io import require_output, run
from restoration_reference import gaussian, motion, restore
from sharpening_reference import unsharp
from test_cli import expect
from test_continuous import transfer_decode, transfer_encode
from tvl1_reference import TvSettings, solve

if TYPE_CHECKING:
    from pathlib import Path


CLOCKWISE_QUARTER_TURN = 90
OPTIONS = ["--deblur", "wiener"]
WORD_MAX = 65535
SAMPLE_TOLERANCE = 3


def check_pixels(actual: Fixture, expected: list[float]) -> None:
    """Compare each encoded integer to a separately computed linear restoration."""
    maximum = (1 << actual.depth) - 1
    expect(
        max(
            abs(p[0] - round(transfer_encode(min(1, max(0, y))) * maximum))
            for p, y in zip(actual.pixels, expected, strict=True)
        )
        <= SAMPLE_TOLERANCE,
        "independent direct Fourier output",
    )


def protection_color_and_composition(exe: Path, root: Path) -> None:
    """Restore linear luminance, transport once, protect exact destinations, then contrast/S01."""
    width, height = 9, 7
    pixels = tuple((12000 + i * 13, 24000 + i * 17, 44000 - i * 11) for i in range(width * height))
    fixture = Fixture(width, height, pixels, depth=16, color=RGB)
    before = linear(fixture, 1, None)
    protected = [i % 3 == 0 for i in range(len(pixels))]
    mask = root / "protected.png"
    mask.write_bytes(Fixture(width, height, tuple((int(v),) for v in protected), depth=1).encoded())
    _, target = restore([luminance(p) for p in before], (width, height), gaussian(0.7), 0.03, 0.6)
    out, response = run(
        exe,
        root,
        fixture,
        [
            *OPTIONS,
            "--psf",
            "gaussian",
            "--psf-sigma",
            ".7",
            "--wiener-k",
            ".03",
            "--deblur-blend",
            ".6",
            "--protect-mask",
            str(mask),
        ],
    )
    actual = require_output(out)
    for i, (rgb, y, keep) in enumerate(zip(before, target, protected, strict=True)):
        expected = (
            pixels[i]
            if keep
            else tuple(
                round(transfer_encode(c) * WORD_MAX) for c in transport(rgb, min(1, max(0, y)))
            )
        )
        expect(
            max(abs(a - b) for a, b in zip(actual.pixels[i], expected, strict=True))
            <= (0 if keep else SAMPLE_TOLERANCE),
            "neutral-axis transport and exact protection",
        )
    expect(
        response["record"]["execution"]["restoration"]["counts"]["evaluated"]
        == protected.count(False),
        "protected pixels excluded from excursions",
    )
    contrast_sharpening_order(exe, root)


def denoising_order(exe: Path, root: Path) -> None:
    """Independent TV-L1 iterates enter R01 without an intermediate quantizer."""
    width, height = 9, 7
    fixture = Fixture(
        width, height, tuple((22000 + (i * 1733) % 19000,) for i in range(width * height)), depth=16
    )
    encoded = [p[0] / WORD_MAX for p in fixture.pixels]
    tv = solve(encoded, width, height, TvSettings(cap=30, tolerance=1e-8)).values
    entering = [transfer_decode(0.6 * f + 0.4 * u) for f, u in zip(encoded, tv, strict=True)]
    _, expected = restore(entering, (width, height), gaussian(0.7), 0.03, 0.6)
    out, response = run(
        exe,
        root,
        fixture,
        [
            "--denoise",
            "tvl1",
            "--tv-iterations",
            "30",
            "--tv-tolerance",
            "1e-8",
            "--denoise-blend",
            ".4",
            *OPTIONS,
            "--psf",
            "gaussian",
            "--psf-sigma",
            ".7",
            "--wiener-k",
            ".03",
            "--deblur-blend",
            ".6",
        ],
    )
    check_pixels(require_output(out), expected)
    expect(
        response["record"]["execution"]["denoising"]["counts"]["changed"] > 0,
        "denoiser actually changed preceding context",
    )
    expect(
        response["warnings"].count("W_RESTORATION_INFERENCE") == 1
        and response["warnings"].count("W_PSF_AFTER_TRANSFORM") == 1,
        "changed denoising warns about approximate subsequent PSF model",
    )
    expect(
        response["record"]["execution"]["denoising"]["counts"]["evaluated"] == len(encoded),
        "denoising observed once",
    )


def contrast_sharpening_order(exe: Path, root: Path) -> None:
    """Restore, then contrast and sharpen the same independent floating Y plane."""
    width, height = 9, 7
    gray = Fixture(width, height, tuple((16000 + i * 97,) for i in range(width * height)), depth=16)
    values = [transfer_decode(p[0] / WORD_MAX) for p in gray.pixels]
    _, restored = restore(values, (width, height), gaussian(0.7), 0.03, 0.6)
    contrasted = [transfer_encode(min(1, max(0, v))) ** 1.2 for v in restored]
    sharpened = unsharp(contrasted, (width, height), 0.8, 0.5, 1)
    out, response = run(
        exe,
        root,
        gray,
        [
            *OPTIONS,
            "--psf",
            "gaussian",
            "--psf-sigma",
            ".7",
            "--wiener-k",
            ".03",
            "--deblur-blend",
            ".6",
            "--contrast",
            "gamma",
            "--sharpen",
            "unsharp",
        ],
    )
    expected_samples = [round(min(1, max(0, v)) * WORD_MAX) for v in sharpened]
    expect(
        max(
            abs(p[0] - v) for p, v in zip(require_output(out).pixels, expected_samples, strict=True)
        )
        <= SAMPLE_TOLERANCE,
        "restoration before contrast and sharpening without quantization",
    )
    expect(
        response["record"]["execution"]["restoration"]["counts"]["evaluated"] == len(values),
        "verification does not recount",
    )


def binary_composition(exe: Path, root: Path) -> None:
    """Direct Fourier restoration precedes terminal Q16 without an intermediate image."""
    width, height = 9, 7
    fixture = Fixture(
        width, height, tuple((9000 + (i * 1733) % 38000,) for i in range(width * height)), depth=16
    )
    original = [transfer_decode(p[0] / WORD_MAX) for p in fixture.pixels]
    _, restored = restore(original, (width, height), gaussian(0.7), 0.03, 0.6)
    expected = terminal_fixed([min(1, max(0, y)) for y in restored])
    output, response = run(
        exe,
        root,
        fixture,
        [
            *OPTIONS,
            "--psf",
            "gaussian",
            "--psf-sigma",
            ".7",
            "--wiener-k",
            ".03",
            "--deblur-blend",
            ".6",
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
        "independent R01 terminal logical bits",
    )
    facts = response["record"]["execution"]["binarization"]
    expect(
        facts["black_samples"] == sum(p == (0,) for p in expected)
        and facts["white_samples"] == sum(p == (1,) for p in expected),
        "terminal counts",
    )


def rotated_motion(exe: Path, root: Path) -> None:
    """A motion PSF is already in post-G02 coordinates; rotating it again changes the answer."""
    width, height = 9, 7
    fixture = Fixture(
        width, height, tuple((9000 + (i * 1733) % 38000,) for i in range(width * height)), depth=16
    )
    # Independent clockwise90 gather, before any photometric operation.
    rotated = [
        fixture.pixels[(height - 1 - x) * width + y] for y in range(width) for x in range(height)
    ]
    values = [transfer_decode(p[0] / WORD_MAX) for p in rotated]
    original = [(y, y, y) for y in values]
    _, correct = restore(values, (height, width), motion(3.5, 37), 0.03, 0.7)
    _, wrong = restore(values, (height, width), motion(3.5, 127), 0.03, 0.7)
    expected = final_codes(original, [min(1, max(0, y)) for y in correct], 16, gray=True)
    wrong_codes = final_codes(original, [min(1, max(0, y)) for y in wrong], 16, gray=True)
    expect(
        max(abs(a[0] - b[0]) for a, b in zip(expected, wrong_codes, strict=True))
        > 2 * SAMPLE_TOLERANCE,
        "fixture distinguishes erroneous90-degree PSF reorientation",
    )
    output, response = run(
        exe,
        root,
        fixture,
        [
            "--rotate",
            "90",
            *OPTIONS,
            "--psf",
            "motion",
            "--psf-length",
            "3.5",
            "--psf-angle",
            "37",
            "--wiener-k",
            ".03",
            "--deblur-blend",
            ".7",
        ],
    )
    actual = require_output(output)
    expect((actual.width, actual.height) == (height, width), "true postrotation processing frame")
    expect(
        max(abs(a[0] - b[0]) for a, b in zip(actual.pixels, expected, strict=True))
        <= SAMPLE_TOLERANCE,
        "active G02/R01 motion matches every independent directDFT sample",
    )
    expect(
        response["record"]["execution"]["interpretation"]["rotation"] == CLOCKWISE_QUARTER_TURN
        and response["record"]["execution"]["restoration"]["counts"]["evaluated"] == len(values)
        and response["warnings"].count("W_PSF_AFTER_TRANSFORM") == 1,
        "actual geometry, completed restoration and derived transformed-model warning",
    )
