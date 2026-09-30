# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Independent, small JPEG writer: explicit DCT coefficients and canonical Huffman codes.

No native encoder is used. Fixtures test decoded coefficients, not a lossy round trip.
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from pathlib import Path

BYTE_BITS = 8
BYTE_MAX = 255
CHROMA_MCU_BLOCK_MAX = 8


def marker(code: int, payload: bytes) -> bytes:
    """Serialize a length-delimited JPEG marker."""
    return bytes([255, code]) + struct.pack(">H", len(payload) + 2) + payload


class Bits:
    """Emit entropy bytes with mandated zero stuffing after FF."""

    def __init__(self) -> None:
        """Start an empty entropy segment."""
        self.output = bytearray()
        self.value = 0
        self.count = 0

    def put(self, value: int, count: int) -> None:
        """Append most-significant-bit-first code bits."""
        self.value = (self.value << count) | value
        self.count += count
        while self.count >= BYTE_BITS:
            self.count -= 8
            byte = (self.value >> self.count) & 255
            self.output.append(byte)
            if byte == BYTE_MAX:
                self.output.append(0)
        self.value &= (1 << self.count) - 1

    def finish(self) -> bytes:
        """Pad the entropy segment with one bits."""
        if self.count:
            self.put((1 << (8 - self.count)) - 1, 8 - self.count)
        return bytes(self.output)


def amplitude(bits: Bits, value: int) -> int:
    """DC category and amplitude follow JPEG's signed-magnitude complement convention."""
    size = abs(value).bit_length()
    bits.put(size, 4)
    if size:
        bits.put(value if value >= 0 else (1 << size) - 1 + value, size)
    return size


def ac_coefficients(bits: Bits, *, varied: bool) -> None:
    """Two nonzero low-frequency coefficients; all remaining AC coefficients are zero."""
    if varied:
        bits.put(1, 3)  # Huffman symbol 04: coefficient +8, category 4, run 0.
        bits.put(8, 4)
        bits.put(2, 3)  # Huffman symbol 03: coefficient -5, category 3, run 0.
        bits.put(2, 3)
    bits.put(0, 3)  # End of block.


def level(component: int, x: int, y: int, color: str) -> int:
    """Known integer DC levels are independent of compression approximations."""
    if color == "gray":
        return 48 + ((x * 29 + y * 43) % 160)
    if color == "rgb":
        return (180, 80, 30)[component]
    if color == "ycbcr-gradient":
        values = (160, 96 + ((x * 40 + y * 10) % 100), 180 - ((x * 30 + y * 20) % 100))
        return values[component]
    return (160, 96, 180)[component]


