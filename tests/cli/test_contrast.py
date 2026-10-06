# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent C01/C02 executable maps, protection, precision and stage composition."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import RGB, RGBA, Fixture, exif
from illumination_reference import Plane, quantile, surface
from morphology_reference import background
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode
from test_illumination import LINEAR, require_output, run
from tvl1_reference import TvSettings, solve

WORD_MAX = 65535
CODE_TOLERANCE = 4
MINIMUM_LEVELS_RANGE = 1e-6
RANK_TOLERANCE = 1e-12
RECORD_VERSION = 7


def map_levels(values: list[float], low: float, high: float) -> tuple[list[float], float, float]:
    """Sort all eligible samples directly; no production quantile or histogram code."""
    lo, hi = quantile(values, low / 100), quantile(values, high / 100)
    mapped = (
        values
        if hi - lo < MINIMUM_LEVELS_RANGE
        else [min(1, max(0, (f - lo) / (hi - lo))) for f in values]
    )
    return mapped, lo, hi


def numerical(exe: Path, root: Path) -> None:
    """Check maps, clipping equality, all-sample ranks and sub-byte precision independently."""
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        pixels = tuple((round(maximum * (0.1 + (i % 101) / 125)),) for i in range(128))
        fixture = Fixture(16, 8, pixels, depth=depth)
        values = [p[0] / maximum for p in pixels]
        mapped, lo, hi = map_levels(values, 10, 90)
        output, response = run(
            exe,
            root,
            fixture,
            [
                "--contrast",
                "levels",
                "--levels-low",
                "10",
                "--levels-high",
                "90",
            ],
        )
        report = response["contrast"]
        expect(report["method"] == {"id": "C01", "method_version": 1}, "C01 identity")
        expect(report["measured_samples"] == len(values), "all eligible samples measured")
        expect(
            abs(report["levels"]["low"] - lo) < RANK_TOLERANCE
            and abs(report["levels"]["high"] - hi) < RANK_TOLERANCE,
            "independent nearest ranks",
        )
        expect(
            report["clipped_low_samples"] == sum(f < lo for f in values)
            and report["clipped_high_samples"] == sum(f > hi for f in values),
            "strict tail clipping excludes equality",
        )
        expect(
            report["clipped_low_fraction"] == sum(f < lo for f in values) / len(values),
            "fraction has the declared eligible denominator",
        )
        expect(
            max(
                abs(p[0] / maximum - f)
                for p, f in zip(require_output(output).pixels, mapped, strict=True)
            )
            <= 2 / maximum,
            "independent levels without eight-bit bottleneck",
        )
        output, response = run(exe, root, fixture, ["--contrast", "gamma", "--gamma", "2"])
        expect(response["contrast"]["method"] == {"id": "C02", "method_version": 1}, "C02 identity")
        expect(
            max(
                abs(p[0] / maximum - f * f)
                for p, f in zip(require_output(output).pixels, values, strict=True)
            )
            <= 2 / maximum,
            "gamma is f^G, not its reciprocal",
        )
    endpoints = Fixture(4, 1, ((0,), (16384,), (49151,), (WORD_MAX,)), depth=16)
    for exponent in (0.25, 4):
        output, _ = run(exe, root, endpoints, ["--contrast", "gamma", "--gamma", str(exponent)])
        output = require_output(output)
        expect(output.pixels[0] == (0,) and output.pixels[-1] == (WORD_MAX,), "exact endpoints")


