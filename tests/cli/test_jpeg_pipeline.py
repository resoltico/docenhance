#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""JPEG metadata conflicts, resource refusals and downstream I01 equivalence."""

from __future__ import annotations

import dataclasses
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from continuous_fixtures import Fixture, decode_output, exif
from test_cli import call_json, expect
from test_jpeg import DATA, marker, prepend, process, refused

ORIENTED_WIDTH = 9


def physical_exif(unit: int, x: int, y: int, *, orientation: int = 1) -> bytes:
    """Encode independent IFD0 SHORT/RATIONAL entries and exact checked offsets."""
    entries = [
        struct.pack("<HHIH", 0x112, 3, 1, orientation) + b"\0\0",
        struct.pack("<HHII", 0x11A, 5, 1, 62),
        struct.pack("<HHII", 0x11B, 5, 1, 70),
        struct.pack("<HHIH", 0x128, 3, 1, unit) + b"\0\0",
    ]
    return (
        b"II\x2a\0\x08\0\0\0"
        + struct.pack("<H", 4)
        + b"".join(entries)
        + b"\0" * 4
        + struct.pack("<IIII", x, 1, y, 1)
    )


def resolution(exe: Path, root: Path) -> None:
    """Valid EXIF takes precedence; malformed metadata never falls back to valid JFIF."""
    source = bytearray((DATA / "gray-1x1-baseline.jpg").read_bytes())
    at = source.index(b"JFIF\0")
    source[at + 7] = 1
    source[at + 8 : at + 12] = struct.pack(">HH", 72, 144)
    encoded = prepend(
        bytes(source), marker(225, b"Exif\0\0" + physical_exif(2, 300, 150, orientation=6))
    )
    response, data = process(exe, root, "exif-precedence", encoded)
    expect(
        response["source_decoding"]["resolution_conflict"],
        "both conflicting physical declarations are recorded",
    )
    expect(
        response["conversion"]["resolution"] == {"x_ppm": 5906, "y_ppm": 11811},
        "EXIF precedence and axis swap are explicit",
    )
    expect(
        decode_output(data).width == ORIENTED_WIDTH, "orientation is reflected in the image extent"
    )
    unitless = prepend(bytes(source), marker(225, b"Exif\0\0" + physical_exif(1, 3, 2)))
    response, _ = process(exe, root, "unitless-exif", unitless)
    expect(
        response["conversion"]["resolution"] == {"x_ppm": 2835, "y_ppm": 5669},
        "unitless EXIF does not invent DPI and permits physical JFIF",
    )
    malformed = bytearray(physical_exif(2, 300, 150))
    malformed[66:70] = b"\0" * 4
    refused(
        exe,
        root,
        "malformed-exif-no-fallback",
        prepend(bytes(source), marker(225, b"Exif\0\0" + malformed)),
    )
    for name, body in (
        ("exif-prefix", b"Exif-invalid"),
        ("exif-offset", b"Exif\0\0II\x2a\0\xff\xff\xff\x7f"),
    ):
        refused(exe, root, name, prepend(bytes(source), marker(225, body)))
    wrong_mask = root / "wrong-mask.png"
    wrong_mask.write_bytes(Fixture(17, 9, tuple((255,) for _ in range(17 * 9))).encoded())
    refused(exe, root, "unoriented-mask", encoded, "E_INPUT", "--protect-mask", str(wrong_mask))


def limits(exe: Path, root: Path) -> None:
    """Bounded ICC/marker/scan policies are actual refusals, including with override."""
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    adobe_rgb = marker(238, b"Adobe" + struct.pack(">HHHB", 100, 0, 0, 0))
    color = (DATA / "ycbcr-2x2-baseline.jpg").read_bytes()
    refused(exe, root, "conflicting-color", prepend(color, adobe_rgb))
    refused(
        exe,
        root,
        "duplicate-jfif",
        prepend(source, marker(224, b"JFIF\0\x01\x02\0\0\x01\0\x01\0\0")),
    )
    for name, payload in (
        ("icc-zero-index", b"ICC_PROFILE\0\0\x01x"),
        ("icc-zero-count", b"ICC_PROFILE\0\x01\0x"),
        ("icc-signature", b"ICC_PROFILE-invalid"),
    ):
        refused(exe, root, name, prepend(source, marker(226, payload)))
    too_large = [
        marker(226, b"ICC_PROFILE\0" + bytes([i + 1, 65]) + b"x" * 65000) for i in range(65)
    ]
    refused(exe, root, "profile-byte-ceiling", prepend(source, *too_large), error="E_RESOURCE")
    refused(
        exe,
        root,
        "profile-byte-ceiling-override",
        prepend(source, *too_large),
        "E_RESOURCE",
        "--profile-policy",
        "srgb",
    )
    refused(
        exe,
        root,
        "marker-count",
        prepend(source, *(marker(254, b"") for _ in range(65536))),
        error="E_RESOURCE",
    )
    refused(
        exe,
        root,
        "marker-bytes",
        prepend(source, *(marker(254, b"x" * 65000) for _ in range(130))),
        error="E_RESOURCE",
    )
    # Framing refuses excessive work before decoding repeated invalid progressive scans.
    progressive = (DATA / "gray-1x1-progressive.jpg").read_bytes()
    last_scan = progressive.rindex(b"\xff\xda")
    repeated = progressive[:-2] + progressive[last_scan:-2] * 128 + progressive[-2:]
    refused(exe, root, "scan-ceiling", repeated, error="E_RESOURCE")
    late = source[:-2] + marker(225, b"Exif\0\0" + exif(1)) + source[-2:]
    refused(exe, root, "late-interpretation", late)
    refused(
        exe,
        root,
        "iso-gain-map",
        prepend(source, marker(226, b"urn:iso:std:iso:ts:21496:-1\0opaque")),
    )


