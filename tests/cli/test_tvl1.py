# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""D02 numerical, precision, protection, stopping and composition contracts at the real boundary."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import RGB, RGBA, Fixture, exif
from illumination_reference import Plane, surface
from morphology_reference import background
from test_cli import call, call_json, expect
from test_continuous import transfer_decode, transfer_encode
from test_illumination import LINEAR, require_output, run
from tvl1_reference import TvSettings, solve

FIELD_TOLERANCE = 1e-10
WORD_MAX = 65535
EARLIEST_STOP = 30
MINIMUM_TV_TOLERANCE = 1e-8
COLOR_WORD_TOLERANCE = 4


def numerical(exe: Path, root: Path) -> None:
    """Compare output and diagnostics to simultaneous double-precision reference iterates."""
    width, height = 13, 9
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        pixels = tuple(
            (round(maximum * (0.65 + (((i * 37) % 101) - 50) / 1000)),)
            for i in range(width * height)
        )
        fixture = Fixture(width, height, pixels, depth=depth, metadata=LINEAR)
        values = [transfer_encode(p[0] / maximum) for p in pixels]
        reference = solve(values, width, height, TvSettings(cap=400, tolerance=1e-8))
        output, response = run(
            exe,
            root,
            fixture,
            [
                "--denoise",
                "tvl1",
                "--tv-iterations",
                "400",
                "--tv-tolerance",
                "1e-8",
                "--denoise-blend",
                "1",
            ],
        )
        output = require_output(output)
        report = response["denoising"]
        detail = report["tvl1"]
        expect(report["method"] == {"id": "D02", "method_version": 1}, "D02 identity")
        expect(
            report["representation"] == "float64" and report["native_calls"] == 0,
            "floating full field is not native integer analysis",
        )
        expect(
            detail["iterations"] == reference.iterations and detail["stop"] == reference.stop,
            "actual stopping criterion",
        )
        expect(
            abs(detail["primal_update"] - reference.primal_update) < FIELD_TOLERANCE
            and abs(detail["dual_update"] - reference.dual_update) < FIELD_TOLERANCE,
            "independent final primal and dual update norms",
        )
        expect(
            abs(detail["objective_start"] - reference.objective_start) < FIELD_TOLERANCE,
            "independent initial objective",
        )
        expect(
            abs(detail["objective_end"] - reference.objective_end) < FIELD_TOLERANCE,
            "independent final objective",
        )
        expect(
            detail["objective_end"] < detail["objective_start"],
            "defined noisy fixture reduces objective",
        )
        expect(
            max(
                abs(p[0] / maximum - value)
                for p, value in zip(output.pixels, reference.values, strict=True)
            )
            <= 2 / maximum,
            "independent TV result without eight-bit quantization",
        )
        expect(report["evaluated_samples"] == width * height, "reconstruction counted once")


def stopping_and_precision(exe: Path, root: Path) -> None:
    """Short caps warn honestly, constants are exact, and 16-bit distinctions survive."""
    fixture = Fixture(8, 4, ((20000,),) * 32, depth=16, metadata=LINEAR)
    baseline, _ = run(exe, root, fixture, [])
    for cap in (10, 20, 29, 30):
        output, response = run(
            exe, root, fixture, ["--denoise", "tvl1", "--tv-iterations", str(cap)]
        )
        report = response["denoising"]
        expect(output == baseline, "constant identity")
        expect(report["tvl1"]["iterations"] == cap, "constant uses actual iterations")
        expect(
            report["warnings"] == (["W_TV_ITERATION_LIMIT"] if cap < EARLIEST_STOP else []),
            "iteration-limit warning matches two-checkpoint policy",
        )
    # A fixed primal is insufficient: high fidelity pins u while the dual field still moves.
    field = Fixture(9, 5, tuple((30000 + i * 7,) for i in range(45)), depth=16)
    options = [
        "--denoise",
        "tvl1",
        "--tv-lambda",
        "20",
        "--tv-iterations",
        "40",
        "--tv-tolerance",
        "1e-8",
    ]
    _, response = run(exe, root, field, options)
    detail = response["denoising"]["tvl1"]
    expect(
        detail["primal_update"] == 0 and detail["dual_update"] > MINIMUM_TV_TOLERANCE,
        "dual criterion independently prevents a false stopping claim",
    )
    expect(
        detail["stop"] == "iteration_limit" and detail["passing_checkpoints"] == 0,
        "both update criteria are required",
    )
    text = call(
        exe,
        [
            "process",
            str(root / "source.png"),
            "--out-dir",
            str(root / f"out-{len(list(root.iterdir()))}"),
            *options,
        ],
    )
    dual_text = text.split("dual update=", 1)[1].split(";", 1)[0]
    expect(float(dual_text) == detail["dual_update"], "text preserves small measured residuals")
    faint = Fixture(8, 4, tuple((30000 + i % 2,) for i in range(32)), depth=16)
    baseline, _ = run(exe, root, faint, [])
    output, _ = run(exe, root, faint, ["--denoise", "tvl1", "--tv-lambda", "20"])
    expect(
        output == baseline and len(set(require_output(output).pixels)) > 1,
        "one-code distinctions are not collapsed into eight bits",
    )


