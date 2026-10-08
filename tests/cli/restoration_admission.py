# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""R01 strict option presence, raw PSF input and read-only record verification."""

from __future__ import annotations

import json
import struct
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

from continuous_fixtures import RGB, RGBA, Fixture
from test_cli import call_json, expect
from test_illumination import require_output, run

OPTIONS = ["--deblur", "wiener"]


def admission_and_raw_kernel(exe: Path, root: Path) -> None:
    """Strict presence and PSF loading remain mandatory even at zero blend."""
    fixture = Fixture(3, 3, ((20000,),) * 9, depth=16)
    for options in (
        ["--psf", "gaussian"],
        [*OPTIONS],
        [*OPTIONS, "--psf", "gaussian", "--psf-angle", "0"],
        [*OPTIONS, "--psf", "motion", "--psf-sigma", "1"],
        [*OPTIONS, "--psf", "gaussian", "--psf-sigma", "nan"],
        [*OPTIONS, "--psf", "gaussian", "--wiener-k", "0"],
        [*OPTIONS, "--psf", "gaussian", "--deblur-blend", "1.1"],
        [*OPTIONS, "--psf", "gaussian", "--output-mode", "bw"],
    ):
        run(exe, root, fixture, options, 2)
    for depth, color, side in ((8, 0, 2), (4, 0, 3), (8, RGB, 3), (8, RGBA, 3), (16, 0, 3)):
        kernel = root / "invalid-kernel.png"
        channels = 1 if color == 0 else (3 if color == RGB else 4)
        kernel.write_bytes(
            Fixture(
                side, side, ((0,) * channels,) * (side * side), depth=depth, color=color
            ).encoded()
        )
        run(
            exe,
            root,
            fixture,
            [*OPTIONS, "--psf", "kernel", "--psf-file", str(kernel), "--deblur-blend", "0"],
            3,
        )
    run(
        exe,
        root,
        fixture,
        [
            *OPTIONS,
            "--psf",
            "kernel",
            "--psf-file",
            str(root / "missing.png"),
            "--deblur-blend",
            "0",
        ],
        3,
    )
    kernel = root / "raw-kernel.png"
    taps = ((0,),) * 4 + ((16384,), (32768,)) + ((0,),) * 3
    kernel.write_bytes(
        Fixture(3, 3, taps, depth=16, metadata=((b"gAMA", struct.pack(">I", 200000)),)).encoded()
    )
    out, response = run(
        exe,
        root,
        fixture,
        [*OPTIONS, "--psf", "kernel", "--psf-file", str(kernel), "--deblur-blend", "0"],
    )
    expect(require_output(out).pixels == fixture.pixels, "zero blend exact identity")
    expect(
        response["restoration"]["psf"]["coefficients"] == [0] * 4 + [1 / 3, 2 / 3] + [0] * 3,
        "raw 16-bit kernel bypasses profile interpretation",
    )
    expect(response["restoration"]["native_calls"] == 0, "zero blend does not transform")
    bundle = max(
        (p for p in root.iterdir() if p.name.startswith("out-")), key=lambda p: int(p.name[4:])
    )
    moved_kernel = kernel.rename(root / "kernel-moved.png")
    call_json(exe, ["verify", str(bundle), "--json"])
    moved_kernel.rename(kernel)
    path = bundle / "run.json"
    original = path.read_bytes()
    record = json.loads(original)
    record["execution"]["restoration"]["psf"]["coefficients"][4] = 0.9
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    path.write_bytes(original)
