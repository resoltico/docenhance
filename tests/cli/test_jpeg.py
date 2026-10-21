#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Exercise the real JPEG-to-bundle path, metadata policy and independent PNG references."""

from __future__ import annotations

import copy
import json
import struct
import sys
import tempfile
from pathlib import Path

from codec_samples import check_binary, check_default_sauvola, jpeg_dc_fixture, oriented_fixture
from composition_reference import linear, luminance, terminal_fixed
from continuous_fixtures import (
    Fixture,
    chunks,
    decode_output,
    exif,
    gamma_profile,
    profile_from_output,
)
from jpeg_io import DATA, marker, prepend, process, refused
from test_cli import RECORD_VERSION, call_json, expect

WORD_DEPTH = 16


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
            response["record"]["source"]["container"]["process"] in path.stem,
            "actual baseline/progressive process is recorded",
        )
        interpretation = response["record"]["execution"]["interpretation"]
        expect(interpretation["alpha_composited_pixels"] == 0, "JPEG never invents alpha")
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

    png = Fixture(3, 1, ((0,), (128,), (255,))).encoded()
    input_path = root / "png-named-jpeg.jpg"
    input_path.write_bytes(png)
    response = call_json(
        exe, ["process", str(input_path), "--out-dir", str(root / "signature-png"), "--json"]
    )
    expect(
        response["record"]["source"]["container"]["format"] == "png",
        "PNG with JPEG suffix remains PNG",
    )


def metadata(exe: Path, root: Path) -> None:
    """Orientation is applied exactly once; no camera metadata survives into result PNGs."""
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    _, plain = process(exe, root, "orientation-plain", source)
    original = jpeg_dc_fixture()
    expect(decode_output(plain).pixels == original.pixels, "independent DC-only source")
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
            response["record"]["execution"]["interpretation"]["source_orientation"] == orientation,
            "orientation observation agrees",
        )
        expect(
            set(chunks(data)) == {b"IHDR", b"iCCP", b"IDAT", b"IEND"},
            "EXIF and unknown metadata are stripped",
        )
        mask = root / f"mask-{orientation}.png"
        mask.write_bytes(
            Fixture(
                expected.width, expected.height, tuple((255,) for _ in expected.pixels)
            ).encoded()
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
            decode_output(protected_data).pixels == expected.pixels,
            "oriented protection mask bypasses I01 samples",
        )
        expect(
            protected["record"]["execution"]["illumination"]["counts"]["protected"]
            == len(expected.pixels),
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
        response["record"]["execution"]["interpretation"]["resolution"]
        == {"x_ppm": 2835, "y_ppm": 5669},
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
        response["record"]["execution"]["interpretation"]["profile_decision"] == "icc",
        "compatible ICC controls color interpretation",
    )
    decoded = decode_output(data)
    expect(set(decoded.pixels) != {(180, 80, 30)}, "ICC is applied instead of silently ignored")
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
        overridden["record"]["execution"]["interpretation"]["profile_decision"]
        == "overridden_srgb",
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
        altered["source"]["container"][field] = value
        path.write_text(json.dumps(altered), encoding="utf-8")
        call_json(exe, ["verify", str(path.parent), "--json"], 3)
    for orientation in (0, 9, 4294967295):
        altered = copy.deepcopy(record)
        altered["execution"]["interpretation"]["source_orientation"] = orientation
        path.write_text(json.dumps(altered), encoding="utf-8")
        response = call_json(exe, ["verify", str(path.parent), "--json"], 3)
        expect(response["error"]["code"] == "E_INPUT", "invalid orientation fails admission")
    for version in (RECORD_VERSION - 1, RECORD_VERSION + 1):
        altered = copy.deepcopy(record)
        altered["record"]["version"] = version
        path.write_text(json.dumps(altered), encoding="utf-8")
        call_json(exe, ["verify", str(path.parent), "--json"], 3)
    altered = copy.deepcopy(record)
    altered["output"]["verification"] = "not_compared"
    path.write_text(json.dumps(altered), encoding="utf-8")
    call_json(exe, ["verify", str(path.parent), "--json"], 3)
    path.write_bytes(original)
    call_json(exe, ["verify", str(path.parent), "--json"])


def binary_outputs(exe: Path, root: Path) -> None:
    """Admitted JPEG binary follows actual terminal Q16 and codec errors remain real refusals."""
    source = (DATA / "gray-1x1-baseline.jpg").read_bytes()
    fixture = jpeg_dc_fixture()
    logical = terminal_fixed([rgb[0] for rgb in linear(fixture, 1, None)])
    response, data = process(
        exe, root, "jpeg-binary", source, "--output-mode", "bw", "--binarize", "fixed"
    )
    check_binary(response, data, Fixture(fixture.width, fixture.height, logical, depth=1))
    rgb_source = (DATA / "rgb-1x1-progressive.jpg").read_bytes()
    rgb = Fixture(17, 9, ((180, 80, 30),) * 153, color=2)
    expected = terminal_fixed([luminance(pixel) for pixel in linear(rgb, 1, None)])
    response, data = process(
        exe, root, "rgb-binary", rgb_source, "--output-mode", "bw", "--binarize", "fixed"
    )
    check_binary(response, data, Fixture(17, 9, expected, depth=1))
    check_default_sauvola(
        process(exe, root, "bw-default", source, "--output-mode", "bw"),
        process(
            exe, root, "bw-explicit-sauvola", source, "--output-mode", "bw", "--binarize", "sauvola"
        ),
    )
    damaged = bytearray(source)
    damaged[source.index(b"\xff\xc0") + 4] = 12
    refused(
        exe,
        root,
        "bw-twelve-bit",
        bytes(damaged),
        "E_INPUT",
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
    )


def main() -> int:
    """Run the complete JPEG integration matrix against the built executable."""
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for test in (encodings, metadata, profiles, failures, records, binary_outputs):
            test(executable, root)
            print(f"PASS: {test.__name__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