def protection_and_admission(exe: Path, root: Path) -> None:
    """Protection restores destinations while keeping all samples as optimization context."""
    fixture = Fixture(
        9, 5, tuple((12000 + (i * 47 % 13000),) for i in range(45)), depth=16, metadata=LINEAR
    )
    mask = root / "tv-mask.png"
    mask.write_bytes(Fixture(9, 5, ((1,),) + ((0,),) * 44, depth=1).encoded())
    before = mask.read_bytes()
    baseline, _ = run(exe, root, fixture, [])
    output, response = run(
        exe,
        root,
        fixture,
        ["--denoise", "tvl1", "--protect-mask", str(mask), "--denoise-blend", "1"],
    )
    output = require_output(output)
    expect(
        output.pixels[0] == require_output(baseline).pixels[0] and mask.read_bytes() == before,
        "protected destination and mask remain exact",
    )
    reference = solve(
        [transfer_encode(p[0] / WORD_MAX) for p in fixture.pixels], 9, 5, TvSettings()
    )
    expect(
        max(abs(output.pixels[i][0] / WORD_MAX - reference.values[i]) for i in range(1, 45))
        <= 2 / WORD_MAX,
        "protected samples remain solver context",
    )
    expect(
        response["denoising"]["evaluated_samples"] == len(fixture.pixels) - 1,
        "protected sample not counted",
    )
    for options in (
        ["--tv-lambda", "1"],
        ["--denoise", "nlm", "--tv-iterations", "150"],
        ["--denoise", "tvl1", "--nlm-h", "3"],
        ["--denoise", "tvl1", "--tv-lambda", "0"],
        ["--denoise", "tvl1", "--tv-iterations", "9"],
        ["--denoise", "tvl1", "--tv-iterations", "1001"],
        ["--denoise", "tvl1", "--tv-iterations", "1e2"],
        ["--denoise", "tvl1", "--tv-tolerance", "0"],
        ["--denoise", "tvl1", "--output-mode", "bw"],
    ):
        run(exe, root, fixture, options, 2)
    for options in (
        ["--denoise", "tvl1", "--denoise-blend", "0"],
        ["--denoise", "tvl1", "--protect-mask", str(mask)],
    ):
        if "--protect-mask" in options:
            mask.write_bytes(Fixture(9, 5, ((1,),) * 45, depth=1).encoded())
        output, response = run(exe, root, fixture, options)
        expect(
            output == baseline and response["denoising"]["tvl1"]["iterations"] == 0,
            "validated no-op does not manufacture solver execution",
        )
    mask.write_bytes(Fixture(5, 9, ((1,),) * 45, depth=1).encoded())
    run(
        exe,
        root,
        fixture,
        ["--denoise", "tvl1", "--denoise-blend", "0", "--protect-mask", str(mask)],
        3,
    )


def composition_and_record(exe: Path, root: Path) -> None:
    """Both illumination alternatives feed TV; verification preserves the immutable solved field."""
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
            combined.pixels != entering.pixels and report["denoising"]["changed_samples"] > 0,
            "composition fixture must exercise an active TV correction",
        )
        expect(
            report["illumination"]["complete"]
            and report["illumination"]["application"]["evaluated_samples"] == len(fixture.pixels),
            "illumination counted once before TV",
        )
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
    path.write_bytes(original)
    color = Fixture(8, 4, ((12000, 22000, 42000),) * 32, depth=16, color=RGB, metadata=LINEAR)
    baseline, _ = run(exe, root, color, [])
    output, _ = run(exe, root, color, ["--denoise", "tvl1"])
    expect(output == baseline, "constant luminance keeps original color")


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
    expect(response["denoising"]["changed_samples"] > 0, "color fixture actually changes")
    expect(
        response["conversion"]["alpha_flattened_pixels"] == sum(p[3] < WORD_MAX for p in pixels),
        "alpha observations counted once through solver and verification",
    )


def main() -> None:
    """Run against production; every result and mask lives in an owned temporary directory."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-tv-") as directory:
        root = Path(directory)
        numerical(exe, root)
        stopping_and_precision(exe, root)
        protection_and_admission(exe, root)
        composition_and_record(exe, root)
        color_alpha_orientation(exe, root)
    print("PASS: five TV-L1 objective, precision, protection and composition groups")


if __name__ == "__main__":
    main()