@dataclass(frozen=True)
class JpegFixture:
    """A complete independently specified coefficient image and scan organization."""

    width: int
    height: int
    color: str = "gray"
    sampling: tuple[int, int] = (1, 1)
    progressive: bool = False
    varied_ac: bool = False

    def factors(self) -> list[tuple[int, int]]:
        """Component sampling factors describe the admitted MCU layout."""
        components = 1 if self.color == "gray" else 3
        return [self.sampling if self.color.startswith("ycbcr") else (1, 1)] + [(1, 1)] * (
            components - 1
        )

    def dc_entropy(self) -> bytes:
        """Encode interleaved DC differences and, for baseline, each block's AC values."""
        factors = self.factors()
        max_h = max(h for h, _ in factors)
        max_v = max(v for _, v in factors)
        bits = Bits()
        previous = [0] * len(factors)
        for mcu_y in range(math.ceil(self.height / (8 * max_v))):
            for mcu_x in range(math.ceil(self.width / (8 * max_h))):
                for c, (h, v) in enumerate(factors):
                    for local_y in range(v):
                        for local_x in range(h):
                            dc = 8 * (
                                level(c, mcu_x * h + local_x, mcu_y * v + local_y, self.color) - 128
                            )
                            amplitude(bits, dc - previous[c])
                            previous[c] = dc
                            if not self.progressive:
                                ac_coefficients(bits, varied=self.varied_ac)
        return bits.finish()

    def ac_entropy(self, identities: list[int]) -> bytes:
        """Progressive AC scans are noninterleaved and omit dummy edge blocks."""
        output = bytearray()
        factors = self.factors()
        max_h = max(h for h, _ in factors)
        max_v = max(v for _, v in factors)
        for c, (h, v) in enumerate(factors):
            output += marker(218, bytes([1, identities[c], 0, 1, 63, 0]))
            bits = Bits()
            count = math.ceil(self.width * h / (max_h * 8)) * math.ceil(
                self.height * v / (max_v * 8)
            )
            for _ in range(count):
                ac_coefficients(bits, varied=self.varied_ac)
            output += bits.finish()
        return bytes(output)

    def encoded(self) -> bytes:
        """Serialize framing, fixed tables and all entropy scans without a native encoder."""
        factors = self.factors()
        identities = (
            [1] if self.color == "gray" else [82, 71, 66] if self.color == "rgb" else [1, 2, 3]
        )
        output = bytearray(b"\xff\xd8")
        if self.color == "rgb":
            output += marker(238, b"Adobe" + struct.pack(">HHHB", 100, 0, 0, 0))
        else:
            output += marker(224, b"JFIF\0\x01\x02\0\0\x01\0\x01\0\0")
        output += marker(219, b"\0" + bytes([1]) * 64)
        frame = struct.pack(">BHHB", 8, self.height, self.width, len(factors))
        for identity, (h, v) in zip(identities, factors, strict=True):
            frame += bytes([identity, (h << 4) | v, 0])
        output += marker(194 if self.progressive else 192, frame)
        dc_counts = bytes([0, 0, 0, 12] + [0] * 12)
        ac_counts = bytes([0, 0, 3] + [0] * 13)
        output += marker(
            196, b"\0" + dc_counts + bytes(range(12)) + b"\x10" + ac_counts + bytes([0, 4, 3])
        )
        output += marker(
            218,
            bytes([len(factors)])
            + b"".join(bytes([i, 0]) for i in identities)
            + bytes([0, 0 if self.progressive else 63, 0]),
        )
        output += self.dc_entropy()
        if self.progressive:
            output += self.ac_entropy(identities)
        output += b"\xff\xd9"
        return bytes(output)


def main() -> None:
    """Write deterministic positive samples; negative samples are mutations in tests."""
    root = Path(__file__).resolve().parent
    for progressive in (False, True):
        process = "progressive" if progressive else "baseline"
        (root / f"gray-document-{process}.jpeg").write_bytes(
            JpegFixture(128, 96, progressive=progressive).encoded()
        )
        (root / f"chroma-patch-{process}.jpeg").write_bytes(
            JpegFixture(
                33, 33, color="ycbcr-gradient", sampling=(2, 2), progressive=progressive
            ).encoded()
        )
        layouts = [
            ("gray", (1, 1)),
            ("rgb", (1, 1)),
            ("ycbcr-gradient", (2, 1)),
            ("ycbcr-gradient", (2, 2)),
            ("ycbcr-gradient", (4, 1)),
        ]
        layouts += [
            ("ycbcr", (h, v))
            for h in range(1, 5)
            for v in range(1, 5)
            if h * v <= CHROMA_MCU_BLOCK_MAX
        ]
        for color, sampling in layouts:
            name = f"{color}-{sampling[0]}x{sampling[1]}-{process}.jpg"
            (root / name).write_bytes(
                JpegFixture(
                    17, 9, color=color, sampling=sampling, progressive=progressive
                ).encoded()
            )
        (root / f"gray-ac-{process}.jpg").write_bytes(
            JpegFixture(17, 9, progressive=progressive, varied_ac=True).encoded()
        )


if __name__ == "__main__":
    main()
