# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent PNG fixture I/O for the small 8-bit grayscale executable oracles."""

from __future__ import annotations

import struct
import zlib
from typing import TYPE_CHECKING

from continuous_fixtures import GRAY, decode_output
from test_cli import chunk, expect

if TYPE_CHECKING:
    from pathlib import Path


SIGNATURE = b"\x89PNG\r\n\x1a\n"


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


def read_logical_image(path: Path) -> list[bytes]:
    """Read physical PNG1 as logical0/1 rows, rejecting original nonzero unused bits."""
    image = decode_output(path.read_bytes())
    expect(image.color == GRAY and image.depth == 1, "physical grayscale PNG1")
    return [
        bytes(p[0] for p in image.pixels[y * image.width : (y + 1) * image.width])
        for y in range(image.height)
    ]


def read_image(path: Path) -> list[bytes]:
    """Display-byte API for existing byte-domain oracles; PNG1 white is explicitly displayed255."""
    image = decode_output(path.read_bytes())
    expect(image.color == GRAY and image.depth in (1, 8), "grayscale display format")
    scale = 255 if image.depth == 1 else 1
    return [
        bytes(p[0] * scale for p in image.pixels[y * image.width : (y + 1) * image.width])
        for y in range(image.height)
    ]
