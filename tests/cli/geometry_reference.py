# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Exact G02 permutations through real codecs, protection and bundle verification."""

from __future__ import annotations

import dataclasses
import json
import math
import struct
from typing import TYPE_CHECKING, Any

from clahe_reference import clahe
from codec_samples import oriented_fixture
from composition_reference import final_codes, linear
from continuous_fixtures import RGB, Fixture, chunks, decode_output, exif
from geometry_admission import refused
from jpeg_io import DATA as JPEG_DATA
from jpeg_io import marker, prepend
from restoration_reference import motion
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode
from tiff_fixtures import TiffFixture

if TYPE_CHECKING:
    from pathlib import Path


BYTE_DEPTH = 8
TURNS = (0, 90, 180, 270)
FIRST_TRANSPOSED = 5
ELIGIBLE_SAMPLES = 4
CODEC_ORIENTATION = 7
CODE_TOLERANCE = 2
COEFFICIENT_TOLERANCE = 2e-14


def rotated(source: Fixture, degrees: int) -> Fixture:
    """Rotate by transposing/reversing rows; independent of production coordinate maps."""
    rows = [
        list(source.pixels[y * source.width : (y + 1) * source.width]) for y in range(source.height)
    ]
    for _ in range(degrees // 90):
        rows = [list(row) for row in zip(*reversed(rows), strict=True)]
    return Fixture(
        len(rows[0]),
        len(rows),
        tuple(pixel for row in rows for pixel in row),
        source.color,
        source.depth,
    )


def process(
    exe: Path, root: Path, source: Fixture, options: list[str]
) -> tuple[Fixture, dict[str, Any], Path]:
    """Require complete schema-admitted publication and unchanged source bytes."""
    path = root / "source.png"
    encoded = source.encoded()
    path.write_bytes(encoded)
    directory = root / f"result-{len(list(root.iterdir()))}"
    response = call_json(
        exe, ["process", str(path), "--out-dir", str(directory), *options, "--json"]
    )
    expect(path.read_bytes() == encoded, "G02 source bytes remain unchanged")
    expected_degrees = int(options[options.index("--rotate") + 1]) if "--rotate" in options else 0
    observation = response["record"]["execution"]["interpretation"]
    expect(observation["rotation"] == expected_degrees, "actual exact turn reported")
    record = json.loads((directory / "run.json").read_bytes())
    expect(record["request"]["rotation"] == expected_degrees, "immutable turn recorded")
    call_json(exe, ["verify", str(directory), "--json"])
    return decode_output((directory / "result.png").read_bytes()), response, directory


def orientation_composition(exe: Path, root: Path, depth: int) -> Path:
    """Asymmetric samples discriminate all eight metadata orientations and four turns."""
    retained = root
    maximum = (1 << depth) - 1
    for color in (0, RGB):
        pixels = tuple(
            (v,) if color == 0 else (v, maximum - v, v // 2)
            for v in (0, maximum // 7, maximum // 3, maximum // 2, maximum - 1, maximum)
        )
        plain = Fixture(3, 2, pixels, color=color, depth=depth)
        baseline, _, _ = process(exe, root, plain, [])
        expect(baseline == plain, "all no-filter sRGB sample codes preserved exactly")
        for orientation in range(1, 9):
            source = dataclasses.replace(
                plain,
                metadata=(
                    (b"eXIf", exif(orientation)),
                    (b"pHYs", struct.pack(">IIB", 1000, 2000, 1)),
                ),
            )
            oriented = oriented_fixture(baseline, orientation)
            for degrees in TURNS:
                actual, response, retained = process(exe, root, source, ["--rotate", str(degrees)])
                expect(
                    actual == rotated(oriented, degrees),
                    f"exact {depth}-bit {color} G01={orientation} G02={degrees}",
                )
                swaps = (orientation >= FIRST_TRANSPOSED) != (degrees in (90, 270))
                resolution = (2000, 1000) if swaps else (1000, 2000)
                expect(
                    response["record"]["execution"]["interpretation"]["resolution"]
                    == {"x_ppm": resolution[0], "y_ppm": resolution[1]},
                    "composed physical axes",
                )
                metadata = chunks((retained / "result.png").read_bytes())
                expect(
                    struct.unpack(">IIB", metadata[b"pHYs"][0]) == (*resolution, 1),
                    "output physical axes match report",
                )
                expect(b"eXIf" not in metadata, "output has no second metadata orientation")
    if depth == BYTE_DEPTH:
        for width, height in ((1, 5), (5, 1), (1, 1)):
            source = Fixture(width, height, tuple((i * 31,) for i in range(width * height)))
            for degrees in TURNS:
                output, _, _ = process(exe, root, source, ["--rotate", str(degrees)])
                expect(output == rotated(source, degrees), "singleton-axis permutation")
    return retained


def protection_frame(exe: Path, root: Path) -> None:
    """Masks arrive after G01 and follow G02; canonical stored mask stays in that input frame."""
    plain = Fixture(3, 2, tuple((10000 + i * 5000,) for i in range(6)), depth=16)
    for orientation in range(1, 9):
        source = dataclasses.replace(plain, metadata=((b"eXIf", exif(orientation)),))
        baseline, _, _ = process(exe, root, source, [])
        mask = Fixture(
            baseline.width, baseline.height, ((1,), (0,), (0,), (0,), (1,), (0,)), depth=1
        )
        path = root / "mask.png"
        path.write_bytes(mask.encoded())
        altered, _, _ = process(
            exe, root, source, ["--contrast", "gamma", "--gamma", "2", "--protect-mask", str(path)]
        )
        expect(
            altered.pixels[0] == baseline.pixels[0] and altered.pixels[4] == baseline.pixels[4],
            "protected B samples retain exact codes",
        )
        expect(
            any(a != b for a, b in zip(altered.pixels, baseline.pixels, strict=True)),
            "mask control has photometric alteration",
        )
        for degrees in TURNS:
            actual, response, directory = process(
                exe,
                root,
                source,
                [
                    "--rotate",
                    str(degrees),
                    "--contrast",
                    "gamma",
                    "--gamma",
                    "2",
                    "--protect-mask",
                    str(path),
                ],
            )
            expect(
                actual == rotated(altered, degrees), "mask and photometric input rotate identically"
            )
            expect(
                response["record"]["execution"]["contrast"]["counts"]["eligible"]
                == ELIGIBLE_SAMPLES
                and response["record"]["execution"]["contrast"]["counts"]["protected"]
                == len(mask.pixels) - ELIGIBLE_SAMPLES
                and response["record"]["execution"]["contrast"]["counts"]["evaluated"]
                == ELIGIBLE_SAMPLES,
                "gamma evaluates eligible destinations without changing protected destinations",
            )
            stored = decode_output((directory / "assets/protect-mask.png").read_bytes())
            expect(
                (stored.width, stored.height) == (mask.width, mask.height),
                "bundle retains B-frame mask dimensions",
            )
            expect(
                tuple(bool(p[0]) for p in stored.pixels) == tuple(bool(p[0]) for p in mask.pixels),
                "bundle retains B-frame protection",
            )
        wrong = dataclasses.replace(mask, width=mask.height, height=mask.width)
        path.write_bytes(wrong.encoded())
        refused(exe, root, ["--rotate", "90", "--protect-mask", str(path)], 3)


def binary_samples(exe: Path, root: Path) -> None:
    """Binary uses common G01 interpretation then exact G02 before terminal classification."""
    source = Fixture(5, 3, tuple((i * 17,) for i in range(15)), metadata=((b"eXIf", exif(6)),))
    for method in ("otsu", "sauvola", "fixed"):
        options = ["--output-mode", "bw", "--binarize", method]
        baseline, _, _ = process(exe, root, source, options)
        expect((baseline.width, baseline.height) == (3, 5), "binary applies metadata orientation")
        for degrees in TURNS:
            actual, _, _ = process(exe, root, source, [*options, "--rotate", str(degrees)])
            expect(
                actual == rotated(baseline, degrees), "binary samples classify after exact rotation"
            )


def processing_frame(exe: Path, root: Path) -> None:
    """Anisotropic CLAHE tiles and explicit R01 PSFs use C after exact user rotation."""
    width, height = 67, 51
    source = Fixture(
        width, height, tuple((9000 + (i * 1733) % 38000,) for i in range(width * height)), depth=16
    )
    turned = rotated(source, 90)
    protected = Fixture(
        width, height, tuple((int(i % 29 == 0),) for i in range(width * height)), depth=1
    )
    path = root / "clahe-mask.png"
    path.write_bytes(protected.encoded())
    protection = [bool(p[0]) for p in rotated(protected, 90).pixels]
    original = linear(turned, 1, None)
    expected, identities = clahe(
        [transfer_encode(rgb[0]) for rgb in original],
        (turned.width, turned.height),
        (2, 3),
        2,
        protection,
    )
    expected_codes = final_codes(
        original, [transfer_decode(value) for value in expected], 16, gray=True
    )
    actual, response, _ = process(
        exe,
        root,
        source,
        [
            "--rotate",
            "90",
            "--contrast",
            "clahe",
            "--clahe-grid",
            "2x3",
            "--protect-mask",
            str(path),
        ],
    )
    expect(
        response["record"]["execution"]["contrast"]["identity_tiles"] == identities,
        "CLAHE resolved tiles use C canvas",
    )
    expect(
        response["record"]["execution"]["contrast"]["counts"]["changed"] > 0,
        "anisotropic CLAHE remains active",
    )
    expect(
        max(abs(p[0] - q[0]) for p, q in zip(actual.pixels, expected_codes, strict=True))
        <= CODE_TOLERANCE,
        "all rotated CLAHE samples match independent C-frame oracle",
    )
    expect(
        all(
            not flag or a == b
            for flag, a, b in zip(protection, actual.pixels, turned.pixels, strict=True)
        ),
        "CLAHE protects exact C-frame destinations",
    )
    restoration_frame(exe, root, source)


def restoration_frame(exe: Path, root: Path, source: Fixture) -> None:
    """A zero blend still resolves the actual post-rotation motion PSF facts."""
    small = Fixture(9, 7, source.pixels[:63], depth=16)
    restored, report, _ = process(
        exe,
        root,
        small,
        [
            "--rotate",
            "90",
            "--deblur",
            "wiener",
            "--psf",
            "motion",
            "--psf-length",
            "3.5",
            "--psf-angle",
            "37",
            "--deblur-blend",
            "0",
        ],
    )
    expect(restored == rotated(small, 90), "zero-blend R01 retains exact rotated samples")
    stage = report["record"]["execution"]["restoration"]
    expect("W_PSF_AFTER_TRANSFORM" in report["warnings"], "G02 precedes inference warning")
    psf_width, psf_height, coefficients = motion(3.5, 37)
    expect(
        (stage["psf"]["width"], stage["psf"]["height"]) == (psf_width, psf_height),
        "PSF resolved in C independently of source axes",
    )
    expect(
        abs(stage["psf"]["normalized_sum"] - math.fsum(coefficients)) < COEFFICIENT_TOLERANCE
        and abs(
            stage["psf"]["centroid_x"]
            - math.fsum(
                value * (i % psf_width - psf_width // 2) for i, value in enumerate(coefficients)
            )
        )
        < COEFFICIENT_TOLERANCE
        and abs(
            stage["psf"]["centroid_y"]
            - math.fsum(
                value * (i // psf_width - psf_height // 2) for i, value in enumerate(coefficients)
            )
        )
        < COEFFICIENT_TOLERANCE,
        "zero blend still validates independent motion PSF normalization and centroid",
    )


def source_codecs(exe: Path, root: Path) -> None:
    """Exact G02 composes with codec G01 without changing decoder precision or samples."""
    tiff = TiffFixture(
        3, 2, ((1,), (1025,), (16385,), (32769,), (49153,), (65534,)), depth=16, orientation=7
    )
    jpeg = (JPEG_DATA / "gray-document-progressive.jpeg").read_bytes()
    sources = (tiff.encoded(), prepend(jpeg, marker(225, b"Exif\0\0" + exif(7))))
    for index, encoded in enumerate(sources):
        path = root / f"codec-{index}.data"
        path.write_bytes(encoded)
        baseline = None
        for degrees in TURNS:
            directory = root / f"codec-{index}-{degrees}"
            response = call_json(
                exe,
                [
                    "process",
                    str(path),
                    "--out-dir",
                    str(directory),
                    "--rotate",
                    str(degrees),
                    "--json",
                ],
            )
            output = decode_output((directory / "result.png").read_bytes())
            if degrees == 0:
                baseline = output
            expect(baseline is not None, "identity establishes independent decoded samples")
            if baseline is None:
                message = "Missing source-codec baseline"
                raise AssertionError(message)
            expect(output == rotated(baseline, degrees), "decoded JPEG/TIFF samples rotate exactly")
            expect(
                response["record"]["execution"]["interpretation"]["source_orientation"]
                == CODEC_ORIENTATION,
                "codec orientation retained separately",
            )
            expect(
                response["record"]["execution"]["interpretation"]["rotation"] == degrees,
                "codec turn observed",
            )
            expect(
                response["record"]["execution"]["interpretation"]["resolution"] is None,
                "unknown physical resolution remains unknown",
            )
            call_json(exe, ["verify", str(directory), "--json"])
        expect(path.read_bytes() == encoded, "source codec snapshot unchanged")
