# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""S01 follows the complete illumination/denoise/contrast prefix exactly once."""

from __future__ import annotations

from typing import TYPE_CHECKING

from clahe_reference import clahe
from composition_reference import Nlm
from composition_reference import denoise as nlm_reference
from continuous_fixtures import Fixture
from illumination_io import LINEAR, require_output, run
from illumination_reference import Plane, surface
from morphology_reference import background
from sharpening_reference import unsharp
from test_cli import expect
from test_continuous import transfer_encode
from tvl1_reference import TvSettings, solve

if TYPE_CHECKING:
    from pathlib import Path

MAXIMUM = 65535
TOLERANCE = 4


def prefix_composition(exe: Path, root: Path) -> None:
    """Both TV-L1 and binary64 NLM retain a completely independent floating prefix oracle."""
    width = height = 32
    pixels = tuple(
        (18000 + x * 900 + y * 30 + (x % 3) * 100,) for y in range(height) for x in range(width)
    )
    fixture = Fixture(width, height, pixels, depth=16, metadata=LINEAR)
    values = [p[0] / MAXIMUM for p in pixels]
    contrast = ["--contrast", "clahe", "--clahe-grid", "2x2"]
    for selector in ("surface", "morph"):
        field = (
            surface(Plane(values, width, height, [False] * len(values)), 8, 1)
            if selector == "surface"
            else background(values, width, height, 8)
        )
        illuminated = [
            min(1, y * min(2, max(1, 0.9 / max(b, 0.02))))
            for y, b in zip(values, field, strict=True)
        ]
        denoised = solve(
            [transfer_encode(y) for y in illuminated], width, height, TvSettings(fidelity=0.05)
        ).values
        after_contrast, _ = clahe(denoised, (width, height), (2, 2), 2)
        options = [
            "--illumination",
            selector,
            "--background-target",
            "0.9",
            "--background-cell" if selector == "surface" else "--background-radius",
            "8",
        ]
        for denoiser in ("tvl1", "nlm"):
            denoise = (
                ["--denoise", "tvl1", "--tv-lambda", "0.05", "--denoise-blend", "1"]
                if denoiser == "tvl1"
                else ["--denoise", "nlm", "--nlm-patch", "3", "--nlm-search", "7"]
            )
            if denoiser == "tvl1":
                entering = after_contrast
            else:
                filtered, _, _ = nlm_reference(
                    illuminated,
                    width,
                    height,
                    [False] * len(values),
                    Nlm(h=3, patch=3, search=7, blend=0.5),
                )
                entering, _ = clahe(
                    [transfer_encode(y) for y in filtered], (width, height), (2, 2), 2
                )
            raw = unsharp(entering, (width, height), 0.8, 0.5, 1)
            output, report = run(
                exe, root, fixture, [*options, *denoise, *contrast, "--sharpen", "unsharp"]
            )
            expect(
                max(
                    abs(p[0] - round(min(1, max(0, f)) * MAXIMUM))
                    for p, f in zip(require_output(output).pixels, raw, strict=True)
                )
                <= TOLERANCE,
                "sharpening follows independently floating full prefix "
                "without intermediate quantization",
            )
            expect(
                report["record"]["execution"]["illumination"]["counts"]["evaluated"] == len(values)
                and all(
                    report["record"]["execution"][name]["counts"]["evaluated"] == len(values)
                    for name in ("denoising", "contrast", "sharpening")
                ),
                "each stage observed exactly once",
            )
            expect(
                report["record"]["execution"]["sharpening"]["context_samples"] == len(values),
                "blur prepared exactly once",
            )
