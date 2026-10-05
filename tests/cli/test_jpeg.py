#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Exercise the real JPEG-to-bundle path, metadata policy and independent PNG references."""

from __future__ import annotations

import copy
import hashlib
import json
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any

from continuous_fixtures import (
    Fixture,
    chunks,
    decode_output,
    exif,
    gamma_profile,
    profile_from_output,
)
from test_cli import call_json, expect

WIRE_VERSION = 3
RECORD_VERSION = 3
WORD_DEPTH = 16
FIRST_TRANSPOSED = 5
DATA = Path(__file__).resolve().parents[1] / "fixtures/jpeg"


def marker(code: int, payload: bytes) -> bytes:
    """Frame metadata independently of the native JPEG adapter."""
    return bytes([255, code]) + struct.pack(">H", len(payload) + 2) + payload


def prepend(source: bytes, *markers: bytes) -> bytes:
    """Insert explicit test declarations immediately after SOI."""
    return source[:2] + b"".join(markers) + source[2:]


def process(
    exe: Path, root: Path, name: str, source: bytes, *options: str
) -> tuple[dict[str, Any], bytes]:
    """A misleading PNG suffix must not change JPEG source admission."""
    input_path = root / f"{name}.png"
    input_path.write_bytes(source)
    directory = root / name
    response = call_json(
        exe, ["process", str(input_path), "--out-dir", str(directory), *options, "--json"]
    )
    record = json.loads((directory / "run.json").read_bytes())
    expect(response["schema_version"] == WIRE_VERSION, "new response wire version is explicit")
    expect(
        record["record"]["version"] == RECORD_VERSION,
        "new records have explicit source observations",
    )
    expect(
        record["source"]["sha256"] == hashlib.sha256(source).hexdigest(),
        "identity is the original encoded JPEG",
    )
    expect(
        record["source"]["decoding"] == response["source_decoding"],
        "response and record use one typed source mapping",
    )
    expect(
        response["source_decoding"]["format"] == "jpeg",
        "signature chooses JPEG regardless of extension",
    )
    call_json(exe, ["verify", str(directory), "--json"])
    expect(input_path.read_bytes() == source, "processing preserves the source")
    return response, (directory / "result.png").read_bytes()


def refused(
    exe: Path,
    root: Path,
    name: str,
    source: bytes,
    error: str = "E_INPUT",
    *options: str,
) -> None:
    """Neither invalid input nor unsupported domains may leave a published/staged bundle."""
    status = {"E_INPUT": 3, "E_RESOURCE": 4, "E_NOT_IMPLEMENTED": 4}[error]
    input_path = root / f"{name}.jpg"
    input_path.write_bytes(source)
    directory = root / name
    response = call_json(
        exe, ["process", str(input_path), "--out-dir", str(directory), *options, "--json"], status
    )
    expect(response["error"]["code"] == error, "refusal has the specified error class")
    expect(response["publication"] == "not_started", "decode refusal precedes staging")
    expect(
        not directory.exists() and not list(root.glob(f"{name}.staging-*")),
        "decode failure has no publication",
    )
    expect(input_path.read_bytes() == source, "refusal preserves the source")


