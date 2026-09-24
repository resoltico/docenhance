# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Independent PNG fixture I/O for the small 8-bit grayscale executable oracles."""

from __future__ import annotations

import struct
import zlib
from typing import TYPE_CHECKING

from test_cli import chunk, expect

if TYPE_CHECKING:
    from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"
CHUNK_OVERHEAD = 12
HEADER_BYTES = 8
FILTER_NONE, FILTER_SUB, FILTER_UP, FILTER_AVERAGE, FILTER_PAETH = range(5)
BYTE_MODULUS = 256


def write_image(path: Path, rows: list[bytes]) -> None:
    """Encode fixture samples without relying on the production codec."""
    expect(rows and rows[0] and all(len(row) == len(rows[0]) for row in rows), "rectangle")
    header = struct.pack(">IIBBBBB", len(rows[0]), len(rows), 8, 0, 0, 0, 0)
    scanlines = b"".join(b"\0" + row for row in rows)
    path.write_bytes(
        SIGNATURE
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(scanlines))
        + chunk(b"IEND", b"")
    )


def predict(kind: int, left: int, above: int, corner: int) -> int:
    """PNG row predictors, including Paeth's specified tie order."""
    if kind == FILTER_NONE:
        return 0
    if kind == FILTER_SUB:
        return left
    if kind == FILTER_UP:
        return above
    if kind == FILTER_AVERAGE:
        return (left + above) // 2
    expect(kind == FILTER_PAETH, "unsupported output filter")
    p = left + above - corner
    return min((left, above, corner), key=lambda value: abs(p - value))


def read_image(path: Path) -> list[bytes]:
    """Decode produced rows independently; reject CRC or layout errors in the output."""
    encoded = path.read_bytes()
    expect(encoded.startswith(SIGNATURE), "output signature")
    offset, width, height = HEADER_BYTES, 0, 0
    parts = []
    while offset < len(encoded):
        size = struct.unpack(">I", encoded[offset : offset + 4])[0]
        kind = encoded[offset + 4 : offset + HEADER_BYTES]
        payload = encoded[offset + HEADER_BYTES : offset + HEADER_BYTES + size]
        expect(chunk(kind, payload) == encoded[offset : offset + size + CHUNK_OVERHEAD], "CRC")
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            expect((depth, color, compression, filtering, interlace) == (8, 0, 0, 0, 0), "format")
        elif kind == b"IDAT":
            parts.append(payload)
        offset += size + CHUNK_OVERHEAD
    decoded = zlib.decompress(b"".join(parts))
    expect(len(decoded) == height * (width + 1), "scanline extent")
    rows: list[bytes] = []
    previous = bytes(width)
    for y in range(height):
        start = y * (width + 1)
        filter_kind = decoded[start]
        current = bytearray(decoded[start + 1 : start + width + 1])
        for x, value in enumerate(current):
            prediction = predict(
                filter_kind, current[x - 1] if x else 0, previous[x], previous[x - 1] if x else 0
            )
            current[x] = (value + prediction) % BYTE_MODULUS
        rows.append(bytes(current))
        previous = bytes(current)
    return rows
