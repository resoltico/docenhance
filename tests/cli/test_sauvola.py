#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Execute B02 and B03 through production admission, codecs, publication and JSON reporting."""

from __future__ import annotations

import math
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import Fixture
from contract_reference import reviewed_methods
from grayscale import read_logical_image, write_image
from test_cli import call_json, expect

ARGUMENT_ERROR = 2
WHITE = 255
WORD_MAX = 65535
BINARY_METHOD_VERSION = 2
INK_TOP, INK_BOTTOM = 3, 13


def reflected(index: int, extent: int) -> int:
    """Independent REFLECT_101 definition, with the singleton convention explicit."""
    if extent == 1:
        return 0
    period = 2 * (extent - 1)
    folded = index % period
    return folded if folded < extent else period - folded


def oracle(rows: list[list[int]], window: int, k: float, r: float) -> list[bytes]:
    """Enumerate Q16 windows with exact integer variance and normalized binary64 math."""
    radius, n = window // 2, window * window
    output = []
    for y, row in enumerate(rows):
        result = bytearray()
        for x, pixel in enumerate(row):
            values = [
                rows[reflected(yy, len(rows))][reflected(xx, len(row))]
                for yy in range(y - radius, y + radius + 1)
                for xx in range(x - radius, x + radius + 1)
            ]
            total = sum(values)
            squares = sum(p * p for p in values)
            mean = float(total) / float(n)
            deviation = math.sqrt(float(n * squares - total * total)) / float(n)
            threshold = mean * (1.0 + k * (deviation / (WORD_MAX * r) - 1.0))
            result.append(0 if pixel <= threshold else 1)
        output.append(bytes(result))
    return output


def command(source: Path, output: Path, selector: str = "sauvola") -> list[str]:
    """Return a real processing invocation, with JSON for schema and identity checks."""
    return [
        "process",
        str(source),
        "--out-dir",
        str(output),
        "--output-mode",
        "bw",
        "--binarize",
        selector,
        "--json",
    ]


def sample_cases(exe: Path, root: Path) -> None:
    """Exercise both dimensions, singleton borders, defaults, overrides and threshold extrema."""
    for number, (width, height, window, k, r) in enumerate(
        (
            (13, 7, 7, 0.2, 0.5),
            (1, 3, 31, 0.2, 0.5),
            (9, 1, 31, 0.2, 0.5),
            (5, 3, 3, 0.0, 1.0),
            (5, 3, 3, 1.0, 1 / WHITE),
        )
    ):
        rows = [bytes((x * 71 + y * 113) % 256 for x in range(width)) for y in range(height)]
        source, output = root / f"case-{number}.png", root / f"case-{number}"
        write_image(source, rows)
        original = source.read_bytes()
        response = call_json(
            exe,
            [
                *command(source, output),
                "--sauvola-window",
                str(window),
                "--sauvola-k",
                str(k),
                "--sauvola-r",
                str(r),
            ],
        )
        request = response["record"]["request"]["binarization"]
        expect(
            request["method"] == "B02" and request["version"] == BINARY_METHOD_VERSION,
            "current B02 identity",
        )
        expect(response["publication"] == "completed", "B02 publication")
        expect(response["output"] == str(output / "result.png"), "output identity")
        expect(
            read_logical_image(output / "result.png")
            == oracle([[257 * p for p in row] for row in rows], window, k, r),
            "B02 reference",
        )
        expect(source.read_bytes() == original, "source preserved")
    source, output = root / "constant.png", root / "maximum"
    write_image(source, [b"\x7f"])
    call_json(exe, [*command(source, output), "--sauvola-window", "4095"])
    expect(read_logical_image(output / "result.png") == [b"\x01"], "maximum window")
    default_output = root / "defaults"
    call_json(exe, command(source, default_output))
    expect(read_logical_image(default_output / "result.png") == [b"\x01"], "B02 defaults")
    fixed_output = root / "fixed"
    response = call_json(exe, command(source, fixed_output, "fixed"))
    request = response["record"]["request"]["binarization"]
    expect(
        request["method"] == "B03" and request["version"] == BINARY_METHOD_VERSION,
        "current B03 identity",
    )
    expect(read_logical_image(fixed_output / "result.png") == [b"\0"], "B03 remains unchanged")


def q16_cases(exe: Path, root: Path) -> None:
    """Full-range and one-code values plus maximal constant moments cannot use a byte bridge."""
    rows = [[(y * 11377 + x * 7919 + x % 2) % 65536 for x in range(7)] for y in range(5)]
    source = root / "q16.png"
    source.write_bytes(Fixture(7, 5, tuple((p,) for row in rows for p in row), depth=16).encoded())
    for number, (window, k, r) in enumerate(((3, 0.0, 1.0), (7, 0.2, 0.5), (31, 1.0, 1 / WHITE))):
        output = root / f"q16-{number}"
        response = call_json(
            exe,
            [
                *command(source, output),
                "--sauvola-window",
                str(window),
                "--sauvola-k",
                str(k),
                "--sauvola-r",
                str(r),
            ],
        )
        expected = oracle(rows, window, k, r)
        expect(read_logical_image(output / "result.png") == expected, "independent Q16 population")
        facts = response["record"]["execution"]["binarization"]
        expect(
            facts["black_samples"] == sum(row.count(0) for row in expected)
            and facts["white_samples"] == sum(row.count(1) for row in expected),
            "actual bit counts",
        )
    subbyte = [[10000, 10001, 10002]]
    source = root / "q16-subbyte.png"
    source.write_bytes(Fixture(3, 1, tuple((p,) for p in subbyte[0]), depth=16).encoded())
    output = root / "q16-subbyte"
    expected = oracle(subbyte, 3, 0, 1)
    expect(
        expected == [b"\0\0\x01"], "one-code local mean decisions do not collapse to byte samples"
    )
    call_json(exe, [*command(source, output), "--sauvola-window", "3", "--sauvola-k", "0"])
    expect(read_logical_image(output / "result.png") == expected, "full precision local mean")
    source = root / "q16-constant.png"
    source.write_bytes(Fixture(1, 1, ((WORD_MAX,),), depth=16).encoded())
    n = 4095 * 4095
    total, squares = n * WORD_MAX, n * WORD_MAX * WORD_MAX
    expect(
        n * squares > (1 << 64) and n * squares - total * total == 0,
        "independent maximal exact variance exceeds uint64 products",
    )
    for k, expected_row in ((0.2, b"\x01"), (1e-20, b"\0")):
        output = root / f"q16-constant-{k}"
        call_json(
            exe, [*command(source, output), "--sauvola-window", "4095", "--sauvola-k", str(k)]
        )
        expect(
            read_logical_image(output / "result.png") == [expected_row],
            "maximal wide moments and literal tiny-k constant equality",
        )


