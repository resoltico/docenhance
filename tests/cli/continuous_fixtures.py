# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
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
CHUNK_OVERHEAD = 12
IHDR_BYTES = 13
FILTER_MAX = 4
ICC_NAME_MAX = 79
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
    if not 0 <= kind <= FILTER_MAX:
        msg = "Invalid PNG filter kind"
        raise ValueError(msg)
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


def _read_chunk(data: bytes) -> tuple[bytes, bytes, bytes]:
    """Check the exact declared frame and CRC before chunk-order admission."""
    if len(data) < CHUNK_OVERHEAD:
        message = "Truncated PNG chunk"
        raise ValueError(message)
    size = int.from_bytes(data[:4], "big")
    if size > len(data) - CHUNK_OVERHEAD:
        message = "Output chunk extent mismatch"
        raise ValueError(message)
    name, body = data[4:8], data[8 : 8 + size]
    crc = int.from_bytes(data[8 + size : 12 + size], "big")
    if not name.isalpha() or name[2] & 32 or zlib.crc32(name + body) != crc:
        message = "Output chunk name or CRC mismatch"
        raise ValueError(message)
    return name, body, data[CHUNK_OVERHEAD + size :]


def chunks(data: bytes) -> dict[bytes, list[bytes]]:
    """Read exact PNG framing, CRCs, singleton headers, contiguous IDAT and final IEND."""
    if not data.startswith(SIGNATURE):
        message = "Invalid output signature"
        raise ValueError(message)
    data = data[len(SIGNATURE) :]
    result: dict[bytes, list[bytes]] = {}
    idat_finished = False
    while data:
        name, body, data = _read_chunk(data)
        size = len(body)
        if not result and name != b"IHDR":
            message = "IHDR must be first"
            raise ValueError(message)
        if name == b"IHDR" and (result or size != IHDR_BYTES):
            message = "IHDR must be one complete header"
            raise ValueError(message)
        if name in (b"iCCP", b"pHYs") and name in result:
            message = "Duplicate output metadata"
            raise ValueError(message)
        if name == b"IDAT" and idat_finished:
            message = "Noncontiguous IDAT"
            raise ValueError(message)
        if b"IDAT" in result and name != b"IDAT":
            idat_finished = True
        result.setdefault(name, []).append(body)
        if name == b"IEND":
            if size or data or b"IDAT" not in result:
                message = "Invalid final IEND or absent image data"
                raise ValueError(message)
            return result
    message = "Output has no IEND"
    raise ValueError(message)


def inflate(data: bytes, expected: int | None = None) -> bytes:
    """One complete zlib stream, with no ignored compressed suffix or oversized raster."""
    decoder = zlib.decompressobj()
    result = decoder.decompress(data, 0 if expected is None else expected + 1)
    if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        message = "Incomplete zlib stream or compressed suffix"
        raise ValueError(message)
    if expected is not None and len(result) != expected:
        message = "Unexpected decoded output extent"
        raise ValueError(message)
    return result


def unfilter(stream: bytes, height: int, row_bytes: int, bpp: int) -> list[bytes]:
    """Reconstruct original unfiltered packed rows before any low-bit sample expansion."""
    prior = bytes(row_bytes)
    rows = []
    for y in range(height):
        at = y * (row_bytes + 1)
        kind, filtered = stream[at], stream[at + 1 : at + 1 + row_bytes]
        row = bytearray()
        for index, value in enumerate(filtered):
            left = row[index - bpp] if index >= bpp else 0
            corner = prior[index - bpp] if index >= bpp else 0
            row.append((value + prediction(kind, left, prior[index], corner)) & BYTE_MASK)
        rows.append(bytes(row))
        prior = bytes(row)
    return rows


