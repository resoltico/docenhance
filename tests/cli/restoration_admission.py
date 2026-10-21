# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""R01 strict option presence, raw PSF input and read-only record verification."""

from __future__ import annotations

import hashlib
import json
import struct
from typing import TYPE_CHECKING

from continuous_fixtures import RGB, RGBA, Fixture
from illumination_io import require_output, run
from test_cli import call_json, expect

if TYPE_CHECKING:
    from pathlib import Path


PSF_MOMENT_TOLERANCE = 2e-14
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
    raw_kernel_record(exe, root, fixture)


def raw_kernel_record(exe: Path, root: Path, fixture: Fixture) -> None:
    """Raw coefficients have independent moments and retained bytes, even on an identity stage."""
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
        abs(response["record"]["execution"]["restoration"]["psf"]["normalized_sum"] - 1)
        < PSF_MOMENT_TOLERANCE
        and abs(response["record"]["execution"]["restoration"]["psf"]["centroid_x"] - 2 / 3)
        < PSF_MOMENT_TOLERANCE
        and response["record"]["execution"]["restoration"]["psf"]["centroid_y"] == 0,
        "independent raw16-bit normalized mass and moments bypass color interpretation",
    )
    facts = response["record"]["execution"]["restoration"]
    expect(
        facts["transform"] is None
        and facts["counts"]["evaluated"] == 0
        and facts["reason"] == "zero_blend",
        "zero blend has no fabricated transform",
    )
    bundle = max(
        (p for p in root.iterdir() if p.name.startswith("out-")), key=lambda p: int(p.name[4:])
    )
    asset = next(asset for asset in response["record"]["assets"] if asset["role"] == "psf_kernel")
    expect(
        (bundle / asset["path"]).read_bytes() == kernel.read_bytes()
        and asset["sha256"] == hashlib.sha256(kernel.read_bytes()).hexdigest(),
        "actual original encoded PSF asset and its independent identity retained",
    )
    moved_kernel = kernel.rename(root / "kernel-moved.png")
    call_json(exe, ["verify", str(bundle), "--json"])
    moved_kernel.rename(kernel)
    path = bundle / "run.json"
    original = path.read_bytes()
    record = json.loads(original)
    record["execution"]["restoration"]["psf"]["coefficients"] = [0] * 4 + [0.9] + [0] * 4
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    record = json.loads(original)
    record["execution"]["restoration"]["psf"]["centroid_x"] = 0
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    path.write_bytes(original)
    call_json(exe, ["verify", str(bundle), "--json"])