def protection_and_noops(exe: Path, root: Path) -> None:
    """Protected extremes cannot alter levels quantiles; identities retain exact RGB."""
    values = [0, WORD_MAX, *range(30000, 30030)]
    fixture = Fixture(8, 4, tuple((v,) for v in values), depth=16)
    mask = root / "protect.png"
    mask.write_bytes(Fixture(8, 4, ((1,), (1,)) + ((0,),) * 30, depth=1).encoded())
    output, response = run(
        exe, root, fixture, ["--contrast", "levels", "--protect-mask", str(mask)]
    )
    output = require_output(output)
    expect(output.pixels[:2] == fixture.pixels[:2], "exact protected output")
    expected, lo, hi = map_levels([v / WORD_MAX for v in values[2:]], 0.5, 99.5)
    expect(
        abs(response["contrast"]["levels"]["low"] - lo) < RANK_TOLERANCE
        and abs(response["contrast"]["levels"]["high"] - hi) < RANK_TOLERANCE,
        "protected values excluded from fitting",
    )
    expect(
        max(abs(p[0] / WORD_MAX - f) for p, f in zip(output.pixels[2:], expected, strict=True))
        <= 2 / WORD_MAX,
        "low-contrast 16-bit distinctions reach the map",
    )
    color = Fixture(8, 4, ((12000, 22000, 42000),) * 32, color=RGB, depth=16)
    baseline, _ = run(exe, root, color, [])
    for options, reason in (
        (["--contrast", "gamma", "--gamma", "1"], "identity_gamma"),
        (["--contrast", "levels", "--contrast-blend", "0"], "zero_blend"),
        (["--contrast", "levels"], "insufficient_dynamic_range"),
    ):
        actual, report = run(exe, root, color, options)
        expect(
            actual == baseline and report["contrast"]["reason"] == reason,
            "validated algebraic/flat identity",
        )
        expect(report["contrast"]["evaluated_samples"] == 0, "no invented pixel execution")
    mask.write_bytes(Fixture(8, 4, ((1,),) * 32, depth=1).encoded())
    actual, report = run(exe, root, color, ["--contrast", "gamma", "--protect-mask", str(mask)])
    expect(
        actual == baseline and report["contrast"]["reason"] == "no_eligible_samples",
        "fully protected identity",
    )
    single = Fixture(8, 4, tuple((i * 2000,) for i in range(32)), depth=16)
    mask.write_bytes(Fixture(8, 4, ((1,),) * 15 + ((0,),) + ((1,),) * 16, depth=1).encoded())
    baseline, _ = run(exe, root, single, [])
    actual, report = run(exe, root, single, ["--contrast", "levels", "--protect-mask", str(mask)])
    expect(actual == baseline, "one eligible sample amid protected variation is exact identity")
    expect(
        report["contrast"]["measured_samples"] == 1
        and report["contrast"]["reason"] == "insufficient_dynamic_range",
        "single eligible quantiles are measured and flat",
    )
    mask.write_bytes(Fixture(4, 8, ((1,),) * 32, depth=1).encoded())
    run(exe, root, color, ["--contrast", "gamma", "--gamma", "1", "--protect-mask", str(mask)], 3)


def composition(exe: Path, root: Path) -> None:
    """Compose floating independent illumination, TV and gamma; require active stage changes."""
    width, height = 16, 8
    fixture = Fixture(
        width,
        height,
        tuple((18000 + x * 1500 + y * 30,) for y in range(height) for x in range(width)),
        depth=16,
        metadata=LINEAR,
    )
    values = [p[0] / WORD_MAX for p in fixture.pixels]
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
        expected = [f * f for f in denoised]
        options = [
            "--illumination",
            selector,
            "--background-target",
            "0.9",
            "--background-cell" if selector == "surface" else "--background-radius",
            "8",
            "--denoise",
            "tvl1",
            "--tv-lambda",
            "0.05",
            "--denoise-blend",
            "1",
            "--contrast",
            "gamma",
            "--gamma",
            "2",
        ]
        output, report = run(exe, root, fixture, options)
        expect(
            max(
                abs(p[0] / WORD_MAX - f)
                for p, f in zip(require_output(output).pixels, expected, strict=True)
            )
            < CODE_TOLERANCE / WORD_MAX,
            "independent full-precision stage composition",
        )
        expect(
            report["denoising"]["changed_samples"] > 0
            and report["contrast"]["changed_samples"] > 0,
            "composition cannot pass using an inactive denoiser/contrast",
        )
        expect(
            report["illumination"]["application"]["evaluated_samples"] == len(values)
            and report["denoising"]["evaluated_samples"] == len(values)
            and report["contrast"]["evaluated_samples"] == len(values),
            "prefix counted once",
        )


def nlm_levels_composition(exe: Path, root: Path) -> None:
    """Check D01 feeds C01 and retains its complete observations exactly once."""
    fixture = Fixture(16, 8, tuple((80 + (i % 16) * 5 + (i % 3) * 3,) for i in range(128)))
    denoise = ["--denoise", "nlm", "--nlm-patch", "3", "--nlm-search", "7"]
    prefix, prefix_report = run(exe, root, fixture, denoise)
    values = [p[0] / 255 for p in require_output(prefix).pixels]
    expected, _, _ = map_levels(values, 0.5, 99.5)
    output, report = run(exe, root, fixture, [*denoise, "--contrast", "levels"])
    expect(
        max(
            abs(p[0] / 255 - f)
            for p, f in zip(require_output(output).pixels, expected, strict=True)
        )
        < 4 / 255,
        "D01 to C01 agrees within the independent prefix quantization bound",
    )
    expect(report["denoising"] == prefix_report["denoising"], "D01 observations counted once")
    expect(
        report["denoising"]["changed_samples"] > 0 and report["contrast"]["changed_samples"] > 0,
        "both D01 and C01 are active",
    )