def decode_output(data: bytes) -> Fixture:
    """Decode opaque PNG8/16 integers or physical PNG1 logical0/1; never display-expand bits."""
    metadata = chunks(data)
    width, height, depth, color, compression, filtering, interlace = struct.unpack(
        ">IIBBBBB", metadata[b"IHDR"][0]
    )
    if any(name not in (b"IHDR", b"IDAT", b"IEND", b"iCCP", b"pHYs") for name in metadata):
        message = "Unexpected canonical output metadata"
        raise ValueError(message)
    if not width or not height or compression or filtering or interlace:
        message = "Unexpected output encoding"
        raise ValueError(message)
    if color not in (GRAY, RGB) or depth not in (1, BYTE_BITS, WORD_BITS):
        message = "Unsupported output precision or color"
        raise ValueError(message)
    if depth == 1 and (color != GRAY or b"iCCP" in metadata):
        message = "PNG1 output must be grayscale without ICC"
        raise ValueError(message)
    bpp = max(1, CHANNELS[color] * depth // BYTE_BITS)
    row_bytes = (width * CHANNELS[color] * depth + 7) // BYTE_BITS
    stream = inflate(b"".join(metadata[b"IDAT"]), height * (row_bytes + 1))
    pixels: list[Pixel] = []
    for row in unfilter(stream, height, row_bytes, bpp):
        if depth == 1:
            tail = (8 - width % 8) % 8
            if tail and row[-1] & ((1 << tail) - 1):
                message = "Nonzero original PNG1 unused row bits"
                raise ValueError(message)
            pixels.extend(((row[x // 8] >> (7 - x % 8)) & 1,) for x in range(width))
            continue
        stride = depth // BYTE_BITS
        samples = [int.from_bytes(row[i : i + stride], "big") for i in range(0, row_bytes, stride)]
        pixels.extend(
            tuple(samples[i : i + CHANNELS[color]]) for i in range(0, len(samples), CHANNELS[color])
        )
    return Fixture(width, height, tuple(pixels), color, depth)


def exif(orientation: int) -> bytes:
    """Create a little-endian IFD0 with only orientation; no copied camera metadata."""
    return b"II\x2a\0\x08\0\0\0" + struct.pack("<HHHIH", 1, 0x112, 3, 1, orientation) + b"\0" * 6


def profile_from_output(data: bytes) -> bytes:
    """Extract and decompress the actual serialized profile for independent inspection."""
    packed = chunks(data)[b"iCCP"][0]
    name_end = packed.index(b"\0")
    if not 1 <= name_end <= ICC_NAME_MAX or packed[name_end + 1 : name_end + 2] != b"\0":
        message = "Invalid ICC PNG compression framing"
        raise ValueError(message)
    return inflate(packed[name_end + 2 :])


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


def decoder_controls() -> None:
    """Independent strict packed-row positives and CRC/zlib/tail rejection controls."""

    def refused(data: bytes) -> None:
        try:
            decode_output(data)
        except (ValueError, zlib.error):
            return
        message = "Malformed independent PNG decoder control was accepted"
        raise AssertionError(message)

    for width in (1, 7, 8, 9, 15, 16, 17):
        pixels: tuple[Pixel, ...] = tuple(((x + y) % 2,) for y in range(5) for x in range(width))
        fixture = Fixture(width, 5, pixels, depth=1)
        if decode_output(fixture.encoded()).pixels != pixels:
            message = "Logical packed sample decoder changed physical bits"
            raise AssertionError(message)
        header = struct.pack(">IIBBBBB", width, 5, 1, GRAY, 0, 0, 0)
        raw = b"".join(
            b"\0" + pack_samples([p[0] for p in pixels[y * width : (y + 1) * width]], 1)
            for y in range(5)
        )

        def framed(payload: bytes, header: bytes = header) -> bytes:
            return (
                SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", payload) + chunk(b"IEND", b"")
            )

        refused(framed(zlib.compress(raw) + b"suffix"))
        corrupt = bytearray(fixture.encoded())
        corrupt[-1] ^= 1
        refused(bytes(corrupt))
        if width % 8:
            altered = bytearray(raw)
            altered[(width + 7) // 8] |= 1
            refused(framed(zlib.compress(altered)))
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        for color, channels in ((GRAY, 1), (RGB, 3)):
            pixels = tuple(
                tuple((i * 37 + c * 13) % maximum for c in range(channels)) for i in range(15)
            )
            fixture = Fixture(3, 5, pixels, color=color, depth=depth)
            if decode_output(fixture.encoded()) != fixture:
                message = "Existing independent8/16-bit decode API changed"
                raise AssertionError(message)
