# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""S01 real-executable precision, borders, protection, excursions and composition."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

from clahe_reference import clahe
from continuous_fixtures import RGB, Fixture
from sharpening_composition import prefix_composition
from sharpening_reference import unsharp
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode
from test_contrast import map_levels
from test_illumination import require_output, run

OPTIONS = ["--sharpen", "unsharp"]
TOLERANCE = 3
WORD_MAX = 65535
RANGE_TOLERANCE = 1e-10
WORD_DEPTH = 16
PRECISION_WIDTH = 67
BYTE_LEVELS = 256


def numerical(exe: Path, root: Path) -> None:
    """Impulse, singleton and full precision cases are compared at every sample."""
    for width, height in ((1, 1), (1, 9), (9, 1), (9, 7), (67, 19)):
        for depth in (8, 16):
            maximum = (1 << depth) - 1
            pixels = tuple(
                (round(maximum * (0.15 + ((i * 37) % (width * height)) / (2 * width * height))),)
                for i in range(width * height)
            )
            fixture = Fixture(width, height, pixels, depth=depth)
            values = [p[0] / maximum for p in pixels]
            for sigma in (0.3, 0.8, 3):
                raw = unsharp(values, (width, height), sigma, 0.5, 1)
                output, response = run(
                    exe, root, fixture, [*OPTIONS, "--sharpen-sigma", str(sigma)]
                )
                actual = require_output(output)
                expect(
                    max(
                        abs(p[0] - round(min(1, max(0, f)) * maximum))
                        for p, f in zip(actual.pixels, raw, strict=True)
                    )
                    <= TOLERANCE,
                    "independent 2D Gaussian with REFLECT_101",
                )
                report = response["sharpening"]
                expect(
                    report["context_samples"] == width * height, "all entering samples are context"
                )
                expect(
                    report["evaluated_samples"] == width * height,
                    "one evaluation per eligible sample",
                )
                expect(
                    abs(report["pre_clamp"]["low"] - min(raw)) < RANGE_TOLERANCE,
                    "pre-clamp minimum",
                )
                expect(
                    abs(report["pre_clamp"]["high"] - max(raw)) < RANGE_TOLERANCE,
                    "pre-clamp maximum",
                )
                expect(report["warnings"] == ["W_SHARPENING"], "explicit acutance warning")
                if depth == WORD_DEPTH and width == PRECISION_WIDTH:
                    expect(
                        len({p[0] for p in actual.pixels}) > BYTE_LEVELS,
                        "sub-byte output precision",
                    )
    pixels = ((0,),) * 31 + ((WORD_MAX,),) + ((0,),) * 31
    fixture = Fixture(9, 7, pixels, depth=16)
    raw = unsharp([p[0] / WORD_MAX for p in pixels], (9, 7), 0.8, 2, 0)
    _, response = run(
        exe, root, fixture, [*OPTIONS, "--sharpen-amount", "2", "--sharpen-threshold", "0"]
    )
    report = response["sharpening"]
    low, high = sum(f < 0 for f in raw), sum(f > 1 for f in raw)
    expect(
        report["clipped_low_samples"] == low and report["clipped_high_samples"] == high,
        "strict pre-clamp excursions",
    )
    expect(report["clipped_fraction"] == (low + high) / len(raw), "derived clipping fraction")


def protection_and_color(exe: Path, root: Path) -> None:
    """Constants/zero amount exact; protected pixels remain blur context and unchanged."""
    flat = Fixture(19, 13, ((23456,),) * (19 * 13), depth=16)
    out, _ = run(exe, root, flat, [*OPTIONS, "--sharpen-amount", "2", "--sharpen-threshold", "0"])
    expect(require_output(out).pixels == flat.pixels, "exact constant identity")
    out, response = run(exe, root, flat, [*OPTIONS, "--sharpen-amount", "0"])
    expect(require_output(out).pixels == flat.pixels, "zero amount exact")
    expect(response["sharpening"]["warnings"] == [], "zero amount no sharpening warning")
    width, height = 9, 7
    pixels = tuple((10000 + i * 17, 25000 + i * 31, 45000 - i * 13) for i in range(width * height))
    fixture = Fixture(width, height, pixels, depth=16, color=RGB)
    protected = [i % 3 == 0 for i in range(width * height)]
    mask = root / "protection.png"
    mask.write_bytes(Fixture(width, height, tuple((int(p),) for p in protected), depth=1).encoded())
    linear = [tuple(transfer_decode(c / WORD_MAX) for c in p) for p in pixels]
    ys = [sum(c * w for c, w in zip(p, (0.2126, 0.7152, 0.0722), strict=True)) for p in linear]
    fs = [transfer_encode(y) for y in ys]
    raw = unsharp(fs, (width, height), 0.8, 0.5, 0)
    out, response = run(
        exe, root, fixture, [*OPTIONS, "--sharpen-threshold", "0", "--protect-mask", str(mask)]
    )
    actual = require_output(out)
    for p, before, y, f, keep in zip(actual.pixels, linear, ys, raw, protected, strict=True):
        target = transfer_decode(min(1, max(0, f)))
        transported = (
            before
            if keep
            else tuple(
                c * target / y if target <= y else c + (1 - c) * (target - y) / (1 - y)
                for c in before
            )
        )
        expected = tuple(round(transfer_encode(c) * WORD_MAX) for c in transported)
        expect(
            max(abs(a - b) for a, b in zip(p, expected, strict=True)) <= TOLERANCE,
            "neutral-axis transport",
        )
    for i, keep in enumerate(protected):
        if keep:
            expect(actual.pixels[i] == pixels[i], "protected destinations exact")
    report = response["sharpening"]
    expect(
        report["evaluated_samples"] == protected.count(False), "protection excludes observations"
    )
    expect(report["context_samples"] == width * height, "protection retained as blur context")
    eligible = [f for f, keep in zip(raw, protected, strict=True) if not keep]
    expect(
        abs(report["pre_clamp"]["low"] - min(eligible)) < RANGE_TOLERANCE,
        "eligible excursion range",
    )