def color_alpha_and_records(exe: Path, root: Path) -> None:
    """Independent alpha/orientation/color transport; current records reject forged observations."""
    pixels = tuple((12000 + i * 100, 24000, 42000, (0, 32768, WORD_MAX)[i % 3]) for i in range(12))
    fixture = Fixture(4, 3, pixels, color=RGBA, depth=16, metadata=(*LINEAR, (b"eXIf", exif(6))))
    entering = [
        tuple(c / WORD_MAX * p[3] / WORD_MAX + 1 - p[3] / WORD_MAX for c in p[:3])
        for x in range(4)
        for y in reversed(range(3))
        for p in [pixels[y * 4 + x]]
    ]
    expected = []
    for rgb in entering:
        y = sum(c * w for c, w in zip(rgb, (0.2126, 0.7152, 0.0722), strict=True))
        f = transfer_encode(y)
        target = transfer_decode(0.5 * f + 0.5 * f * f)
        expected.append(tuple(round(WORD_MAX * transfer_encode(c * target / y)) for c in rgb))
    output, report = run(
        exe, root, fixture, ["--contrast", "gamma", "--gamma", "2", "--contrast-blend", "0.5"]
    )
    output = require_output(output)
    expect((output.width, output.height) == (3, 4), "oriented contrast coordinates")
    expect(
        max(
            abs(a - b)
            for pixel, wanted in zip(output.pixels, expected, strict=True)
            for a, b in zip(pixel, wanted, strict=True)
        )
        <= CODE_TOLERANCE,
        "shared color transport rather than channel-wise gamma",
    )
    expect(
        report["conversion"]["alpha_flattened_pixels"] == sum(p[3] < WORD_MAX for p in pixels),
        "alpha counted once",
    )
    bundle = max(
        (p for p in root.iterdir() if p.is_dir()), key=lambda p: int(p.name.split("-")[-1])
    )
    call_json(exe, ["verify", str(bundle), "--json"])
    path = bundle / "run.json"
    original = path.read_bytes()
    record = json.loads(original)
    expect(
        record["record"]["version"] == RECORD_VERSION
        and record["execution"]["contrast"] == report["contrast"],
        "current response and record agree",
    )
    record["execution"]["contrast"]["measured_samples"] = 1
    path.write_text(json.dumps(record), encoding="utf-8")
    call_json(exe, ["verify", str(bundle), "--json"], 3)
    path.write_bytes(original)


def admission(exe: Path, root: Path) -> None:
    """Presence, bounds, selectors and private alternatives fail before filesystem effects."""
    fixture = Fixture(1, 1, ((123,),))
    for options in (
        ["--gamma", "2"],
        ["--contrast", "off", "--contrast-blend", "0"],
        ["--contrast", "gamma", "--levels-low", "0"],
        ["--contrast", "levels", "--gamma", "1"],
        ["--contrast", "levels", "--levels-low", "11"],
        ["--contrast", "levels", "--levels-high", "89"],
        ["--contrast", "gamma", "--gamma", "0"],
        ["--contrast", "gamma", "--gamma", "nan"],
        ["--contrast", "gamma", "--gamma", "5"],
        ["--contrast", "gamma", "--contrast-blend", ""],
        ["--contrast", "clahe"],
        ["--output-mode", "bw", "--contrast", "off"],
    ):
        run(exe, root, fixture, options, 2)


def main() -> None:
    """Every real execution uses owned fixtures and output directories."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-contrast-") as directory:
        root = Path(directory)
        numerical(exe, root)
        protection_and_noops(exe, root)
        composition(exe, root)
        nlm_levels_composition(exe, root)
        color_alpha_and_records(exe, root)
        admission(exe, root)
    print("PASS: C01/C02 numerical, precision, protection, composition and record contracts")


if __name__ == "__main__":
    main()
