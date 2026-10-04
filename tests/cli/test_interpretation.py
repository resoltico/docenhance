#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Adversarial interpretation checks using independent scalar/PNG/TIFF construction."""

from __future__ import annotations

import math
import random
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from continuous_fixtures import GRAY, GRAY_ALPHA, RGB, RGBA, Fixture, chunks, decode_output, exif
from test_cli import call_json, expect
from test_continuous import rejected, run, transfer_decode, transfer_encode

WORD_MAX = 65535
PIXELS = 10000
SEED = 53117
COEFFICIENTS = (0.2126, 0.7152, 0.0722)
TIFF_SHORT = 3


@dataclass(frozen=True)
class ScalarIntent:
    """Output representation, matte and optional source gamma for the reference."""

    depth: int
    gray: bool
    matte: int
    gamma: int | None


def reference(pixel: tuple[int, ...], fixture: Fixture, intent: ScalarIntent) -> tuple[int, ...]:
    """Normalize/interpret, composite and quantize with no float32 intermediate."""
    color = fixture.color
    source_max = (1 << fixture.depth) - 1
    alpha = pixel[-1] / source_max if color in (GRAY_ALPHA, RGBA) else 1.0
    count = 3 if color in (RGB, RGBA) else 1
    samples = tuple(value / source_max if alpha else 0.0 for value in pixel[:count])
    linear = tuple(
        value ** (100000 / intent.gamma) if intent.gamma else transfer_decode(value)
        for value in samples
    )
    values = tuple(alpha * value + (1 - alpha) * intent.matte for value in linear)
    if count == 1:
        values *= 3
    if intent.gray or count == 1:
        values = (
            sum(
                coefficient * value for coefficient, value in zip(COEFFICIENTS, values, strict=True)
            ),
        )
    return tuple(
        math.floor(((1 << intent.depth) - 1) * transfer_encode(value) + 0.5) for value in values
    )


def scalar_precision(exe: Path, root: Path) -> None:
    """Cover seeded half-level boundaries, alpha, gray/RGB and gamma with exact codes."""
    rng = random.Random(SEED)  # noqa: S311 - Reproducible numerical fixtures, no security use.
    for color in (RGB, RGBA, GRAY_ALPHA):
        count = 3 if color == RGB else 4 if color == RGBA else 2
        for depth in (8, 16):
            maximum = (1 << depth) - 1
            pixels = tuple(
                tuple(rng.randrange(maximum + 1) for _ in range(count)) for _ in range(PIXELS)
            )
            fixture = Fixture(100, 100, pixels, color, depth)
            for output_depth, gray, matte, gamma in (
                (16, True, 1, None),
                (16, False, 1, None),
                (16, False, 0, None),
                (8, True, 1, None),
                (16, True, 1, 100000),
            ):
                if gamma:
                    fixture = Fixture(
                        100,
                        100,
                        pixels,
                        color,
                        depth,
                        metadata=((b"gAMA", struct.pack(">I", gamma)),),
                    )
                else:
                    fixture = Fixture(100, 100, pixels, color, depth)
                options = [
                    "--bit-depth",
                    str(output_depth),
                    "--output-mode",
                    "gray" if gray else "preserve",
                    "--alpha",
                    "white" if matte else "black",
                ]
                output, report, _ = run(exe, root, fixture, options)
                intent = ScalarIntent(output_depth, gray, matte, gamma)
                expected = tuple(reference(pixel, fixture, intent) for pixel in pixels)
                expect(
                    output.pixels == expected, f"exact scalar {color}/{depth}->{output_depth} codes"
                )
                expected_flattened = (
                    sum(pixel[-1] != maximum for pixel in pixels) if color != RGB else 0
                )
                expect(
                    report["alpha_flattened_pixels"] == expected_flattened,
                    "alpha observations occur exactly once",
                )


def physical_exif(
    endian: str,
    x: tuple[int, int] | None,
    y: tuple[int, int] | None,
    unit: int = 2,
    orientation: int = 1,
) -> bytes:
    """Construct selected IFD0 fields, including deliberately invalid rational values."""
    fields = [(0x112, 3, struct.pack(endian + "H", orientation) + b"\0\0")]
    for tag, rational in ((0x11A, x), (0x11B, y)):
        if rational is not None:
            fields.append((tag, 5, struct.pack(endian + "II", *rational)))
    fields.append((0x128, 3, struct.pack(endian + "H", unit) + b"\0\0"))
    offset = 8 + 2 + 12 * len(fields) + 4
    entries, tail = bytearray(), bytearray()
    for tag, kind, data in fields:
        entries.extend(struct.pack(endian + "HHI", tag, kind, 1))
        if kind == TIFF_SHORT:
            entries.extend(data)
        else:
            entries.extend(struct.pack(endian + "I", offset + len(tail)))
            tail.extend(data)
    return (
        (b"II" if endian == "<" else b"MM")
        + struct.pack(endian + "HIH", 42, 8, len(fields))
        + entries
        + b"\0" * 4
        + tail
    )


