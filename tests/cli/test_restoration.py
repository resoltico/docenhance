# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""R01 real-native Fourier conformance with independent phase and DC references."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from composition_reference import linear, luminance, transport
from continuous_fixtures import RGB, Fixture
from restoration_admission import admission_and_raw_kernel
from restoration_reference import gaussian, motion, optimal, reflect, restore
from sharpening_reference import unsharp
from test_cli import expect
from test_continuous import transfer_decode, transfer_encode
from test_illumination import require_output, run
from tvl1_reference import TvSettings, solve

OPTIONS = ["--deblur", "wiener"]
WORD_MAX = 65535
SAMPLE_TOLERANCE = 3
COEFFICIENT_TOLERANCE = 2e-14
BYTE_LEVELS = 256
DC_TOLERANCE = 2e-6
ANALYTIC_TOLERANCE = 1e-12
TRANSFORM_CALLS = 3
MIN_GUARD = 32
IMPULSE_INDEX = 49


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


def kernels_and_numerics(exe: Path, root: Path) -> None:
    """Asymmetric PSFs reject origin/conjugation errors; factories cover clockwise motion."""
    width, height = 9, 7
    pixels = tuple((9000 + (i * 1733) % 38000,) for i in range(width * height))
    fixture = Fixture(width, height, pixels, depth=16)
    values = [transfer_decode(p[0] / WORD_MAX) for p in pixels]
    taps = [0, 0, 0, 0, 4, 2, 0, 1, 0]
    kernel = root / "asymmetric.png"
    kernel.write_bytes(Fixture(3, 3, tuple((v,) for v in taps)).encoded())
    cases = [
        (["--psf", "gaussian", "--psf-sigma", ".7"], gaussian(0.7)),
        (["--psf", "motion", "--psf-length", "3.5", "--psf-angle", "37"], motion(3.5, 37)),
        (["--psf", "kernel", "--psf-file", str(kernel)], (3, 3, [v / 7 for v in taps])),
    ]
    for options, psf in cases:
        raw, expected = restore(values, (width, height), psf, 0.03, 0.7)
        out, response = run(
            exe, root, fixture, [*OPTIONS, *options, "--wiener-k", ".03", "--deblur-blend", ".7"]
        )
        check_pixels(require_output(out), expected)
        report = response["restoration"]
        expect(
            report["complete"] and report["native_calls"] == TRANSFORM_CALLS,
            "three completed native transforms",
        )
        expect(report["evaluated_samples"] == len(values), "observations counted once")
        expect(report["context_samples"] == len(values), "all input is Fourier context")
        actual_psf = report["psf"]
        expect((actual_psf["width"], actual_psf["height"]) == psf[:2], "factory dimensions")
        expect(
            max(abs(a - b) for a, b in zip(actual_psf["coefficients"], psf[2], strict=True))
            < COEFFICIENT_TOLERANCE,
            "independent normalized PSF coefficients",
        )
        expect("W_RESTORATION_INFERENCE" in report["warnings"], "inference warning mandatory")
        expect(report["guard"] == MIN_GUARD, "minimum reflected guard")
        expect(report["raw_low_samples"] == sum(v < 0 for v in raw), "raw low excursion count")
        expect(report["raw_high_samples"] == sum(v > 1 for v in raw), "raw high excursion count")
        expect(report["blended_low_samples"] == sum(v < 0 for v in expected), "blend before clamp")
        if options[1] == "kernel":
            expect("W_PSF_OFF_CENTER" in report["warnings"], "asymmetric centroid warning")
            expect(
                kernel.read_bytes() == Fixture(3, 3, tuple((v,) for v in taps)).encoded(),
                "PSF source preserved",
            )


