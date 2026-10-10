# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent byte fixtures for retained black-box refusal probes."""

from __future__ import annotations

import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"


def chunk(kind: bytes, payload: bytes) -> bytes:
    """Encode one PNG chunk, including its independently computed CRC."""
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload))
    )


def gray_png(width: int = 2, height: int = 1, metadata: bytes = b"") -> bytes:
    """Encode a tiny grayscale source without using the product encoder."""
    header = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    return (
        SIGNATURE
        + header
        + metadata
        + chunk(b"IDAT", zlib.compress(b"\0\x00\xff"))
        + chunk(b"IEND", b"")
    )


def png_refusals() -> dict[str, bytes]:
    """One deliberate structural defect per input, with valid surrounding chunk CRCs."""
    valid = gray_png()
    crc = bytearray(valid)
    crc[29] ^= 1
    header = valid[8:33]
    data = chunk(b"IDAT", zlib.compress(b"\0\x00\xff"))
    ending = chunk(b"IEND", b"")
    return {
        "empty": b"",
        "unsupported-gif": b"GIF89a\x02\0\x01\0\0\0\0",
        "unsupported-pdf": b"%PDF-1.7\n%%EOF\n",
        "signature-only": SIGNATURE,
        "truncated-header": valid[:20],
        "truncated-data": valid[:44],
        "truncated-end": valid[:-1],
        "bad-crc": bytes(crc),
        "trailing-bytes": valid + b"foreign",
        "animated": gray_png(metadata=chunk(b"acTL", struct.pack(">II", 1, 0))),
        "unknown-critical": gray_png(metadata=chunk(b"ABCD", b"")),
        "duplicate-header": SIGNATURE + header + header + data + ending,
        "nonconsecutive-data": (
            SIGNATURE + header + data + chunk(b"tEXt", b"a\0b") + data + ending
        ),
        "invalid-zlib": SIGNATURE + header + chunk(b"IDAT", b"bad deflate") + ending,
        "zero-width": gray_png(0, 1),
        "zero-height": gray_png(2, 0),
        "duplicate-gamma": gray_png(metadata=chunk(b"gAMA", b"\0\0\xb1\x8f") * 2),
        "unsupported-cicp": gray_png(metadata=chunk(b"cICP", bytes((9, 16, 0, 1)))),
    }


def container_refusals() -> dict[str, bytes]:
    """Deliberate JPEG/TIFF framing and coding refusals, identified by bytes."""
    return {
        "jpeg-signature-only": b"\xff\xd8",
        "jpeg-truncated-marker": b"\xff\xd8\xff\xe0\0\x10JFIF",
        "jpeg-missing-frame": b"\xff\xd8\xff\xd9",
        "jpeg-lossless-frame": b"\xff\xd8\xff\xc3\0\x0b\x08\0\x01\0\x01\x01\x01\x11\0\xff\xd9",
        "tiff-signature-only": b"II*\0",
        "tiff-ifd-outside": b"II*\0\xff\xff\xff\x7f",
        "tiff-truncated-ifd": b"II*\0\x08\0\0\0\x01\0",
        "bigtiff-truncated": b"II+\0\x08\0\0\0",
        "bigtiff-invalid-offset-width": b"II+\0\x04\0\0\0" + b"\0" * 8,
    }