def bypasses_and_threshold_identity(exe: Path, root: Path) -> None:
    """Positive thresholded/protected identities retain their requested warning."""
    width, height = 9, 7
    pixels = tuple((30000 + i * 13,) for i in range(width * height))
    fixture = Fixture(width, height, pixels, depth=16)
    out, response = run(exe, root, fixture, [*OPTIONS, "--sharpen-threshold", "20"])
    expect(require_output(out).pixels == pixels, "thresholded low contrast identity exact")
    report = response["sharpening"]
    expect(
        report["status"] == "no_change" and report["reason"] == "no_effect", "threshold identity"
    )
    expect(report["evaluated_samples"] == len(pixels), "thresholded samples still evaluated once")
    expect(report["corrected_samples"] == 0 and report["changed_samples"] == 0, "no residual work")
    expect(report["warnings"] == ["W_SHARPENING"], "positive thresholded identity warns")
    mask = root / "all-protected.png"
    mask.write_bytes(Fixture(width, height, ((1,),) * len(pixels), depth=1).encoded())
    out, response = run(exe, root, fixture, [*OPTIONS, "--protect-mask", str(mask)])
    expect(require_output(out).pixels == pixels, "fully protected identity exact")
    report = response["sharpening"]
    expect(report["reason"] == "no_eligible_samples", "fully protected bypass reason")
    expect(report["warnings"] == ["W_SHARPENING"], "positive fully protected request warns")
    expect(report["pre_clamp"] is None and report["clipped_fraction"] is None, "no invented range")
    expect(
        all(
            report[name] == 0
            for name in (
                "eligible_samples",
                "context_samples",
                "evaluated_samples",
                "corrected_samples",
                "changed_samples",
                "clipped_low_samples",
                "clipped_high_samples",
                "preparation_charge_peak",
            )
        ),
        "fully protected bypass allocates no field and evaluates no pixels",
    )
    expect(report["protected_samples"] == len(pixels), "full mask counted once")
    bundle = max(
        (p for p in root.iterdir() if p.name.startswith("out-")), key=lambda p: int(p.name[4:])
    )
    call_json(exe, ["verify", str(bundle), "--json"])


def composition_and_records(exe: Path, root: Path) -> None:
    """All contrast alternatives feed sharpening; tampered closed observations fail."""
    width, height = 67, 51
    pixels = tuple((12000 + (i * 59) % 32000,) for i in range(width * height))
    fixture = Fixture(width, height, pixels, depth=16)
    values = [p[0] / WORD_MAX for p in pixels]
    alternatives = [
        ([], values),
        (["--contrast", "gamma"], [f**1.2 for f in values]),
        (["--contrast", "levels"], map_levels(values, 0.5, 99.5)[0]),
        (
            ["--contrast", "clahe", "--clahe-grid", "2x2"],
            clahe(values, (width, height), (2, 2), 2)[0],
        ),
    ]
    for options, entering in alternatives:
        raw = unsharp(entering, (width, height), 0.8, 0.5, 1)
        out, response = run(exe, root, fixture, [*options, *OPTIONS])
        expect(
            max(
                abs(p[0] - round(min(1, max(0, f)) * WORD_MAX))
                for p, f in zip(require_output(out).pixels, raw, strict=True)
            )
            <= TOLERANCE,
            "sharpening follows contrast without intermediate quantization",
        )
        expect(
            response["contrast"]["evaluated_samples"] in (0, width * height),
            "contrast observed once",
        )
    bundle = max(
        (p for p in root.iterdir() if p.name.startswith("out-")), key=lambda p: int(p.name[4:])
    )
    call_json(exe, ["verify", str(bundle), "--json"])
    path = bundle / "run.json"
    original = path.read_bytes()
    for field, value in (
        ("clipped_fraction", 0.75),
        ("context_samples", 0),
        ("preparation_charge_peak", 1),
    ):
        record = json.loads(original)
        record["execution"]["sharpening"][field] = value
        path.write_text(json.dumps(record), encoding="utf-8")
        call_json(exe, ["verify", str(bundle), "--json"], 3)
    path.write_bytes(original)


def admission(exe: Path, root: Path) -> None:
    """Presence, bounds, malformed options and binary output are refused without effects."""
    fixture = Fixture(1, 1, ((128,),))
    for options in (
        ["--sharpen-sigma", "1"],
        ["--sharpen", "off", "--sharpen-amount", "0"],
        [*OPTIONS, "--sharpen-sigma", "nan"],
        [*OPTIONS, "--sharpen-sigma", "0.29"],
        [*OPTIONS, "--sharpen-amount", "2.01"],
        [*OPTIONS, "--sharpen-threshold", "20.1"],
        [*OPTIONS, "--output-mode", "bw"],
        [*OPTIONS, "--sharpen-amount", "0", "--sharpen-sigma", "0"],
        [*OPTIONS, "--sharpen-amount", "0", "--sharpen-threshold", "-1"],
        [*OPTIONS, "--sharpen-amount", "0", "--sharpen-threshold", "nan"],
    ):
        run(exe, root, fixture, options, 2)


def main() -> None:
    """Execute the complete independent S01 contract suite."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        numerical(exe, root)
        protection_and_color(exe, root)
        bypasses_and_threshold_identity(exe, root)
        composition_and_records(exe, root)
        prefix_composition(exe, root)
        admission(exe, root)
    print("PASS: S01 constants, impulses, borders, protection, precision, composition and records")


if __name__ == "__main__":
    main()
