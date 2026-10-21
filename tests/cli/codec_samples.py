# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent container fixture coordinates/DC samples and physical binary output checks."""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

from continuous_fixtures import Fixture, chunks, decode_output
from test_cli import expect

FIRST_TRANSPOSED = 5
BYTE_DEPTH = 8
SAUVOLA_VERSION = 2


def jpeg_dc_fixture(width: int = 17, height: int = 9) -> Fixture:
    """Known DC-only gray coefficients from fixtures/jpeg/README, never decoded CLI output."""
    return Fixture(
        width,
        height,
        tuple(
            (48 + (29 * (x // 8) + 43 * (y // 8)) % 160,)
            for y in range(height)
            for x in range(width)
        ),
    )


def tiff_pattern(depth: int, channels: int) -> tuple[tuple[int, ...], ...]:
    """Literal19x17 sample formula from the immutable TIFF fixture README."""
    return tuple(
        tuple(
            (x * 19 + y * 37 + c * 53) % 256
            if depth == BYTE_DEPTH
            else (x * 1493 + y * 2039 + c * 8191 + 1) % 65536
            for c in range(channels)
        )
        for y in range(17)
        for x in range(19)
    )


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


def check_binary(response: dict[str, Any], data: bytes, expected: Fixture) -> None:
    """Check exact logical pixels, real physical PNG1 and one terminal classification report."""
    actual = decode_output(data)
    expect(
        (actual.width, actual.height, actual.depth, actual.color)
        == (expected.width, expected.height, 1, 0),
        "physical grayscale PNG1 shape",
    )
    expect(actual.pixels == expected.pixels, "independent terminal Q16 Fixed samples")
    expect(
        set(chunks(data)) <= {b"IHDR", b"pHYs", b"IDAT", b"IEND"},
        "binary output has no ICC or unexpected metadata",
    )
    record = response["record"]
    expect(
        record["request"]["output"]["mode"] == "bw"
        and record["request"]["output"]["depth"] is None,
        "binary depth is not continuous configuration",
    )
    expect(
        (record["output"]["width"], record["output"]["height"])
        == (expected.width, expected.height),
        "binary output dimensions match independent geometry",
    )
    expect(
        record["output"]["depth"] == 1
        and record["output"]["channels"] == 1
        and not record["output"]["profile_embedded"],
        "binary output facts match physical representation",
    )
    facts = record["execution"]["binarization"]
    white = sum(pixel[0] for pixel in expected.pixels)
    expect(
        facts["method"] == "B03"
        and facts["white_samples"] == white
        and facts["black_samples"] == len(expected.pixels) - white,
        "all pixels including protected destinations are classified exactly once",
    )


def check_mask_kernel_assets(response: dict[str, Any], mask: bytes, kernel: bytes) -> None:
    """Both concrete resource roles retain their distinct original and canonical identities."""
    record = response["record"]
    expect(
        {asset["role"] for asset in record["assets"]} == {"protection_mask", "psf_kernel"},
        "mask and PSF remain distinct retained resources",
    )
    supplied = record["request"]["protection"]["supplied"]
    expect(
        supplied["sha256"] == hashlib.sha256(mask).hexdigest() and supplied["bytes"] == len(mask),
        "protection request retains the supplied source identity",
    )
    directory = Path(response["output"]).parent
    for asset in record["assets"]:
        actual = (directory / asset["path"]).read_bytes()
        expect(
            asset["sha256"] == hashlib.sha256(actual).hexdigest() and asset["bytes"] == len(actual),
            "actual retained asset bytes match their identities",
        )
    expect((directory / "assets/psf.png").read_bytes() == kernel, "PSF original bytes retained")


def check_output_metadata(response: dict[str, Any], data: bytes) -> None:
    """Only canonical color metadata or physical resolution may survive in either output mode."""
    inventory = set(chunks(data))
    binary = response["record"]["request"]["output"]["mode"] == "bw"
    expect(
        inventory <= {b"IHDR", b"iCCP", b"pHYs", b"IDAT", b"IEND"},
        "closed output PNG metadata inventory",
    )
    expect((b"iCCP" in inventory) != binary, "ICC belongs only to continuous output")


def check_default_sauvola(
    implicit: tuple[dict[str, Any], bytes], explicit: tuple[dict[str, Any], bytes]
) -> None:
    """Default and explicit B02 admission produce the same method facts and physical PNG."""
    for response, data in (implicit, explicit):
        request = response["record"]["request"]["binarization"]
        expect(
            request["method"] == "B02"
            and request["version"] == SAUVOLA_VERSION
            and request["selector"] == "sauvola",
            "implicit and explicit binary selectors admit Sauvola version2",
        )
        expect(
            response["record"]["execution"]["binarization"]["method"] == "B02",
            "actual default classification completed B02",
        )
        decoded = decode_output(data)
        expect((decoded.depth, decoded.color) == (1, 0), "Sauvola writes physical grayscale PNG1")
    expect(
        implicit[0]["record"]["request"]["binarization"]
        == explicit[0]["record"]["request"]["binarization"],
        "implicit and explicit Sauvola retain identical method requests",
    )
    expect(
        implicit[0]["record"]["execution"]["binarization"]
        == explicit[0]["record"]["execution"]["binarization"],
        "implicit and explicit Sauvola retain identical completed Facts",
    )
    expect(
        implicit[1] == explicit[1],
        "implicit and explicit Sauvola publish identical whole PNG bytes",
    )
