# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""C03 real-boundary references: uneven grids, protection, color, precision and records."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

from clahe_reference import clahe
from continuous_fixtures import RGB, Fixture
from illumination_reference import Plane, surface
from morphology_reference import background
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode
from test_illumination import LINEAR, require_output, run
from tvl1_reference import TvSettings, solve

WORD_DEPTH = 16
BYTE_LEVELS = 256
TILE_HALF = 16
SPARSE_SUM = 4
IDENTITY_COUNT = 2
FLAT_COUNT = 4
CODE_TOLERANCE = 2
COLOR_TOLERANCE = 3
GRID = ["--contrast", "clahe", "--clahe-grid", "2x2"]


def numerical(exe: Path, root: Path) -> None:
    """Compare every sample around boundaries and centers against a distinct scalar oracle."""
    width, height = 67, 51
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        pixels = tuple(
            (round(maximum * (0.1 + ((x * 13 + y * 29) % 211) / 300)),)
            for y in range(height)
            for x in range(width)
        )
        fixture = Fixture(width, height, pixels, depth=depth)
        values = [p[0] / maximum for p in pixels]
        for clip in (1, 2, 8):
            expected, identities = clahe(values, (width, height), (2, 2), clip)
            output, response = run(exe, root, fixture, [*GRID, "--clahe-clip", str(clip)])
            actual = require_output(output)
            report = response["contrast"]
            expect(report["method"] == {"id": "C03", "method_version": 1}, "C03 reported")
            expect(report["identity_tiles"] == identities, "independent identity tiles")
            expect(
                report["measured_samples"] == width * height, "each eligible sample measured once"
            )
            expect(
                max(
                    abs(p[0] - round(f * maximum))
                    for p, f in zip(actual.pixels, expected, strict=True)
                )
                <= CODE_TOLERANCE,
                "mass redistribution and seam-free spatial/intensity interpolation",
            )
            expect(report["changed_samples"] > 0, "active method changes samples")
            if depth == WORD_DEPTH:
                expect(
                    len({p[0] for p in actual.pixels}) > BYTE_LEVELS,
                    "16-bit output retains sub-byte levels",
                )


def protection(exe: Path, root: Path) -> None:
    """Sparse/flat identity tiles and protected observations/destinations have exact semantics."""
    width = height = 32
    values = [0.2 + ((x * 7 + y * 3) % 31) / 50 for y in range(height) for x in range(width)]
    mask_values = [
        x < TILE_HALF and (y >= TILE_HALF or x + y > SPARSE_SUM)
        for y in range(height)
        for x in range(width)
    ]
    fixture = Fixture(width, height, tuple((round(f * 65535),) for f in values), depth=16)
    mask = root / "protection.png"
    mask.write_bytes(
        Fixture(width, height, tuple((int(p),) for p in mask_values), depth=1).encoded()
    )
    expected, identities = clahe(
        [p[0] / 65535 for p in fixture.pixels], (width, height), (2, 2), 2, mask_values
    )
    output, response = run(exe, root, fixture, [*GRID, "--protect-mask", str(mask)])
    actual = require_output(output)
    expect(
        identities == IDENTITY_COUNT and response["contrast"]["identity_tiles"] == IDENTITY_COUNT,
        "empty and sparse identity tiles",
    )
    expect(
        response["contrast"]["measured_samples"] == mask_values.count(False), "protected exclusion"
    )
    for i, p in enumerate(actual.pixels):
        expect(
            abs(p[0] - round(expected[i] * 65535)) <= CODE_TOLERANCE,
            "protected independent contextual oracle",
        )
        if mask_values[i]:
            expect(p == fixture.pixels[i], "protected destinations exact")
    flat = Fixture(width, height, ((23456,),) * (width * height), depth=16)
    output, response = run(exe, root, flat, GRID)
    expect(require_output(output).pixels == flat.pixels, "all identity tiles preserve exact RGB")
    expect(response["contrast"]["identity_tiles"] == FLAT_COUNT, "flat tile count")
    output, response = run(exe, root, Fixture(1, 1, ((123,),)), [*GRID, "--contrast-blend", "0"])
    expect(require_output(output).pixels == ((123,),), "zero blend bypasses pixel applicability")
    expect(response["contrast"]["measured_samples"] == 0, "zero blend does no histogram work")


