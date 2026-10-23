# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""D02 numerical, precision, protection, stopping and composition contracts at the real boundary."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from continuous_fixtures import Fixture
from illumination_io import LINEAR, require_output, run
from test_cli import call, expect
from test_continuous import transfer_encode
from tvl1_composition import binary_composition, color_alpha_orientation, composition_and_record
from tvl1_reference import TvSettings, solve

FIELD_TOLERANCE = 1e-10
WORD_MAX = 65535
EARLIEST_STOP = 30
MINIMUM_TV_TOLERANCE = 1e-8


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
        report = response["record"]["execution"]["denoising"]
        detail = report["solver"]
        expect(report["method"] == "D02", "D02 identity")
        expect(
            report["context_samples"] == width * height and report["solver"] is not None,
            "actual full floating solver context and complete solution",
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
        expect(report["counts"]["evaluated"] == width * height, "reconstruction counted once")


def stopping_and_precision(exe: Path, root: Path) -> None:
    """Short caps warn honestly, constants are exact, and 16-bit distinctions survive."""
    fixture = Fixture(8, 4, ((20000,),) * 32, depth=16, metadata=LINEAR)
    baseline, _ = run(exe, root, fixture, [])
    for cap in (10, 20, 29, 30):
        output, response = run(
            exe, root, fixture, ["--denoise", "tvl1", "--tv-iterations", str(cap)]
        )
        report = response["record"]["execution"]["denoising"]
        expect(output == baseline, "constant identity")
        expect(report["solver"]["iterations"] == cap, "constant uses actual iterations")
        expect(
            response["warnings"].count("W_TV_ITERATION_LIMIT") == int(cap < EARLIEST_STOP),
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
    detail = response["record"]["execution"]["denoising"]["solver"]
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
    dual_text = text.split("dual_update: ", 1)[1].splitlines()[0]
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
        response["record"]["execution"]["denoising"]["counts"]["evaluated"]
        == len(fixture.pixels) - 1,
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
            output == baseline
            and response["record"]["execution"]["denoising"]["solver"] is None
            and response["record"]["execution"]["denoising"]["counts"]["evaluated"] == 0,
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
        binary_composition(exe, root)
    print("PASS: TV-L1 objective, precision, protection, composition and terminal controls")


if __name__ == "__main__":
    main()
