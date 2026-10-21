# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""R01 real Fourier conformance with independent phase and DC references."""

from __future__ import annotations

import math
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import Fixture
from illumination_io import require_output, run
from restoration_admission import admission_and_raw_kernel
from restoration_cases import (
    binary_composition,
    check_pixels,
    denoising_order,
    protection_color_and_composition,
    rotated_motion,
)
from restoration_reference import gaussian, motion, optimal, reflect, restore
from test_cli import expect
from test_continuous import transfer_decode, transfer_encode

OPTIONS = ["--deblur", "wiener"]
WORD_MAX = 65535
COEFFICIENT_TOLERANCE = 2e-14
BYTE_LEVELS = 256
DC_TOLERANCE = 2e-6
ANALYTIC_TOLERANCE = 1e-12
MIN_GUARD = 32
IMPULSE_INDEX = 49


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
        report = response["record"]["execution"]["restoration"]
        expect(
            report["method"] == "R01" and report["transform"] is not None,
            "actual completed R01 has its transform facts",
        )
        expect(report["counts"]["evaluated"] == len(values), "observations counted once")
        expect(report["context_samples"] == len(values), "all input is Fourier context")
        actual_psf = report["psf"]
        expect((actual_psf["width"], actual_psf["height"]) == psf[:2], "factory dimensions")
        expect(
            abs(actual_psf["normalized_sum"] - math.fsum(psf[2])) < COEFFICIENT_TOLERANCE
            and abs(
                actual_psf["centroid_x"]
                - math.fsum(value * (i % psf[0] - psf[0] // 2) for i, value in enumerate(psf[2]))
            )
            < COEFFICIENT_TOLERANCE
            and abs(
                actual_psf["centroid_y"]
                - math.fsum(value * (i // psf[0] - psf[1] // 2) for i, value in enumerate(psf[2]))
            )
            < COEFFICIENT_TOLERANCE,
            "independent normalized PSF mass and both displacement moments",
        )
        expect("W_RESTORATION_INFERENCE" in response["warnings"], "inference warning mandatory")
        expect(report["transform"]["guard"] == MIN_GUARD, "minimum reflected guard")
        fw, fh = optimal(width + 2 * MIN_GUARD), optimal(height + 2 * MIN_GUARD)
        mean = math.fsum(
            values[reflect(y - MIN_GUARD, height) * width + reflect(x - MIN_GUARD, width)]
            for y in range(fh)
            for x in range(fw)
        ) / (fw * fh)
        expect(
            (report["transform"]["width"], report["transform"]["height"]) == (fw, fh)
            and abs(report["transform"]["padded_mean"] - mean) < ANALYTIC_TOLERANCE,
            "independent smooth padding and compensated mean agreement",
        )
        expect(report["raw_low_samples"] == sum(v < 0 for v in raw), "raw low excursion count")
        expect(report["raw_high_samples"] == sum(v > 1 for v in raw), "raw high excursion count")
        expect(report["blended_low_samples"] == sum(v < 0 for v in expected), "blend before clamp")
        if options[1] == "kernel":
            expect("W_PSF_OFF_CENTER" in response["warnings"], "asymmetric centroid warning")
            expect(
                kernel.read_bytes() == Fixture(3, 3, tuple((v,) for v in taps)).encoded(),
                "PSF source preserved",
            )


def constant_dc(exe: Path, root: Path) -> None:
    """Every K retains constant output and the independent padded mean bound."""
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
            abs(
                response["record"]["execution"]["restoration"]["transform"]["padded_mean"]
                - transfer_decode(27341 / WORD_MAX)
            )
            < ANALYTIC_TOLERANCE,
            "compensated padded constant mean retains the1e-12 contract",
        )
        expect(
            max(
                abs(transfer_decode(p[0] / WORD_MAX) - transfer_decode(27341 / WORD_MAX))
                for p in actual.pixels
            )
            < DC_TOLERANCE,
            "constant DC preservation across K",
        )
        expect(
            response["warnings"].count("W_RESTORATION_INFERENCE") == 1
            and "W_PSF_AFTER_TRANSFORM" not in response["warnings"]
            and "W_PSF_OFF_CENTER" not in response["warnings"],
            "identity still reports inference",
        )


def dc_and_delta(exe: Path, root: Path) -> None:
    """A centered delta has analytic attenuation without spatial shift."""
    constant_dc(exe, root)
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
        "binary64 transform retains more than 256 distinct 16-bit levels",
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
            expect(
                response["record"]["execution"]["restoration"]["psf"]["width"] == side,
                "maximal odd PSF admitted",
            )


def main() -> None:
    """Exercise the actual FFT backend, codec, admission, records and publication."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        kernels_and_numerics(exe, root)
        dc_and_delta(exe, root)
        precision_and_kernel_bounds(exe, root)
        protection_color_and_composition(exe, root)
        denoising_order(exe, root)
        rotated_motion(exe, root)
        binary_composition(exe, root)
        admission_and_raw_kernel(exe, root)
    print("PASS: R01 independent phase/DC, kernels, transport, protection, ordering and records")


if __name__ == "__main__":
    main()
