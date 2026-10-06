# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent TIFF/BigTIFF byte framing and lossless encoders; no native image library."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

Field = tuple[int, int, bytes]
TILE_WIDTH_TAG = 322
PACKBITS = 32773
LZW = 5
FAX_WIDTH = 16
GROUP3 = 3
SEPARATE = 2
BYTE_DEPTH = 8


def packed_bits(bits: str) -> bytes:
    """Pack MSB-first codec bits, padding the final byte only."""
    padded = bits + "0" * (-len(bits) % 8)
    return bytes(int(padded[i : i + 8], 2) for i in range(0, len(padded), 8))


def compress(raw: bytes, compression: int, width: int, height: int) -> bytes:
    """Encode literal LZW/PackBits, Deflate, or simple independent CCITT test rows."""
    if compression == 1:
        return raw
    if compression == PACKBITS:
        return b"".join(
            bytes([len(part) - 1]) + part
            for i in range(0, len(raw), 128)
            if (part := raw[i : i + 128])
        )
    if compression in (8, 32946):
        return zlib.compress(raw)
    if compression == LZW:
        # CLEAR every 200 literal codes prevents a width transition. No shared native encoder.
        codes = []
        for i in range(0, len(raw), 200):
            codes += [256, *raw[i : i + 200]]
        return packed_bits("".join(f"{code:09b}" for code in [*codes, 257]))
    if compression in (3, 4):
        if width != FAX_WIDTH:
            raise ValueError(width)
        rows = [raw[i * 2 : i * 2 + 2] for i in range(height)]
        if not all(row in (b"\0\0", b"\xff\xff") for row in rows):
            raise ValueError(raw)
        runs = ["101010" if row == b"\0\0" else "00110101" + "0000010111" for row in rows]
        if compression == GROUP3:
            return packed_bits("".join("000000000001" + run for run in runs))
        # Horizontal mode: white16/black0 or white0/black16, independently each row.
        runs = [
            run + "0000110111" if row == b"\0\0" else run
            for row, run in zip(rows, runs, strict=True)
        ]
        return packed_bits("".join("001" + run for run in runs) + "000000000001000000000001")
    raise ValueError(compression)


def container(
    fields: dict[int, Field],
    units: list[bytes],
    *,
    big: bool,
    little: bool,
    next_ifd: int = 0,
) -> bytes:
    """Build one sorted directory, external values and striles from explicit source data."""
    endian = "<" if little else ">"

    def word(value: int) -> bytes:
        return struct.pack(endian + "H", value)

    def long(value: int) -> bytes:
        return struct.pack(endian + "I", value)

    def offset_pack(value: int) -> bytes:
        return struct.pack(endian + ("Q" if big else "I"), value)

    offset_size = 8 if big else 4
    offset_type = 16 if big else 4
    tiled = TILE_WIDTH_TAG in fields
    tags = dict(fields)
    tags[325 if tiled else 279] = (
        offset_type,
        len(units),
        b"".join(offset_pack(len(u)) for u in units),
    )
    tags[324 if tiled else 273] = (offset_type, len(units), bytes(offset_size * len(units)))
    header_size = 16 if big else 8
    count_size, entry_size = (8, 20) if big else (2, 12)
    alignment = 8 if big else 2
    directory_end = header_size + count_size + len(tags) * entry_size + offset_size
    data_start = (directory_end + alignment - 1) // alignment * alignment
    extra_extent = sum(
        (len(value) + alignment - 1) // alignment * alignment
        for _, _, value in tags.values()
        if len(value) > offset_size
    )
    position = data_start + extra_extent
    offsets = []
    payload = bytearray()
    for unit in units:
        padding = -position % alignment
        payload += bytes(padding)
        position += padding
        offsets.append(position)
        payload += unit
        position += len(unit)
    tags[324 if tiled else 273] = (
        offset_type,
        len(units),
        b"".join(offset_pack(o) for o in offsets),
    )
    extra, directory = bytearray(), bytearray()
    for tag, (kind, count, value) in sorted(tags.items()):
        directory += word(tag) + word(kind) + (offset_pack(count) if big else long(count))
        if len(value) <= offset_size:
            directory += value.ljust(offset_size, b"\0")
        else:
            directory += offset_pack(data_start + len(extra))
            extra += value + bytes(-len(value) % alignment)
    header = (b"II" if little else b"MM") + word(43 if big else 42)
    header += word(8) + word(0) + offset_pack(header_size) if big else offset_pack(header_size)
    return bytes(
        header
        + (offset_pack(len(tags)) if big else word(len(tags)))
        + directory
        + offset_pack(next_ifd)
        + bytes(data_start - directory_end)
        + extra
        + payload
    )