def equivalent_pipeline(exe: Path, root: Path) -> None:
    """Equivalent decoded samples enter the same conversion, I01 and mask implementations."""
    source = (DATA / "gray-document-progressive.jpeg").read_bytes()
    _, plain = process(exe, root, "equivalence-samples", source)
    decoded = decode_output(plain)
    # Decoder sample correctness has separate coefficient/cosine references in native tests.
    png = dataclasses.replace(decoded, metadata=()).encoded()
    mask = root / "equivalent-mask.png"
    mask.write_bytes(
        Fixture(
            decoded.width,
            decoded.height,
            tuple((255 if i % 7 == 0 else 0,) for i in range(len(decoded.pixels))),
        ).encoded()
    )
    options = [
        "--illumination",
        "surface",
        "--background-cell",
        "32",
        "--background-target",
        "0.8",
        "--protect-mask",
        str(mask),
    ]
    jpeg_response, jpeg_data = process(exe, root, "equivalent-jpeg", source, *options)
    png_source = root / "equivalent.png"
    png_source.write_bytes(png)
    png_directory = root / "equivalent-png"
    png_response = call_json(
        exe, ["process", str(png_source), "--out-dir", str(png_directory), *options, "--json"]
    )
    png_data = (png_directory / "result.png").read_bytes()
    expect(
        decode_output(jpeg_data).pixels == decode_output(png_data).pixels,
        "identical interpreted samples yield identical I01 and protected output",
    )
    expect(
        jpeg_response["illumination"] == png_response["illumination"],
        "the decoder does not add a second processing model",
    )
    # Native SIMD selection must not change the specified coefficient fixture samples.
    directory = root / "scalar-jpeg"
    environment = dict(os.environ, JSIMD_FORCENONE="1")
    raw = subprocess.run(
        [
            str(exe),
            "process",
            str(root / "equivalence-samples.png"),
            "--out-dir",
            str(directory),
            "--json",
        ],
        env=environment,
        capture_output=True,
        text=True,
        check=True,
    )
    expect(
        json.loads(raw.stdout)["publication"] == "completed", "scalar execution is real processing"
    )
    expect(
        decode_output((directory / "result.png").read_bytes()).pixels == decoded.pixels,
        "scalar and available SIMD agree on decoded samples",
    )
    for index, name in enumerate(
        (
            "gray-ac-progressive.jpg",
            "chroma-patch-progressive.jpeg",
            "ycbcr-gradient-4x1-progressive.jpg",
        )
    ):
        _, accelerated = process(exe, root, f"simd-case-{index}", (DATA / name).read_bytes())
        scalar_dir = root / f"scalar-case-{index}"
        subprocess.run(
            [
                str(exe),
                "process",
                str(root / f"simd-case-{index}.png"),
                "--out-dir",
                str(scalar_dir),
                "--json",
            ],
            env=environment,
            capture_output=True,
            text=True,
            check=True,
        )
        expect(
            decode_output(accelerated).pixels
            == decode_output((scalar_dir / "result.png").read_bytes()).pixels,
            "ISLOW and selected upsampling samples agree between scalar and available SIMD",
        )
    # PNG metadata precedence remains unchanged through the neutral metadata type.
    physical_png = dataclasses.replace(
        decoded,
        metadata=(
            (b"pHYs", struct.pack(">IIB", 1000, 2000, 1)),
            (b"eXIf", physical_exif(2, 300, 150)),
        ),
    ).encoded()
    path = root / "png-precedence.png"
    path.write_bytes(physical_png)
    response = call_json(
        exe, ["process", str(path), "--out-dir", str(root / "png-precedence"), "--json"]
    )
    expect(
        response["conversion"]["resolution"] == {"x_ppm": 1000, "y_ppm": 2000},
        "PNG pHYs precedence is preserved",
    )


def main() -> int:
    """Run the metadata/resource/pipeline contract against the real executable."""
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for test in (resolution, limits, equivalent_pipeline):
            test(executable, root)
            print(f"PASS: {test.__name__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
