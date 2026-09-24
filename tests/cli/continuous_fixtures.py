# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Independent PNG framing, sample, filter and orientation fixtures; no imaging library."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SIGNATURE = b"\x89PNG\r\n\x1a\n"
GRAY = 0
RGB = 2
INDEXED = 3
GRAY_ALPHA = 4
RGBA = 6
BYTE_BITS = 8
WORD_BITS = 16
BYTE_MASK = 255
CHANNELS = {GRAY: 1, RGB: 3, INDEXED: 1, GRAY_ALPHA: 2, RGBA: 4}
ADAM7 = (
    (0, 0, 8, 8),
    (4, 0, 8, 8),
    (0, 4, 4, 8),
    (2, 0, 4, 4),
    (0, 2, 2, 4),
    (1, 0, 2, 2),
    (0, 1, 1, 2),
)
PIXEL_PASSES = ((0, 0, 1, 1),)
Pixel = tuple[int, ...]


def chunk(name: bytes, data: bytes) -> bytes:
    """Serialize one checked-length PNG chunk with independent CRC."""
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))


def paeth(left: int, above: int, corner: int) -> int:
    """Select PNG's exact Paeth neighbor, including tie order."""
    predictor = left + above - corner
    distances = (abs(predictor - left), abs(predictor - above), abs(predictor - corner))
    return (left, above, corner)[distances.index(min(distances))]