def fixed_q16_cases(exe: Path, root: Path) -> None:
    """Fixed compares normalized terminal Q16, including exact endpoints and the central split."""
    values = (0, 32767, 32768, 65534, 65535)
    source = root / "fixed-q16.png"
    source.write_bytes(Fixture(len(values), 1, tuple((p,) for p in values), depth=16).encoded())
    for number, threshold in enumerate((0.0, 0.5, 32768 / WORD_MAX, 1.0)):
        output = root / f"fixed-q16-{number}"
        expected = bytes(0 if p / WORD_MAX <= threshold else 1 for p in values)
        response = call_json(
            exe, [*command(source, output, "fixed"), "--fixed-threshold", str(threshold)]
        )
        expect(
            read_logical_image(output / "result.png") == [expected],
            "normalized Q16 Fixed equality",
        )
        facts = response["record"]["execution"]["binarization"]
        expect(
            facts["black_samples"] == expected.count(0)
            and facts["white_samples"] == expected.count(1),
            "full classification counts",
        )


def rejection_cases(exe: Path, root: Path) -> None:
    """Wrong-method, empty, non-finite and out-of-domain options fail before reading input."""
    candidates = [
        ("fixed", "--sauvola-window", "31"),
        ("fixed", "--sauvola-k", "0.2"),
        ("fixed", "--sauvola-r", "0.5"),
        ("fixed", "--sauvola-k", ""),
        ("sauvola", "--fixed-threshold", "0.5"),
        ("sauvola", "--fixed-threshold", ""),
        ("fixed", "--fixed-threshold", ""),
    ]
    candidates += [
        ("sauvola", "--sauvola-window", value)
        for value in ("", "0", "1", "2", "4096", "4294967296", "+3", "-3", "3.0", "3e0")
    ]
    candidates += [
        ("sauvola", name, value)
        for name in ("--sauvola-k", "--sauvola-r")
        for value in ("", "nan", "inf", "-0.1", "1.1")
    ]
    candidates += [("sauvola", "--sauvola-r", value) for value in ("0", "1e-300")]
    for selector, name, value in candidates:
        output = root / "never-created"
        response = call_json(
            exe, [*command(root / "absent.png", output, selector), name, value], ARGUMENT_ERROR
        )
        expect(response["publication"] == "not_started", "rejected before effects")
        expect(not output.exists(), "invalid method produced no output")


def discovery_cases(exe: Path) -> None:
    """Capabilities come from executable method types, including selected-method discovery."""
    for identity in ("B02", "B03"):
        response = call_json(exe, ["methods", identity, "--json"])
        expect(
            response["methods"] == [m for m in reviewed_methods() if m["id"] == identity],
            "selected method",
        )


def illumination_case(exe: Path, root: Path) -> None:
    """A synthetic text mask checks a known uneven-background case, not general image quality."""
    width, height = 63, 17
    mask = [
        [INK_TOP <= y <= INK_BOTTOM and x % 9 in (3, 4) for x in range(width)]
        for y in range(height)
    ]
    rows = [
        bytes((80 + x * 170 // (width - 1)) // (5 if mask[y][x] else 1) for x in range(width))
        for y in range(height)
    ]
    truth = [bytes(0 if ink else 1 for ink in row) for row in mask]
    source, adaptive, fixed = root / "illumination.png", root / "adaptive", root / "global"
    write_image(source, rows)
    call_json(exe, [*command(source, adaptive), "--sauvola-window", "15"])
    call_json(exe, command(source, fixed, "fixed"))
    actual = read_logical_image(adaptive / "result.png")
    expect(
        actual == oracle([[257 * p for p in row] for row in rows], 15, 0.2, 0.5),
        "illumination reference",
    )
    expect(actual == truth, "synthetic text mask preserved")
    expect(
        read_logical_image(fixed / "result.png") != truth,
        "fixture distinguishes local thresholding",
    )


def main() -> int:
    """Run real-executable conformance and the explicitly synthetic quality case."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        discovery_cases(exe)
        sample_cases(exe, root)
        q16_cases(exe, root)
        fixed_q16_cases(exe, root)
        rejection_cases(exe, root)
        illumination_case(exe, root)
    print("PASS: typed B02/B03 execution, reference samples, admission and synthetic illumination")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