def resolution_validation(exe: Path, root: Path) -> None:
    """Validate EXIF before selecting pHYs, with exact units, endianness and axis swaps."""
    pixels = ((10,), (30,), (60,), (90,), (150,), (230,))
    phys = (b"pHYs", struct.pack(">IIB", 1000, 2000, 1))
    for endian in ("<", ">"):
        for x, y, unit in (
            ((0, 1), (72, 1), 2),
            ((72, 0), (72, 1), 2),
            ((72, 1), None, 2),
            ((WORD_MAX * WORD_MAX, 1), (72, 1), 2),
            ((0, 1), (72, 1), 1),
        ):
            invalid = physical_exif(endian, x, y, unit)
            for precedence in ((), (phys,)):
                rejected(
                    exe, root, Fixture(3, 2, pixels, metadata=(*precedence, (b"eXIf", invalid)))
                )
        for unit, expected in ((1, None), (2, (2835, 5669)), (3, (7200, 14400))):
            metadata = (b"eXIf", physical_exif(endian, (72, 1), (144, 1), unit))
            _, _, encoded = run(exe, root, Fixture(3, 2, pixels, metadata=(metadata,)))
            if expected:
                expect(
                    struct.unpack(">IIB", chunks(encoded)[b"pHYs"][0]) == (*expected, 1),
                    "physical units rounded once",
                )
            else:
                expect(b"pHYs" not in chunks(encoded), "unitless resolution is not DPI")
        metadata = (b"eXIf", physical_exif(endian, (72, 1), (144, 1), orientation=6))
        output, _, encoded = run(exe, root, Fixture(3, 2, pixels, metadata=(phys, metadata)))
        expect((output.width, output.height) == (2, 3), "orientation changes dimensions once")
        expect(
            struct.unpack(">IIB", chunks(encoded)[b"pHYs"][0]) == (2000, 1000, 1),
            "valid pHYs precedence and oriented axes",
        )


def binary_framing(exe: Path, root: Path) -> None:
    """Static framing applies to binary input while color/orientation never change samples."""
    fixture = Fixture(2, 1, ((100,), (200,)), GRAY)
    plain = fixture.encoded()
    cases = (
        plain + b"another object",
        Fixture(2, 1, fixture.pixels, metadata=((b"acTL", struct.pack(">II", 1, 0)),)).encoded(),
        Fixture(2, 1, fixture.pixels, metadata=((b"fcTL", b"unsupported"),)).encoded(),
        Fixture(2, 1, fixture.pixels, metadata=((b"fdAT", b"unsupported"),)).encoded(),
    )
    for number, data in enumerate(cases):
        source = root / f"framing-{number}.png"
        source.write_bytes(data)
        before = set(root.iterdir())
        result = call_json(
            exe,
            [
                "process",
                str(source),
                "--out-dir",
                str(root / "refused"),
                "--output-mode",
                "bw",
                "--json",
            ],
            3,
        )
        expect(result["publication"] == "not_started", "invalid framing precedes publication")
        expect(
            set(root.iterdir()) == before and source.read_bytes() == data,
            "refusal preserves source and creates no output",
        )
    source = root / "stored-samples.png"
    source.write_bytes(
        Fixture(
            2,
            1,
            fixture.pixels,
            metadata=(
                (b"gAMA", struct.pack(">I", 100000)),
                (b"eXIf", exif(6)),
            ),
        ).encoded()
    )
    output = root / "stored-output"
    call_json(
        exe,
        [
            "process",
            str(source),
            "--out-dir",
            str(output),
            "--output-mode",
            "bw",
            "--binarize",
            "fixed",
            "--json",
        ],
    )
    decoded = decode_output((output / "result.png").read_bytes())
    expect(
        (decoded.width, decoded.height, decoded.pixels) == (2, 1, ((0,), (255,))),
        "stored binary samples ignore gamma and orientation",
    )


def main() -> None:
    """Run actual processing paths; references do not import production numerical code."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-interpretation-") as temporary:
        root = Path(temporary)
        scalar_precision(exe, root)
        resolution_validation(exe, root)
        binary_framing(exe, root)
    print("PASS: scalar precision, metadata validation and static binary framing")


if __name__ == "__main__":
    main()