def encodings(exe: Path, root: Path) -> None:
    """All admitted component/sampling/process fixtures produce the same specified samples."""
    for path in sorted(DATA.glob("*.jpg")):
        response, data = process(exe, root, path.stem, path.read_bytes())
        decoded = decode_output(data)
        expect(
            (decoded.width, decoded.height, decoded.depth) == (17, 9, 8),
            "full-resolution eight-bit JPEG decoding",
        )
        expect(
            response["source_decoding"]["process"] in path.stem,
            "actual baseline/progressive process is recorded",
        )
        expect(response["conversion"]["alpha_flattened_pixels"] == 0, "JPEG never invents alpha")
        if path.stem.startswith("gray-1x1"):
            expected = tuple(
                (48 + (((x // 8) * 29 + (y // 8) * 43) % 160),) for y in range(9) for x in range(17)
            )
            expect(decoded.pixels == expected, "independent DC reference is exact")
        if path.stem.startswith("rgb-"):
            expect(set(decoded.pixels) == {(180, 80, 30)}, "RGB components retain their order")
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    _, data = process(exe, root, "word-output", source, "--bit-depth", "16")
    expect(
        decode_output(data).depth == WORD_DEPTH, "output precision is a separate explicit choice"
    )
    refused(exe, root, "jpeg-binary", source, "E_NOT_IMPLEMENTED", "--output-mode", "bw")
    png = Fixture(3, 1, ((0,), (128,), (255,))).encoded()
    input_path = root / "png-named-jpeg.jpg"
    input_path.write_bytes(png)
    response = call_json(
        exe, ["process", str(input_path), "--out-dir", str(root / "signature-png"), "--json"]
    )
    expect(response["source_decoding"]["format"] == "png", "PNG with JPEG suffix remains PNG")


def oriented_fixture(source: Fixture, orientation: int) -> Fixture:
    """Reference all eight EXIF permutations directly from source coordinates."""
    width, height = (
        (source.height, source.width)
        if orientation >= FIRST_TRANSPOSED
        else (source.width, source.height)
    )
    pixels = []
    for y in range(height):
        for x in range(width):
            coordinates = (
                (x, y),
                (source.width - 1 - x, y),
                (source.width - 1 - x, source.height - 1 - y),
                (x, source.height - 1 - y),
                (y, x),
                (y, source.height - 1 - x),
                (source.width - 1 - y, source.height - 1 - x),
                (source.width - 1 - y, x),
            )
            sx, sy = coordinates[orientation - 1]
            pixels.append(source.pixels[sy * source.width + sx])
    return Fixture(width, height, tuple(pixels), source.color, source.depth)


def metadata(exe: Path, root: Path) -> None:
    """Orientation is applied exactly once; no camera metadata survives into result PNGs."""
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    _, plain = process(exe, root, "orientation-plain", source)
    original = decode_output(plain)
    for orientation in range(1, 9):
        oriented = prepend(source, marker(225, b"Exif\0\0" + exif(orientation)))
        response, data = process(exe, root, f"orientation-{orientation}", oriented)
        expected = oriented_fixture(original, orientation)
        actual = decode_output(data)
        expect(
            actual.pixels == expected.pixels
            and (actual.width, actual.height) == (expected.width, expected.height),
            "exact metadata gather without double orientation",
        )
        expect(
            response["conversion"]["source_orientation"] == orientation,
            "orientation observation agrees",
        )
        expect(
            set(chunks(data)) == {b"IHDR", b"iCCP", b"IDAT", b"IEND"},
            "EXIF and unknown metadata are stripped",
        )
        mask = root / f"mask-{orientation}.png"
        mask.write_bytes(
            Fixture(actual.width, actual.height, tuple((255,) for _ in actual.pixels)).encoded()
        )
        protected, protected_data = process(
            exe,
            root,
            f"protected-{orientation}",
            oriented,
            "--illumination",
            "surface",
            "--protect-mask",
            str(mask),
        )
        expect(
            decode_output(protected_data).pixels == actual.pixels,
            "oriented protection mask bypasses I01 samples",
        )
        expect(
            protected["illumination"]["protected_samples"] == len(actual.pixels),
            "mask uses oriented dimensions",
        )
    invalid = marker(225, b"Exif\0\0" + exif(0))
    refused(exe, root, "invalid-orientation", prepend(source, invalid))
    refused(exe, root, "duplicate-exif", prepend(source, invalid, invalid))
    # Replace JFIF's unitless density with an explicit physical declaration.
    physical = bytearray(source)
    offset = physical.index(b"JFIF\0")
    physical[offset + 7] = 1
    physical[offset + 8 : offset + 12] = struct.pack(">HH", 72, 144)
    response, data = process(exe, root, "jfif-physical", bytes(physical))
    expect(
        response["conversion"]["resolution"] == {"x_ppm": 2835, "y_ppm": 5669},
        "physical JFIF is converted to normalized units",
    )
    expect(b"pHYs" in chunks(data), "physical resolution is retained")


def profiles(exe: Path, root: Path) -> None:
    """ICC assembly is ordered, complete and bounded; override cannot repair its framing."""
    source = (DATA / "rgb-1x1-baseline.jpg").read_bytes()
    _, plain = process(exe, root, "profile-plain", source)
    profile = gamma_profile(profile_from_output(plain), 1.0)
    parts = [profile[i : i + 150] for i in range(0, len(profile), 150)]
    segments = [
        marker(226, b"ICC_PROFILE\0" + bytes([i + 1, len(parts)]) + part)
        for i, part in enumerate(parts)
    ]
    response, data = process(exe, root, "icc-reordered", prepend(source, *reversed(segments)))
    expect(
        response["conversion"]["profile_decision"] == "icc",
        "compatible ICC controls color interpretation",
    )
    decoded = decode_output(data)
    expect(
        decoded.pixels != decode_output(plain).pixels, "ICC is applied instead of silently ignored"
    )
    malformed = marker(226, b"ICC_PROFILE\0\x01\x02" + parts[0])
    refused(exe, root, "icc-incomplete", prepend(source, malformed))
    refused(
        exe,
        root,
        "icc-incomplete-override",
        prepend(source, malformed),
        "E_INPUT",
        "--profile-policy",
        "srgb",
    )
    refused(exe, root, "icc-duplicate", prepend(source, segments[0], segments[0]))
    bad_profile = marker(226, b"ICC_PROFILE\0\x01\x01invalid-profile")
    refused(exe, root, "invalid-profile", prepend(source, bad_profile))
    overridden, _ = process(
        exe, root, "profile-override", prepend(source, bad_profile), "--profile-policy", "srgb"
    )
    expect(
        overridden["conversion"]["profile_decision"] == "overridden_srgb",
        "explicit override ignores only profile semantics",
    )
    incompatible = bytearray(profile)
    incompatible[16:20] = b"CMYK"
    refused(
        exe,
        root,
        "incompatible-profile",
        prepend(source, marker(226, b"ICC_PROFILE\0\x01\x01" + incompatible)),
    )


def failures(exe: Path, root: Path) -> None:
    """Corruption and recognized unsupported coding/extension domains fail explicitly."""
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    for count in (1, 2, 20, len(source) // 2):
        refused(exe, root, f"truncated-{count}", source[:-count])
    refused(exe, root, "trailing-bytes", source + b"junk")
    refused(exe, root, "concatenated", source + source)
    for label, declaration in (
        ("mpo", marker(226, b"MPF\0opaque")),
        ("gain-map", marker(225, b"http://ns.adobe.com/hdr-gain-map/1.0/")),
        ("jumbf", marker(235, b"opaque")),
    ):
        refused(exe, root, label, prepend(source, declaration))
    frame = source.index(b"\xff\xc0")
    for label, index, value in (
        ("twelve-bit", frame + 4, 12),
        ("cmyk", frame + 9, 4),
        ("arithmetic", frame + 1, 201),
        ("lossless", frame + 1, 195),
    ):
        altered = bytearray(source)
        altered[index] = value
        refused(exe, root, label, bytes(altered))
    adobe = b"Adobe" + bytes((0, 100, 0, 0, 0, 0, 2))
    refused(exe, root, "adobe-transform", prepend(source, marker(238, adobe)))
    ycc = (DATA / "ycbcr-1x1-baseline.jpg").read_bytes()
    color_frame = ycc.index(b"\xff\xc0")
    for label, offset, value in (("chroma-sampling", 14, 0x21), ("mcu-block-bound", 11, 0x44)):
        altered = bytearray(ycc)
        altered[color_frame + offset] = value
        refused(exe, root, label, bytes(altered))
    # Remove JFIF and consistently rename component identities in frame and scan: the coding
    # structure remains complete, but none of the admitted color interpretations applies.
    jfif = ycc.index(b"\xff\xe0")
    size = int.from_bytes(ycc[jfif + 2 : jfif + 4], "big")
    unknown = bytearray(ycc[:jfif] + ycc[jfif + 2 + size :])
    frame = unknown.index(b"\xff\xc0")
    scan = unknown.index(b"\xff\xda")
    for index, identity in enumerate((7, 8, 9)):
        unknown[frame + 10 + 3 * index] = identity
        unknown[scan + 5 + 2 * index] = identity
    refused(exe, root, "ambiguous-color", bytes(unknown))
    # Complete framing does not legalize damaged entropy or a native recovery warning.
    entropy = source.index(b"\xff\xda")
    altered = bytearray(source)
    altered[entropy + 4] = 0  # Invalid zero-component scan.
    refused(exe, root, "native-scan-failure", bytes(altered))


def records(exe: Path, root: Path) -> None:
    """Current source observations are cross-checked; obsolete record versions are rejected."""
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    process(exe, root, "record-jpeg", source)
    path = root / "record-jpeg/run.json"
    original = path.read_bytes()
    record = json.loads(original)
    for field, value in (
        ("precision", 12),
        ("width", 18),
        ("decoder_policy", "fast"),
        ("components", 3),
        ("format", "png"),
        ("scans", 0),
        ("resolution_conflict", True),
    ):
        altered = copy.deepcopy(record)
        altered["source"]["decoding"][field] = value
        path.write_text(json.dumps(altered), encoding="utf-8")
        call_json(exe, ["verify", str(path.parent), "--json"], 3)
    for orientation in (0, 9, 4294967295):
        altered = copy.deepcopy(record)
        altered["execution"]["conversion"]["source_orientation"] = orientation
        path.write_text(json.dumps(altered), encoding="utf-8")
        response = call_json(exe, ["verify", str(path.parent), "--json"], 3)
        expect(response["error"]["code"] == "E_INPUT", "invalid orientation fails admission")
    altered = copy.deepcopy(record)
    altered["execution"]["conversion"]["verified"] = False
    path.write_text(json.dumps(altered), encoding="utf-8")
    call_json(exe, ["verify", str(path.parent), "--json"], 3)
    path.write_bytes(original)


def main() -> int:
    """Run the complete JPEG integration matrix against the built executable."""
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for test in (encodings, metadata, profiles, failures, records):
            test(executable, root)
            print(f"PASS: {test.__name__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
