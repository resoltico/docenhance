# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Deterministic first-party pages with explicitly defined synthetic quality targets."""

from __future__ import annotations

import math
import struct
from functools import cache
from typing import TYPE_CHECKING

from audit_paths import ROOT
from continuous_fixtures import GRAY_ALPHA, INDEXED, RGB, RGBA, Fixture, exif
from tiff_fixtures import TiffFixture

if TYPE_CHECKING:
    from pathlib import Path

WIDTH, HEIGHT = 96, 64
MAX_BYTE, MAX_WORD = 255, 65535
MARK_ROW, MARK_LEFT, MARK_RIGHT = 8, 5, 16
STRESS_MARK_ROW, STRESS_MARK_WIDTH = 16, 24
MARKS = ((8, 8), (20, 8), (32, 8))


def page(kind: str) -> Fixture:
    """Build shaded, noisy, low-contrast and soft-edge pages without a native codec."""
    state = 1729
    pixels = []
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if kind == "shaded":
                value = 150 + x * 90 // (WIDTH - 1)
                if y % 16 == MARK_ROW and MARK_LEFT <= x % 24 <= MARK_RIGHT:
                    value = value // 5
            elif kind == "noisy":
                noise = 0
                for _ in range(6):
                    state = (1664525 * state + 1013904223) % (1 << 32)
                    noise += (state >> 24) % 11
                value = 210 + round((noise - 30) / 2)
                if (x, y) in MARKS:
                    value = 30
            elif kind == "low-contrast":
                value = 110 + (x % 32) * 35 // 31 + (y % 7)
            else:
                value = round(35 + 185 / (1 + math.exp(-(x - 47.5) / 1.5)))
            pixels.append((value,))
    return Fixture(WIDTH, HEIGHT, tuple(pixels))


@cache
def png_sources() -> dict[str, Fixture]:
    """Cover source precision, sample models, Adam7 and meaningful asymmetric geometry."""
    sources = {name: page(name) for name in ("shaded", "noisy", "low-contrast", "soft-edge")}
    sources["flat"] = Fixture(WIDTH, HEIGHT, ((180,),) * (WIDTH * HEIGHT))
    sources["black"] = Fixture(8, 4, ((0,),) * 32)
    sources["gray8"] = Fixture(16, 16, tuple((i,) for i in range(256)))
    sources["gray16"] = Fixture(256, 256, tuple((i,) for i in range(MAX_WORD + 1)), depth=16)
    for depth in (1, 2, 4):
        sources[f"gray{depth}"] = Fixture(
            16, 4, tuple((i % (1 << depth),) for i in range(64)), depth=depth
        )
    for depth in (8, 16):
        scale = 1 if depth == MAX_BYTE.bit_length() else 257
        pixels = tuple((i * 13 % 256 * scale, 125 * scale, 230 * scale) for i in range(64))
        sources[f"rgb{depth}"] = Fixture(16, 4, pixels, color=RGB, depth=depth)
        alpha = tuple((*pixel, (0, 128, 255)[i % 3] * scale) for i, pixel in enumerate(pixels))
        sources[f"rgba{depth}"] = Fixture(16, 4, alpha, color=RGBA, depth=depth)
        sources[f"gray-alpha{depth}"] = Fixture(
            16, 4, tuple((p[0], p[3]) for p in alpha), color=GRAY_ALPHA, depth=depth
        )
        for orientation in range(1, 9):
            sources[f"geometry-{depth}-{orientation}"] = Fixture(
                3,
                2,
                tuple((i * 35 * scale,) for i in range(6)),
                depth=depth,
                metadata=(
                    (b"eXIf", exif(orientation)),
                    (b"pHYs", struct.pack(">IIB", 1000, 2000, 1)),
                ),
            )
    sources["adam7"] = Fixture(17, 9, tuple((i * 17 % 256,) for i in range(153)), interlaced=True)
    sources["palette"] = Fixture(
        16,
        4,
        tuple((i % 4,) for i in range(64)),
        color=INDEXED,
        depth=2,
        metadata=((b"PLTE", bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255))),),
    )
    sources["mask"] = Fixture(
        WIDTH, HEIGHT, tuple((255 if i % 5 == 0 else 0,) for i in range(WIDTH * HEIGHT))
    )
    sources["mask-all"] = Fixture(WIDTH, HEIGHT, ((1,),) * (WIDTH * HEIGHT), depth=1)
    sources["psf"] = Fixture(3, 3, ((0,),) * 4 + ((255,),) + ((0,),) * 4)
    return sources


@cache
def tiff_sources() -> dict[str, TiffFixture]:
    """Select every admitted lossless compression plus endian, BigTIFF and layout boundaries."""
    sources = {}
    for compression in (1, 5, 8, 32946, 32773):
        for depth in (8, 16):
            scale = 1 if depth == MAX_BYTE.bit_length() else 257
            name = f"tiff-c{compression}-d{depth}"
            sources[name] = TiffFixture(
                17,
                9,
                tuple(((i * 7 % 256) * scale,) for i in range(153)),
                depth=depth,
                compression=compression,
            )
    pixels = tuple((i * 13 % 256, 123, 231) for i in range(153))
    for big in (False, True):
        for little in (False, True):
            for planar in (1, 2):
                name = f"tiff-b{int(big)}-l{int(little)}-p{planar}"
                sources[name] = TiffFixture(
                    17, 9, pixels, photo=2, big=big, little=little, planar=planar, tiled=True
                )
    for compression in (3, 4):
        sources[f"tiff-fax{compression}"] = TiffFixture(
            16, 4, ((0,),) * 32 + ((1,),) * 32, depth=1, compression=compression
        )
    return sources


def write_sources(directory: Path) -> dict[str, Path]:
    """Retain all fixture bytes, including existing independently authored JPEG/TIFF codestreams."""
    directory.mkdir()
    paths = {}
    for name, fixture in png_sources().items():
        paths[name] = directory / f"{name}.png"
        paths[name].write_bytes(fixture.encoded())
    paths["stress"] = directory / "stress.png"
    stress = Fixture(
        1536,
        1024,
        tuple(
            (
                45
                if y % 32 == STRESS_MARK_ROW and x % 48 < STRESS_MARK_WIDTH
                else 150 + x * 90 // 1535,
            )
            for y in range(1024)
            for x in range(1536)
        ),
    )
    paths["stress"].write_bytes(stress.encoded())
    for name, tiff in tiff_sources().items():
        paths[name] = directory / f"{name}.tif"
        paths[name].write_bytes(tiff.encoded())
    for name in (
        "gray-document-baseline.jpeg",
        "gray-document-progressive.jpeg",
        "chroma-patch-baseline.jpeg",
        "chroma-patch-progressive.jpeg",
    ):
        key = "jpeg-" + name.removesuffix(".jpeg")
        paths[key] = directory / name
        paths[key].write_bytes((ROOT / "tests/fixtures/jpeg" / name).read_bytes())
    for name in ("jpeg-1.tif", "jpeg-1-progressive.tif"):
        source = ROOT / "tests/fixtures/tiff" / name
        if source.is_file():
            paths[name] = directory / name
            paths[name].write_bytes(source.read_bytes())
    return paths
