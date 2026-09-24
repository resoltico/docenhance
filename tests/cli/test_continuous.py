#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real executable color/precision/metadata tests with an independent PNG/sample reader."""

from __future__ import annotations

import dataclasses
import struct
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any

from continuous_fixtures import (
    GRAY_ALPHA,
    INDEXED,
    RGB,
    RGBA,
    Fixture,
    chunks,
    decode_output,
    exif,
    gamma_profile,
    profile_from_output,
)
from test_cli import call_json, expect

BYTE_MAX = 255
WORD_MAX = 65535
GRAY_CURVE_KNOTS = 65530
BYTE_DEPTH = 8
TRANSPARENT_PIXELS = 2
VISIBLE_GAMMA_DIFFERENCE = 20
TRANSPOSED_ORIENTATION = 5
ENCODED_BREAKPOINT = 0.04045
LINEAR_BREAKPOINT = 0.0031308
SRGB = (31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000)


def run(
    exe: Path, root: Path, fixture: Fixture, options: list[str] | None = None
) -> tuple[Fixture, dict[str, Any], bytes]:
    """Run an operation, prove source preservation and independently read its output."""
    source = root / "source.png"
    source.write_bytes(fixture.encoded())
    output = root / f"output-{len(list(root.iterdir()))}"
    result = call_json(
        exe, ["process", str(source), "--out-dir", str(output), *(options or []), "--json"]
    )
    expect(source.read_bytes() == fixture.encoded(), "source samples and metadata preserved")
    encoded = (output / "result.png").read_bytes()
    expect(
        result["operation"] == "continuous" and result["conversion"]["verified"],
        "truthful verified conversion result",
    )
    expect(
        "method" not in result and "method_version" not in result, "no fictional enhancement method"
    )
    expect(
        set(chunks(encoded)) <= {b"IHDR", b"iCCP", b"pHYs", b"IDAT", b"IEND"},
        "minimal metadata inventory",
    )
    return decode_output(encoded), result["conversion"], encoded


def rejected(
    exe: Path, root: Path, fixture: Fixture, options: list[str] | None = None, code: int = 3
) -> None:
    """Require an explicit failure and no final or leftover staging output."""
    source = root / "bad.png"
    source.write_bytes(fixture.encoded())
    before = set(root.iterdir())
    result = call_json(
        exe,
        ["process", str(source), "--out-dir", str(root / "refused"), *(options or []), "--json"],
        code,
    )
    expect(result["publication"] in {"not_started", "not_published"}, "failure does not publish")
    expect(set(root.iterdir()) == before, "failed conversion leaves no staging")


def transfer_decode(value: float) -> float:
    """Independent scalar sRGB sample-to-linear reference."""
    return value / 12.92 if value <= ENCODED_BREAKPOINT else ((value + 0.055) / 1.055) ** 2.4


def transfer_encode(value: float) -> float:
    """Independent scalar linear-to-sRGB reference."""
    return 12.92 * value if value <= LINEAR_BREAKPOINT else 1.055 * value ** (1 / 2.4) - 0.055