def dc_and_delta(exe: Path, root: Path) -> None:
    """All K retain DC; centered delta has analytic attenuation without spatial shift."""
    for k in ("0.00001", "0.01", "1"):
        fixture = Fixture(1, 9, ((27341,),) * 9, depth=16)
        out, response = run(
            exe,
            root,
            fixture,
            [*OPTIONS, "--psf", "gaussian", "--wiener-k", k, "--deblur-blend", "1"],
        )
        actual = require_output(out)
        expect(
            max(
                abs(transfer_decode(p[0] / WORD_MAX) - transfer_decode(27341 / WORD_MAX))
                for p in actual.pixels
            )
            < DC_TOLERANCE,
            "constant DC preservation across K",
        )
        expect(
            response["restoration"]["warnings"] == ["W_RESTORATION_INFERENCE"],
            "identity still reports inference",
        )
    width, height = 11, 9
    values = [0.12 if i != IMPULSE_INDEX else 0.7 for i in range(width * height)]
    fixture = Fixture(
        width, height, tuple((round(transfer_encode(v) * WORD_MAX),) for v in values), depth=16
    )
    delta = root / "delta.png"
    delta.write_bytes(Fixture(3, 3, ((0,),) * 4 + ((WORD_MAX,),) + ((0,),) * 4, depth=16).encoded())
    psf = (3, 3, [0.0] * 4 + [1.0] + [0.0] * 4)
    _, expected = restore(
        [transfer_decode(p[0] / WORD_MAX) for p in fixture.pixels], (width, height), psf, 0.2, 1
    )
    values = [transfer_decode(p[0] / WORD_MAX) for p in fixture.pixels]
    fw, fh = optimal(width + 2 * MIN_GUARD), optimal(height + 2 * MIN_GUARD)
    mean = sum(
        values[reflect(y - MIN_GUARD, height) * width + reflect(x - MIN_GUARD, width)]
        for y in range(fh)
        for x in range(fw)
    ) / (fw * fh)
    expect(
        max(abs(v - (mean + (y - mean) / 1.2)) for y, v in zip(values, expected, strict=True))
        < ANALYTIC_TOLERANCE,
        "centered delta agrees with analytic scalar regularization",
    )
    out, _ = run(
        exe,
        root,
        fixture,
        [
            *OPTIONS,
            "--psf",
            "kernel",
            "--psf-file",
            str(delta),
            "--wiener-k",
            ".2",
            "--deblur-blend",
            "1",
        ],
    )
    check_pixels(require_output(out), expected)
    expect(
        max(range(len(values)), key=lambda i: require_output(out).pixels[i][0]) == IMPULSE_INDEX,
        "centered delta preserves impulse location",
    )


def precision_and_kernel_bounds(exe: Path, root: Path) -> None:
    """Sub-byte samples survive real FFT processing; kernel dimensions are enforced."""
    width, height = 20, 20
    fixture = Fixture(
        width, height, tuple((19000 + i * 37,) for i in range(width * height)), depth=16
    )
    delta = root / "precision-delta.png"
    delta.write_bytes(Fixture(3, 3, ((0,),) * 4 + ((65535,),) + ((0,),) * 4, depth=16).encoded())
    out, _ = run(
        exe,
        root,
        fixture,
        [
            *OPTIONS,
            "--psf",
            "kernel",
            "--psf-file",
            str(delta),
            "--wiener-k",
            ".01",
            "--deblur-blend",
            "1",
        ],
    )
    expect(
        len({p[0] for p in require_output(out).pixels}) > BYTE_LEVELS,
        "float transform retains more than 256 distinct 16-bit levels",
    )
    for side, code in ((129, 0), (131, 3)):
        kernel = root / "bounded-kernel.png"
        taps = ((0,),) * (side * side // 2) + ((255,),) + ((0,),) * (side * side // 2)
        kernel.write_bytes(Fixture(side, side, taps).encoded())
        result, response = run(
            exe,
            root,
            fixture,
            [*OPTIONS, "--psf", "kernel", "--psf-file", str(kernel), "--deblur-blend", "0"],
            code,
        )
        if not code:
            expect(require_output(result).pixels == fixture.pixels, "maximal PSF zero blend exact")
            expect(response["restoration"]["psf"]["width"] == side, "maximal odd PSF admitted")


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
        response["restoration"]["evaluated_samples"] == protected.count(False),
        "protected pixels excluded from excursions",
    )
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
        response["restoration"]["evaluated_samples"] == len(values), "verification does not recount"
    )


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
        response["denoising"]["changed_samples"] > 0, "denoiser actually changed preceding context"
    )
    expect(
        response["restoration"]["warnings"] == ["W_RESTORATION_INFERENCE", "W_PSF_AFTER_TRANSFORM"],
        "changed denoising warns about approximate subsequent PSF model",
    )
    expect(response["denoising"]["evaluated_samples"] == len(encoded), "denoising observed once")


def main() -> None:
    """Exercise the actual OpenCV backend, codec, admission, records and publication."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        kernels_and_numerics(exe, root)
        dc_and_delta(exe, root)
        precision_and_kernel_bounds(exe, root)
        protection_color_and_composition(exe, root)
        denoising_order(exe, root)
        admission_and_raw_kernel(exe, root)
    print("PASS: R01 independent phase/DC, kernels, transport, protection, ordering and records")


if __name__ == "__main__":
    main()