def color_and_records(exe: Path, root: Path) -> None:
    """Independent neutral-axis transport, single blend and closed record rejection."""
    width = height = 32
    pixels = tuple((10000 + i % 20000, 30000 + i * 7 % 20000, 50000) for i in range(width * height))
    fixture = Fixture(width, height, pixels, depth=16, color=RGB)
    linear = [tuple(transfer_decode(c / 65535) for c in p) for p in pixels]
    ys = [sum(c * w for c, w in zip(p, (0.2126, 0.7152, 0.0722), strict=True)) for p in linear]
    fs = [transfer_encode(y) for y in ys]
    candidates, _ = clahe(fs, (width, height), (2, 2), 2)
    output, _ = run(exe, root, fixture, [*GRID, "--contrast-blend", "0.5"])
    actual = require_output(output)
    for p, rgb, y, f, candidate in zip(actual.pixels, linear, ys, fs, candidates, strict=True):
        target = transfer_decode((f + candidate) / 2)
        transported = tuple(
            c * target / y if target <= y else c + (1 - c) * (target - y) / (1 - y) for c in rgb
        )
        expected = tuple(round(transfer_encode(c) * 65535) for c in transported)
        expect(
            max(abs(a - b) for a, b in zip(p, expected, strict=True)) <= COLOR_TOLERANCE,
            "shared color transport",
        )
    bundle = max(
        (p for p in root.iterdir() if p.name.startswith("out-")), key=lambda p: int(p.name[4:])
    )
    call_json(exe, ["verify", str(bundle), "--json"])
    path = bundle / "run.json"
    original = path.read_bytes()
    for identity in (5, -1):
        record = json.loads(original)
        record["execution"]["contrast"]["identity_tiles"] = identity
        path.write_text(json.dumps(record), encoding="utf-8")
        call_json(exe, ["verify", str(bundle), "--json"], 3)
    record = json.loads(original)
    record["request"]["contrast"]["parameters"]["grid_rows"] = 3
    record["execution"]["contrast"]["parameters"]["grid_rows"] = 3
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    path.write_bytes(original)


def composition(exe: Path, root: Path) -> None:
    """Compare a full floating prefix, then ensure every upstream observation is counted once."""
    width = height = 32
    fixture = Fixture(
        width,
        height,
        tuple(
            (18000 + x * 900 + y * 30 + (x % 3) * 100,) for y in range(height) for x in range(width)
        ),
        depth=16,
        metadata=LINEAR,
    )
    values = [p[0] / 65535 for p in fixture.pixels]
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
        expected, _ = clahe(denoised, (width, height), (2, 2), 2)
        illumination = [
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
            output, report = run(exe, root, fixture, [*illumination, *denoise, *GRID])
            if denoiser == "tvl1":
                expect(
                    max(
                        abs(p[0] / 65535 - f)
                        for p, f in zip(require_output(output).pixels, expected, strict=True)
                    )
                    < COLOR_TOLERANCE / 65535,
                    "independent full floating CLAHE composition",
                )
            expect(
                report["denoising"]["changed_samples"] > 0
                and report["contrast"]["changed_samples"] > 0,
                "active composition",
            )
            expect(
                report["illumination"]["application"]["evaluated_samples"] == len(values)
                and report["denoising"]["evaluated_samples"] == len(values)
                and report["contrast"]["measured_samples"] == len(values)
                and report["contrast"]["evaluated_samples"] == len(values),
                "prefix observed once",
            )


def admission(exe: Path, root: Path) -> None:
    """Bounds and explicit private-option presence are rejected without publication."""
    fixture = Fixture(32, 32, ((123,),) * 1024)
    for options in (
        ["--clahe-grid", "2x2"],
        ["--contrast", "gamma", "--clahe-clip", "2"],
        [*GRID, "--gamma", "1"],
        [*GRID, "--levels-low", "0"],
        [*GRID, "--clahe-clip", "nan"],
        [*GRID, "--clahe-clip", "0.9"],
        [*GRID, "--clahe-clip", "8.1"],
        ["--output-mode", "bw", *GRID],
    ):
        run(exe, root, fixture, options, 2)
    for grid in ("1x2", "33x2", "2X2", "2x2x2", "2.0x2", "2x", "-2x2", "3x2"):
        run(exe, root, fixture, ["--contrast", "clahe", "--clahe-grid", grid], 2)


def main() -> None:
    """Run the full independent executable suite."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-clahe-") as directory:
        root = Path(directory)
        numerical(exe, root)
        protection(exe, root)
        color_and_records(exe, root)
        composition(exe, root)
        admission(exe, root)
    print("PASS: C03 independent mappings, precision, protection, color and records")


if __name__ == "__main__":
    main()