def precision(exe: Path, root: Path) -> None:
    """Exercise all 65,536 gray levels, all static sample models and exact low-bit expansion."""
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        pixels: tuple[tuple[int, ...], ...] = tuple((i,) for i in range(maximum + 1))
        fixture = Fixture(256, len(pixels) // 256, pixels, depth=depth)
        output, _, first = run(exe, root, fixture)
        expect(
            output.pixels == fixture.pixels and output.depth == depth,
            "dense integer precision preserved",
        )
        _, _, second = run(exe, root, fixture)
        expect(first == second, "canonical output bytes repeat on this build")
    for color in (RGB, RGBA, GRAY_ALPHA):
        for depth in (8, 16):
            maximum = (1 << depth) - 1
            count = 3 if color in (RGB, RGBA) else 1
            pixels = tuple(
                tuple((i * 13 + c * 31) % maximum for c in range(count))
                + ((maximum,) if color != RGB else ())
                for i in range(35)
            )
            fixture = Fixture(7, 5, pixels, color, depth, interlaced=True)
            output, _, _ = run(exe, root, fixture)
            expect(
                output.pixels == tuple(p[:count] for p in pixels),
                "Adam7 color/alpha/precision samples",
            )
    for depth in (1, 2, 4):
        maximum = (1 << depth) - 1
        fixture = Fixture(maximum + 1, 1, tuple((i,) for i in range(maximum + 1)), depth=depth)
        output, _, _ = run(exe, root, fixture)
        expect(
            output.pixels == tuple((i * BYTE_MAX // maximum,) for i in range(maximum + 1)),
            "low-depth expansion",
        )
    fixture = Fixture(3, 1, ((0,), (256,), (257,)), depth=16)
    output, report, _ = run(exe, root, fixture, ["--bit-depth", "8"])
    expect(
        output.depth == BYTE_DEPTH
        and report["depth_reduced"]
        and "W_DEPTH_REDUCED" in report["warnings"],
        "explicit precision reduction",
    )


def alpha_and_gray(exe: Path, root: Path) -> None:
    """Distinguish linear compositing and luminance from averaging encoded RGB."""
    fixture = Fixture(3, 1, ((255, 0, 0, 0), (0, 0, 0, 128), (128, 128, 128, 255)), RGBA)
    output, report, _ = run(exe, root, fixture)
    expected_middle = round(transfer_encode(1 - 128 / BYTE_MAX) * BYTE_MAX)
    expect(
        output.pixels == ((255, 255, 255), (expected_middle,) * 3, (128, 128, 128)),
        "linear white matte and hidden zero alpha",
    )
    expect(
        report["alpha_flattened_pixels"] == TRANSPARENT_PIXELS,
        "verification does not double-count alpha",
    )
    output, _, _ = run(exe, root, fixture, ["--alpha", "black"])
    expect(output.pixels[0] == (0, 0, 0), "black matte")
    rejected(exe, root, fixture, ["--alpha", "reject"])
    colors = Fixture(3, 1, ((255, 0, 0), (0, 255, 0), (0, 0, 255)), RGB)
    output, _, _ = run(exe, root, colors, ["--output-mode", "gray", "--bit-depth", "16"])
    expected = tuple((round(transfer_encode(y) * WORD_MAX),) for y in (0.2126, 0.7152, 0.0722))
    expect(output.pixels == expected, "linear RGB luminance")
    palette = Fixture(
        3,
        1,
        ((0,), (1,), (2,)),
        INDEXED,
        depth=2,
        metadata=((b"PLTE", b"\xff\0\0\0\xff\0\0\0\xff"), (b"tRNS", b"\0\xff\xff")),
    )
    output, _, _ = run(exe, root, palette)
    expect(
        output.pixels == ((255, 255, 255), (0, 255, 0), (0, 0, 255)),
        "palette transparency expands before compositing",
    )
    transparency = Fixture(2, 1, ((1,), (2,)), depth=16, metadata=((b"tRNS", b"\0\x01"),))
    output, _, _ = run(exe, root, transparency)
    expect(output.pixels == ((65535,), (2,)), "16-bit tRNS comparison before interpretation")


def profiles(exe: Path, root: Path) -> None:
    """Check gamma direction, native ICC transforms, precedence, override, and profile bytes."""
    fixture = Fixture(3, 1, ((0,), (128,), (255,)))
    linear = dataclasses.replace(fixture, metadata=((b"gAMA", struct.pack(">I", 100000)),))
    output, _, _ = run(exe, root, linear)
    expect(
        output.pixels[1] == (round(transfer_encode(128 / BYTE_MAX) * BYTE_MAX),),
        "gAMA is inverse exponent, not gamma encoding",
    )
    gray, _, gray_png = run(exe, root, fixture)
    profile = profile_from_output(gray_png)
    expect(profile[24:36] == struct.pack(">6H", 2000, 1, 1, 0, 0, 0), "canonical profile date")
    icc = b"fixture\0\0" + zlib.compress(gamma_profile(profile, 2.0))
    output, report, _ = run(exe, root, dataclasses.replace(fixture, metadata=((b"iCCP", icc),)))
    expect(
        abs(output.pixels[1][0] - round(transfer_encode((128 / BYTE_MAX) ** 2) * BYTE_MAX)) <= 1,
        "native gray ICC power-curve reference",
    )
    expect(report["profile_decision"] == "icc", "ICC interpretation reported")
    override, report, _ = run(exe, root, linear, ["--profile-policy", "srgb"])
    expect(
        override.pixels == gray.pixels and "W_PROFILE_OVERRIDDEN" in report["warnings"],
        "explicit sRGB override",
    )
    corrupt = dataclasses.replace(fixture, metadata=((b"iCCP", b"bad\0\0invalid"),))
    rejected(exe, root, corrupt)
    run(exe, root, corrupt, ["--profile-policy", "srgb"])
    cicp = dataclasses.replace(fixture, metadata=((b"cICP", bytes((1, 13, 0, 1))), (b"iCCP", icc)))
    output, report, _ = run(exe, root, cicp)
    expect(
        output.pixels == gray.pixels and report["profile_decision"] == "cicp_srgb",
        "cICP outranks valid ICC",
    )
    rejected(exe, root, dataclasses.replace(fixture, metadata=((b"cICP", bytes((9, 16, 0, 1))),)))
    rejected(
        exe,
        root,
        dataclasses.replace(
            fixture, metadata=((b"sRGB", b"\0"), (b"gAMA", struct.pack(">I", 100000)))
        ),
    )
    with_chroma = dataclasses.replace(
        fixture,
        metadata=((b"gAMA", struct.pack(">I", 100000)), (b"cHRM", struct.pack(">8I", *SRGB))),
    )
    converted, _, _ = run(exe, root, with_chroma)
    expect(
        abs(converted.pixels[1][0] - output.pixels[1][0]) > VISIBLE_GAMMA_DIFFERENCE,
        "native declared gamma/chromaticity transform is not ignored",
    )


def profile_boundaries(exe: Path, root: Path) -> None:
    """Verify serialized gray TRC, RGB profile conversion and strict bounded metadata rejection."""
    gray = Fixture(1, 1, ((128,),))
    _, _, encoded = run(exe, root, gray)
    profile = profile_from_output(encoded)
    tag_count = int.from_bytes(profile[128:132], "big")
    for index in range(tag_count):
        at = 132 + 12 * index
        if profile[at : at + 4] != b"kTRC":
            continue
        offset, size = struct.unpack(">II", profile[at + 4 : at + 12])
        curve = profile[offset : offset + size]
        expect(curve[:4] == b"curv", "gray ICC curveType")
        count = int.from_bytes(curve[8:12], "big")
        expect(count == GRAY_CURVE_KNOTS, "all 65530 serialized gray knots")
        knots = struct.unpack(f">{count}H", curve[12:])
        expect(
            all(
                q == int(transfer_decode(i / (count - 1)) * WORD_MAX + 0.5)
                for i, q in enumerate(knots)
            ),
            "serialized gray curve agrees with independent sRGB definition",
        )
        break
    else:
        msg = "Gray output has no kTRC"
        raise AssertionError(msg)
    colors = Fixture(3, 1, ((64, 128, 192), (0, 255, 31), (128, 128, 128)), RGB)
    _, _, encoded = run(exe, root, colors)
    rgb_profile = gamma_profile(profile_from_output(encoded), 2.0)
    icc = b"fixture\0\0" + zlib.compress(rgb_profile)
    transformed, report, _ = run(exe, root, dataclasses.replace(colors, metadata=((b"iCCP", icc),)))
    expected = tuple(
        tuple(round(transfer_encode((v / BYTE_MAX) ** 2) * BYTE_MAX) for v in p)
        for p in colors.pixels
    )
    expect(
        all(
            abs(a - b) <= 1
            for p, q in zip(transformed.pixels, expected, strict=True)
            for a, b in zip(p, q, strict=True)
        ),
        "native RGB ICC power-curve reference",
    )
    expect(report["profile_decision"] == "icc", "RGB ICC selected")
    rejected(exe, root, dataclasses.replace(gray, metadata=((b"iCCP", icc),)))
    oversized = b"fixture\0\0" + zlib.compress(b"0" * (4 * 1024 * 1024 + 1))
    rejected(exe, root, dataclasses.replace(gray, metadata=((b"iCCP", oversized),)), code=4)
    for declarations in (
        ((b"gAMA", b"\0" * 4),),
        ((b"sRGB", b"\4"),),
        ((b"cHRM", b"\0" * 32),),
        ((b"gAMA", struct.pack(">I", 100000)), (b"gAMA", struct.pack(">I", 100000))),
        ((b"eXIf", b""),),
    ):
        rejected(exe, root, dataclasses.replace(gray, metadata=declarations))


def orientation_and_metadata(exe: Path, root: Path) -> None:
    """Check every exact orientation, axis swap, metadata stripping and animation rejection."""
    pixels = ((10,), (30,), (60,), (90,), (150,), (230,))
    expected = (
        (10, 30, 60, 90, 150, 230),
        (60, 30, 10, 230, 150, 90),
        (230, 150, 90, 60, 30, 10),
        (90, 150, 230, 10, 30, 60),
        (10, 90, 30, 150, 60, 230),
        (90, 10, 150, 30, 230, 60),
        (230, 60, 150, 30, 90, 10),
        (60, 230, 30, 150, 10, 90),
    )
    for orientation, samples in enumerate(expected, 1):
        fixture = Fixture(
            3,
            2,
            pixels,
            metadata=(
                (b"eXIf", exif(orientation)),
                (b"pHYs", struct.pack(">IIB", 1000, 2000, 1)),
                (b"tEXt", b"Private\0not retained"),
            ),
        )
        output, report, encoded = run(exe, root, fixture)
        expect(tuple(p[0] for p in output.pixels) == samples, "asymmetric orientation permutation")
        swapped = orientation >= TRANSPOSED_ORIENTATION
        expect(
            (output.width, output.height) == ((2, 3) if swapped else (3, 2)), "oriented dimensions"
        )
        resolution = (2000, 1000) if swapped else (1000, 2000)
        expect(
            struct.unpack(">IIB", chunks(encoded)[b"pHYs"][0]) == (*resolution, 1),
            "physical axes swapped",
        )
        expect(report["source_orientation"] == orientation, "source orientation reported")
    fixture = Fixture(1, 1, ((1,),))
    for metadata in (
        ((b"eXIf", exif(0)),),
        ((b"eXIf", b"short"),),
        ((b"acTL", struct.pack(">II", 1, 0)),),
    ):
        rejected(exe, root, dataclasses.replace(fixture, metadata=metadata))


def admission(exe: Path, root: Path) -> None:
    """Every new option is validated, including presence and wrong-operation conflicts."""
    fixture = Fixture(1, 1, ((128,),))
    options = (
        ["--output-mode", ""],
        ["--output-mode", "invalid"],
        ["--bit-depth", "1"],
        ["--bit-depth", ""],
        ["--alpha", ""],
        ["--profile-policy", ""],
        ["--binarize", "sauvola"],
        ["--fixed-threshold", "0.5"],
        ["--output-mode", "bw", "--alpha", "white"],
        ["--output-mode", "bw", "--bit-depth", "auto"],
        ["--output-mode", "bw", "--profile-policy", "embedded"],
    )
    for arguments in options:
        rejected(exe, root, fixture, arguments, 2)


def main() -> int:
    """Run the complete scoped PNG representation matrix against a real executable."""
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for test in (
            precision,
            alpha_and_gray,
            profiles,
            profile_boundaries,
            orientation_and_metadata,
            admission,
        ):
            test(binary, root)
            print(f"PASS: {test.__name__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