@dataclass(frozen=True)
class TiffFixture:
    """Explicit stored samples and source declarations before interpretation."""

    width: int
    height: int
    pixels: tuple[tuple[int, ...], ...]
    depth: int = 8
    photo: int = 1
    alpha: int = 0
    compression: int = 1
    planar: int = 1
    predictor: int = 1
    tiled: bool = False
    big: bool = False
    little: bool = True
    orientation: int = 1
    rows: int = 2

    def fields(self) -> dict[int, Field]:
        """Declare only actual sample/layout facts; caller may add malformed negative controls."""
        endian = "<" if self.little else ">"

        def short(value: int) -> Field:
            return (3, 1, struct.pack(endian + "H", value))

        def long(value: int) -> Field:
            return (4, 1, struct.pack(endian + "I", value))

        channels = len(self.pixels[0])
        fields = {
            256: long(self.width),
            257: long(self.height),
            258: short(self.depth),
            259: short(self.compression),
            262: short(self.photo),
            274: short(self.orientation),
            277: short(channels),
            284: short(self.planar),
        }
        if self.tiled:
            fields.update({322: long(16), 323: long(16)})
        else:
            fields[278] = long(self.rows)
        if self.alpha:
            fields[338] = short(self.alpha)
        if self.predictor != 1:
            fields[317] = short(self.predictor)
        return fields

    def units(self) -> list[bytes]:
        """Serialize partial final strips and full edge tiles with separate-plane ordering."""
        endian = "<" if self.little else ">"
        channels = len(self.pixels[0])
        planes = channels if self.planar == SEPARATE else 1
        width, height = (16, 16) if self.tiled else (self.width, self.rows)
        units = []
        for plane in range(planes):
            for top in range(0, self.height, height):
                for left in range(0, self.width, width):
                    raw = bytearray()
                    rows = height if self.tiled else min(height, self.height - top)
                    for y in range(rows):
                        samples: list[int] = []
                        for x in range(width):
                            pixel = (
                                self.pixels[(top + y) * self.width + left + x]
                                if top + y < self.height and left + x < self.width
                                else (0,) * channels
                            )
                            samples.extend((pixel[plane],) if self.planar == SEPARATE else pixel)
                        if self.predictor == SEPARATE:
                            stride = 1 if self.planar == SEPARATE else channels
                            original = samples[:]
                            samples[stride:] = [
                                (original[i] - original[i - stride]) % (1 << self.depth)
                                for i in range(stride, len(samples))
                            ]
                        if self.depth == 1:
                            raw += packed_bits("".join(str(s) for s in samples))
                        elif self.depth == BYTE_DEPTH:
                            raw += bytes(samples)
                        else:
                            raw += b"".join(struct.pack(endian + "H", s) for s in samples)
                    units.append(compress(bytes(raw), self.compression, width, rows))
        return units

    def encoded(self, extras: dict[int, Field] | None = None, next_ifd: int = 0) -> bytes:
        """Build a complete independent container, optionally with explicit rejection fields."""
        return container(
            self.fields() | (extras or {}),
            self.units(),
            big=self.big,
            little=self.little,
            next_ifd=next_ifd,
        )
