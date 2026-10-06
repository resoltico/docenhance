#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Generate TIFF fixtures from independent literal samples and first-party JPEG bits."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "cli"))
from tiff_fixtures import TiffFixture, container  # noqa: E402

BYTE_DEPTH = 8
RGBA_CHANNELS = 4
GRAY_ALPHA_CHANNELS = 2
LSB_FILL_ORDER = 2
EXCESSIVE_SCAN_APPROXIMATION = 2
PACKBITS = 32773
YCBCR = 6


def samples(depth: int, channels: int) -> tuple[tuple[int, ...], ...]:
    """Formula is also stated in README; neither native codec supplies expected samples."""
    return tuple(
        tuple(
            (x * 19 + y * 37 + c * 53) % 256
            if depth == BYTE_DEPTH
            else (x * 1493 + y * 2039 + c * 8191 + 1) % 65536
            for c in range(channels)
        )
        for y in range(17)
        for x in range(19)
    )


def jpeg_dimensions(encoded: bytes) -> tuple[int, int]:
    """Read fixture SOF dimensions only, without decoding samples."""
    for i in range(len(encoded) - 9):
        if encoded[i : i + 2] in (b"\xff\xc0", b"\xff\xc2"):
            return struct.unpack(">H", encoded[i + 7 : i + 9])[0], struct.unpack(
                ">H", encoded[i + 5 : i + 7]
            )[0]
    message = "missing SOF"
    raise ValueError(message)


def fixtures() -> dict[str, bytes]:
    """Both endian orders, precision, layout, compression and partial striles."""
    result = {}
    for depth in (8, 16):
        for channels in (1, 2, 3, 4):
            for tiled in (False, True):
                for planar in (1, 2):
                    for big in (False, True):
                        for little in (False, True):
                            name = (
                                f"d{depth}-c{channels}-t{int(tiled)}-p{planar}"
                                f"-b{int(big)}-l{int(little)}.tif"
                            )
                            fixture = TiffFixture(
                                19,
                                17,
                                samples(depth, channels),
                                depth=depth,
                                photo=1 if channels <= GRAY_ALPHA_CHANNELS else 2,
                                alpha=2 if channels in (2, RGBA_CHANNELS) else 0,
                                tiled=tiled,
                                planar=planar,
                                big=big,
                                little=little,
                            )
                            result[name] = fixture.encoded()
    return result


def compressed_fixtures() -> dict[str, bytes]:
    """Codec fixtures are separate from the orthogonal container/sample matrix."""
    result = {}
    for compression in (5, 8, 32946, 32773):
        for depth in (8, 16):
            for predictor in (1, 2) if compression != PACKBITS else (1,):
                fixture = TiffFixture(
                    19,
                    17,
                    samples(depth, 3),
                    photo=2,
                    depth=depth,
                    compression=compression,
                    predictor=predictor,
                )
                result[f"compression-{compression}-d{depth}-predictor{predictor}.tif"] = (
                    fixture.encoded()
                )
    for compression in (3, 4):
        pixels = tuple((y % 2,) for y in range(4) for _ in range(16))
        fixture = TiffFixture(16, 4, pixels, photo=0, depth=1, compression=compression)
        result[f"fax-{compression}.tif"] = fixture.encoded()
    for name, photo in (
        ("gray-1x1-baseline.jpg", 1),
        ("rgb-1x1-baseline.jpg", 2),
        ("ycbcr-2x2-baseline.jpg", 6),
    ):
        encoded = (ROOT / "fixtures/jpeg" / name).read_bytes()
        width, height = jpeg_dimensions(encoded)
        channels = 1 if photo == 1 else 3
        fixture = TiffFixture(
            width, height, ((0,) * channels,), photo=photo, compression=7, rows=height
        )
        fields = fixture.fields()
        if photo == YCBCR:
            fields[530] = (3, 2, struct.pack("<HH", 2, 2))
        result[f"jpeg-{photo}.tif"] = container(fields, [encoded], big=False, little=True)
    for photo, name in (
        (1, "gray-1x1-progressive.jpg"),
        (2, "rgb-1x1-progressive.jpg"),
        (6, "ycbcr-2x2-progressive.jpg"),
    ):
        encoded = (ROOT / "fixtures/jpeg" / name).read_bytes()
        width, height = jpeg_dimensions(encoded)
        channels = 1 if photo == 1 else 3
        fixture = TiffFixture(
            width, height, ((0,) * channels,), photo=photo, compression=7, rows=height
        )
        fields = fixture.fields()
        if photo == YCBCR:
            fields[530] = (3, 2, struct.pack("<HH", 2, 2))
        result[f"jpeg-{photo}-progressive.tif"] = container(
            fields, [encoded], big=False, little=True
        )
    short = TiffFixture(3, 2, ((0,), (40,), (80,), (120,), (160,), (255,)), compression=8)
    result["deflate-missing-checksum.tif"] = container(
        short.fields(), [short.units()[0][:-4]], big=False, little=True
    )
    return result


def scan_fixtures() -> dict[str, bytes]:
    """Valid zero-AC successive-approximation scans challenge native execution bounds."""
    source = (ROOT / "fixtures/jpeg/gray-1x1-progressive.jpg").read_bytes()
    first = source.index(b"\xff\xda")
    second = source.index(b"\xff\xda", first + 2)
    prefix = source[:second]
    fields = TiffFixture(17, 9, ((0,),), compression=7, rows=9).fields()
    result = {}
    for approximation in (1, 2):
        scans = bytearray(prefix)
        phases = [(0, approximation), *((high, high - 1) for high in range(approximation, 0, -1))]
        for high, low in phases:
            for band in range(1, 64):
                scans += b"\xff\xda" + struct.pack(">H", 8)
                scans += bytes([1, 1, 0, band, band, high * 16 + low])
                # Six logical 8x8 blocks, one canonical three-bit EOB code each.
                scans += b"\0\0\x3f"
        scans += b"\xff\xd9"
        name = (
            "jpeg-many-scans.tif"
            if approximation == EXCESSIVE_SCAN_APPROXIMATION
            else "jpeg-bounded-scans.tif"
        )
        result[name] = container(fields, [bytes(scans)], big=False, little=True)
    return result


def bilevel_fixtures() -> dict[str, bytes]:
    """Packed sample bits and compressed stream fill order are distinct from color meaning."""
    result = {}
    bilevel = tuple(((x * 3 + y) % 2,) for y in range(17) for x in range(19))
    for compression in (1, 5, 8, 32773):
        fixture = TiffFixture(19, 17, bilevel, depth=1, photo=0, compression=compression)
        for fill in (1, 2):
            units = fixture.units()
            if fill == LSB_FILL_ORDER:
                units = [bytes(int(f"{value:08b}"[::-1], 2) for value in unit) for unit in units]
            fields = fixture.fields() | {266: (3, 1, struct.pack("<H", fill))}
            result[f"bilevel-{compression}-fill{fill}.tif"] = container(
                fields, units, big=False, little=True
            )
    return result


if __name__ == "__main__":
    directory = Path(__file__).resolve().parent
    for name, data in (
        fixtures() | compressed_fixtures() | bilevel_fixtures() | scan_fixtures()
    ).items():
        (directory / name).write_bytes(data)