def prediction(kind: int, left: int, above: int, corner: int) -> int:
    """Return one of all five lossless PNG row filter predictors."""
    predictors = (0, left, above, (left + above) // 2, paeth(left, above, corner))
    return predictors[kind]


def pack_samples(samples: list[int], depth: int) -> bytes:
    """Serialize integer samples in PNG bit/byte order with zero padding."""
    if depth == WORD_BITS:
        return b"".join(struct.pack(">H", value) for value in samples)
    if depth == BYTE_BITS:
        return bytes(samples)
    result = bytearray()
    accumulator = 0
    bits = 0
    for value in samples:
        accumulator = (accumulator << depth) | value
        bits += depth
        if bits == BYTE_BITS:
            result.append(accumulator)
            accumulator = bits = 0
    if bits:
        result.append(accumulator << (BYTE_BITS - bits))
    return bytes(result)


@dataclass(frozen=True)
class Fixture:
    """A complete first-party raster, before compression and color interpretation."""

    width: int
    height: int
    pixels: tuple[Pixel, ...]
    color: int = GRAY
    depth: int = BYTE_BITS
    interlaced: bool = False
    metadata: tuple[tuple[bytes, bytes], ...] = ()

    def encoded(self) -> bytes:
        """Encode samples through all PNG filters, optionally in Adam7 passes."""
        header = struct.pack(
            ">IIBBBBB", self.width, self.height, self.depth, self.color, 0, 0, self.interlaced
        )
        return (
            SIGNATURE
            + chunk(b"IHDR", header)
            + b"".join(chunk(name, data) for name, data in self.metadata)
            + chunk(b"IDAT", zlib.compress(self.scanlines()))
            + chunk(b"IEND", b"")
        )

    def scanlines(self) -> bytes:
        """Generate filtered rows independently of libpng, with pass-local row history."""
        result = bytearray()
        bpp = max(1, CHANNELS[self.color] * self.depth // BYTE_BITS)
        passes = ADAM7 if self.interlaced else PIXEL_PASSES
        for x0, y0, dx, dy in passes:
            if x0 >= self.width or y0 >= self.height:
                continue
            prior = b""
            for y in range(y0, self.height, dy):
                samples = [
                    c for x in range(x0, self.width, dx) for c in self.pixels[y * self.width + x]
                ]
                row = pack_samples(samples, self.depth)
                kind = y % 5
                result.append(kind)
                for index, value in enumerate(row):
                    left = row[index - bpp] if index >= bpp else 0
                    above = prior[index] if prior else 0
                    corner = prior[index - bpp] if prior and index >= bpp else 0
                    result.append((value - prediction(kind, left, above, corner)) & BYTE_MASK)
                prior = row
        return bytes(result)


def chunks(data: bytes) -> dict[bytes, list[bytes]]:
    """Read output chunks, rejecting truncation, CRC errors and trailing bytes."""
    if not data.startswith(SIGNATURE):
        msg = "Invalid output signature"
        raise ValueError(msg)
    data = data[len(SIGNATURE) :]
    result: dict[bytes, list[bytes]] = {}
    while data:
        size = int.from_bytes(data[:4], "big")
        name, body = data[4:8], data[8 : 8 + size]
        crc = int.from_bytes(data[8 + size : 12 + size], "big")
        if len(body) != size or zlib.crc32(name + body) != crc:
            msg = "Output chunk size or CRC mismatch"
            raise ValueError(msg)
        result.setdefault(name, []).append(body)
        data = data[12 + size :]
        if name == b"IEND":
            if data:
                msg = "Output has trailing bytes"
                raise ValueError(msg)
            return result
    msg = "Output has no IEND"
    raise ValueError(msg)


def decode_output(data: bytes) -> Fixture:
    """Decode raw noninterlaced output integers without using the production codec."""
    metadata = chunks(data)
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", metadata[b"IHDR"][0]
    )
    if compression or filtering or interlace or depth not in (BYTE_BITS, WORD_BITS):
        msg = "Unexpected output encoding"
        raise ValueError(msg)
    bpp = CHANNELS[color] * depth // BYTE_BITS
    row_bytes = width * bpp
    stream = zlib.decompress(b"".join(metadata[b"IDAT"]))
    if len(stream) != height * (row_bytes + 1):
        msg = "Unexpected decoded output extent"
        raise ValueError(msg)
    prior = bytes(row_bytes)
    pixels: list[Pixel] = []
    for y in range(height):
        at = y * (row_bytes + 1)
        kind, filtered = stream[at], stream[at + 1 : at + 1 + row_bytes]
        row = bytearray()
        for index, value in enumerate(filtered):
            left = row[index - bpp] if index >= bpp else 0
            corner = prior[index - bpp] if index >= bpp else 0
            row.append((value + prediction(kind, left, prior[index], corner)) & BYTE_MASK)
        stride = depth // BYTE_BITS
        samples = [int.from_bytes(row[i : i + stride], "big") for i in range(0, row_bytes, stride)]
        pixels.extend(
            tuple(samples[i : i + CHANNELS[color]]) for i in range(0, len(samples), CHANNELS[color])
        )
        prior = bytes(row)
    return Fixture(width, height, tuple(pixels), color, depth)


def exif(orientation: int) -> bytes:
    """Create a little-endian IFD0 with only orientation; no copied camera metadata."""
    return b"II\x2a\0\x08\0\0\0" + struct.pack("<HHHIH", 1, 0x112, 3, 1, orientation) + b"\0" * 6


def profile_from_output(data: bytes) -> bytes:
    """Extract and decompress the actual serialized profile for independent inspection."""
    packed = chunks(data)[b"iCCP"][0]
    return zlib.decompress(packed[packed.index(b"\0") + 2 :])


def gamma_profile(profile: bytes, gamma: float) -> bytes:
    """Replace RGB/gray TRCs with a known parametric power curve in a generated ICC fixture."""
    result = bytearray(profile)
    count = int.from_bytes(profile[128:132], "big")
    for i in range(count):
        at = 132 + 12 * i
        tag = profile[at : at + 4]
        offset, size = struct.unpack(">II", profile[at + 4 : at + 12])
        if tag not in (b"rTRC", b"gTRC", b"bTRC", b"kTRC"):
            continue
        curve = b"para" + b"\0" * 8 + struct.pack(">i", round(gamma * 65536))
        if size < len(curve):
            msg = "ICC fixture TRC has insufficient space"
            raise ValueError(msg)
        result[offset : offset + len(curve)] = curve
        result[at + 8 : at + 12] = struct.pack(">I", len(curve))
    result[84:100] = b"\0" * 16
    return bytes(result)
