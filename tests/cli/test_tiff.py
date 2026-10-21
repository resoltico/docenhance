#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real TIFF-to-PNG bundle boundaries and independent source/decoded-sample expectations."""

from __future__ import annotations

import dataclasses
import struct
import sys
import tempfile
from pathlib import Path

from codec_samples import (
    check_binary,
    check_default_sauvola,
    check_mask_kernel_assets,
    oriented_fixture,
    tiff_pattern,
)
from composition_reference import final_codes, linear, luminance, terminal_fixed
from continuous_fixtures import Fixture, gamma_profile, profile_from_output
from test_cli import call_json, expect
from tiff_fixtures import Field, TiffFixture, container
from tiff_io import DATA, process, refused

BYTE_DEPTH = 8
WORD_DEPTH = 16
FLATTENED_PIXELS = 3
RGBA_CHANNELS = 4
PROTECTED_BINARY_SAMPLES = 6


def sample_matrix(exe: Path, root: Path) -> None:
    """Independent exact samples span all lossless container alternatives."""
    for path in sorted(DATA.glob("d*-c*-t*-p*-b*-l*.tif")):
        decoded, response, _ = process(exe, root, path.read_bytes())
        depth = int(path.stem.split("-")[0][1:])
        channels = int(path.stem.split("-")[1][1:])
        container_facts = response["record"]["source"]["container"]
        expect(
            (container_facts["precision"], container_facts["samples"]) == (depth, channels),
            "source precision/component observations match independent fixture declarations",
        )
        if channels in (2, RGBA_CHANNELS):
            expect(
                response["record"]["execution"]["interpretation"]["alpha_composited_pixels"] > 0,
                "alpha is interpreted",
            )
            continue
        expected = tiff_pattern(depth, channels)
        expect(decoded.pixels == expected and decoded.depth == depth, f"exact {path.name} samples")
    for path in sorted(DATA.glob("compression-*.tif")):
        decoded, response, _ = process(exe, root, path.read_bytes())
        depth = int(path.stem.split("-")[2][1:])
        expect(
            response["record"]["source"]["container"]["precision"] == depth,
            "compression fixture depth is independent of reported samples",
        )
        expected = tiff_pattern(depth, 3)
        expect(decoded.pixels == expected, f"lossless {path.name}")
    for path in sorted(DATA.glob("fax-*.tif")):
        decoded, _, _ = process(exe, root, path.read_bytes())
        expect(
            decoded.pixels
            == tuple((255 if y % 2 == 0 else 0,) for y in range(4) for _ in range(16)),
            "fax MINISWHITE keeps white/black meaning",
        )
    for path in sorted(DATA.glob("bilevel-*.tif")):
        decoded, _, _ = process(exe, root, path.read_bytes())
        expect(
            decoded.pixels
            == tuple((255 if (x * 3 + y) % 2 == 0 else 0,) for y in range(17) for x in range(19)),
            "packed MINISWHITE and fill order preserve binary meaning",
        )
    for path in sorted(DATA.glob("jpeg-*.tif")):
        if path.name == "jpeg-many-scans.tif":
            continue
        process(exe, root, path.read_bytes())


def palette_alpha_orientation(exe: Path, root: Path) -> None:
    """Palette precision, source unassociation, linear flattening and eight orientations."""
    palette = tuple((i * 193) % 65536 for i in range(256))
    color_map = b"".join(
        struct.pack("<H", value)
        for channel in range(3)
        for value in (palette if channel == 0 else tuple(reversed(palette)))
    )
    fixture = TiffFixture(4, 1, ((0,), (1,), (128,), (255,)), photo=3)
    decoded, _, _ = process(exe, root, fixture.encoded({320: (3, 768, color_map)}))
    palette_expected = tuple(
        (palette[i], palette[255 - i], palette[255 - i]) for i in (0, 1, 128, 255)
    )
    expect(
        decoded.depth == WORD_DEPTH and decoded.pixels == palette_expected,
        "palette retains non-8-bit-replicated precision",
    )
    for depth, maximum in ((8, 255), (16, 65535)):
        # Associated channels at one-third opacity encode white/black exactly; zero hides color.
        alpha_sample = maximum // 3
        pixels = (
            (alpha_sample, alpha_sample, alpha_sample, alpha_sample),
            (0, 0, 0, alpha_sample),
            (maximum, maximum, maximum, maximum),
            (0, 0, 0, 0),
        )
        source = TiffFixture(2, 2, pixels, photo=2, alpha=1, depth=depth)
        for matte in ("white", "black"):
            decoded, response, _ = process(exe, root, source.encoded(), "--alpha", matte)
            straight = Fixture(
                2,
                2,
                (
                    (maximum, maximum, maximum, alpha_sample),
                    (0, 0, 0, alpha_sample),
                    (maximum, maximum, maximum, maximum),
                    (0, 0, 0, 0),
                ),
                color=6,
                depth=depth,
            )
            interpreted = linear(straight, int(matte == "white"), None)
            expected = final_codes(
                interpreted, [luminance(p) for p in interpreted], depth, gray=False
            )
            expect(
                decoded.pixels == expected,
                "independent unassociation then linear-light alpha composition",
            )
            expect(
                response["record"]["execution"]["interpretation"]["alpha_composited_pixels"]
                == FLATTENED_PIXELS,
                "flatten count",
            )
        refused(exe, root, source.encoded(), "E_INPUT", "--alpha", "reject")
        white = TiffFixture(
            2, 1, ((0, alpha_sample), (alpha_sample, alpha_sample)), photo=0, alpha=1, depth=depth
        )
        decoded, _, _ = process(exe, root, white.encoded(), "--alpha", "black")
        expect(
            decoded.pixels[0][0] > decoded.pixels[1][0],
            "MINISWHITE inversion follows unassociation",
        )
        invalid_alpha = TiffFixture(1, 1, ((maximum, alpha_sample),), photo=1, alpha=1, depth=depth)
        refused(exe, root, invalid_alpha.encoded(), "E_INPUT")
    oriented_pixels = ((0,), (40,), (80,), (120,), (160,), (255,))
    for orientation in range(1, 9):
        source = TiffFixture(3, 2, oriented_pixels, orientation=orientation)
        decoded, response, _ = process(exe, root, source.encoded())
        reference = oriented_fixture(Fixture(3, 2, oriented_pixels), orientation)
        expect(
            (decoded.width, decoded.height, decoded.pixels)
            == (reference.width, reference.height, reference.pixels),
            "orientation exactly once",
        )
        expect(
            response["record"]["source"]["container"]["orientation"] == orientation,
            "actual orientation recorded",
        )


def metadata_and_pipeline(exe: Path, root: Path) -> None:
    """ICC, density axes, explicit override, enhancement composition and record refusal."""
    source = TiffFixture(3, 2, ((0,), (40,), (80,), (120,), (160,), (255,)), orientation=6)
    fields: dict[int, Field] = {
        282: (5, 1, struct.pack("<II", 254, 1)),
        283: (5, 1, struct.pack("<II", 127, 1)),
        296: (3, 1, struct.pack("<H", 2)),
    }
    _, response, encoded_output = process(exe, root, source.encoded(fields))
    expect(
        response["record"]["execution"]["interpretation"]["resolution"]
        == {"x_ppm": 5000, "y_ppm": 10000},
        "oriented density axes",
    )
    mask = root / "oriented-tiff-mask.png"
    mask.write_bytes(Fixture(2, 3, ((1,), (0,), (0,), (0,), (0,), (0,)), depth=8).encoded())
    process(exe, root, source.encoded(fields), "--protect-mask", str(mask))
    wrong = root / "wrong-tiff-mask.png"
    wrong.write_bytes(Fixture(3, 2, ((1,), (0,), (0,), (0,), (0,), (0,)), depth=8).encoded())
    refused(exe, root, source.encoded(fields), "E_INPUT", "--protect-mask", str(wrong))
    profile = gamma_profile(profile_from_output(encoded_output), 1.0)
    fields[34675] = (7, len(profile), profile)
    decoded, response, _ = process(exe, root, source.encoded(fields))
    expect(
        response["record"]["execution"]["interpretation"]["profile_decision"] == "icc",
        "embedded TIFF ICC applies",
    )
    overridden, response, _ = process(exe, root, source.encoded(fields), "--profile-policy", "srgb")
    no_profile = oriented_fixture(Fixture(3, 2, source.pixels), source.orientation)
    expect(
        overridden.pixels == no_profile.pixels, "independent source under explicit sRGB override"
    )
    expect(
        decoded.pixels != no_profile.pixels
        and response["record"]["execution"]["interpretation"]["profile_decision"]
        == "overridden_srgb",
        "profile override is deliberate and observable",
    )
    enhancement_pipeline(exe, root)


def enhancement_pipeline(exe: Path, root: Path) -> None:
    """Real TIFF samples enter selected eager photometric stages and complete owned Facts."""
    path = DATA / "d16-c3-t1-p2-b1-l0.tif"
    process(
        exe,
        root,
        path.read_bytes(),
        "--illumination",
        "off",
        "--denoise",
        "nlm",
        "--nlm-search",
        "7",
    )
    size = 64
    lit = TiffFixture(
        size,
        size,
        tuple((150 + x * 70 // (size - 1),) for _ in range(size) for x in range(size)),
        depth=8,
    )
    for selection in ("surface", "morph"):
        _, response, _ = process(
            exe,
            root,
            lit.encoded(),
            "--illumination",
            selection,
            "--background-cell" if selection == "surface" else "--background-radius",
            "8",
            "--denoise",
            "nlm",
            "--nlm-patch",
            "3",
            "--nlm-search",
            "7",
        )
        expect(
            response["record"]["execution"]["illumination"]["counts"]["evaluated"] == size * size
            and response["record"]["execution"]["denoising"]["counts"]["evaluated"] == size * size,
            "TIFF composes selected illumination and D01 with verified PNG publication",
        )


def invalid_domains(exe: Path, root: Path) -> None:
    """Named refusals for unsupported/ambiguous fields and malformed framing."""
    base = TiffFixture(3, 2, ((0,), (40,), (80,), (120,), (160,), (255,)))
    for tag, value in (
        (259, 6),
        (262, 5),
        (266, 3),
        (274, 0),
        (284, 3),
        (317, 3),
        (339, 2),
        (339, 3),
        (338, 0),
        (338, 3),
    ):
        refused(exe, root, base.encoded({tag: (3, 1, struct.pack("<H", value))}))
    jpeg = (DATA.parent / "jpeg/gray-1x1-baseline.jpg").read_bytes()
    for marker in (195, 201):
        changed = bytearray(jpeg)
        sof = changed.index(b"\xff\xc0")
        changed[sof + 1] = marker
        fields = TiffFixture(17, 9, ((0,),), compression=7, rows=9).fields()
        refused(exe, root, container(fields, [bytes(changed)], big=False, little=True))
    refused(exe, root, (DATA / "jpeg-many-scans.tif").read_bytes(), "E_RESOURCE")
    refused(exe, root, base.encoded(next_ifd=8))
    refused(exe, root, base.encoded({297: (3, 2, struct.pack("<HH", 0, 2))}))
    refused(exe, root, base.encoded({282: (5, 1, struct.pack("<II", 1, 0))}))
    refused(exe, root, base.encoded({34675: (7, 3, b"bad")}))
    for encoded in (b"II*\0", b"MM\0+\0\4\0\0" + bytes(8), base.encoded()[:-1]):
        refused(exe, root, encoded)
    refused(exe, root, (DATA / "deflate-missing-checksum.tif").read_bytes())
    fields = base.fields()
    fields[278] = (4, 1, struct.pack("<I", 1))
    refused(exe, root, container(fields, [b"\0"], big=False, little=True))
    # The unit bound is independent of the extension and compression ratio.
    oversized = dataclasses.replace(base, width=8388609, height=1)
    fields = oversized.fields()
    refused(exe, root, container(fields, [b"\0"], big=False, little=True), "E_RESOURCE")


def binary_outputs(exe: Path, root: Path) -> None:
    """TIFF integer domains admit real PNG1; G01/mask/PSF roles remain independent resources."""
    pixels = ((0,), (40,), (80,), (120,), (160,), (255,))
    for depth in (8, 16):
        samples = tuple((p[0] * (257 if depth == WORD_DEPTH else 1),) for p in pixels)
        source = TiffFixture(3, 2, samples, depth=depth, orientation=6)
        expected_source = oriented_fixture(Fixture(3, 2, samples, depth=depth), 6)
        logical = terminal_fixed([rgb[0] for rgb in linear(expected_source, 1, None)])
        _, response, data = process(
            exe, root, source.encoded(), "--output-mode", "bw", "--binarize", "fixed"
        )
        check_binary(response, data, Fixture(2, 3, logical, depth=1))
        call_json(exe, ["verify", str(Path(response["output"]).parent), "--json"])
    _, bilevel, packed = process(
        exe,
        root,
        (DATA / "bilevel-1-fill1.tif").read_bytes(),
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
    )
    expected = tuple((int((x * 3 + y) % 2 == 0),) for y in range(17) for x in range(19))
    check_binary(bilevel, packed, Fixture(19, 17, expected, depth=1))
    expect(
        bilevel["record"]["source"]["container"]["precision"] == 1, "original bilevel source domain"
    )
    protected_binary(exe, root, pixels)


def protected_binary(exe: Path, root: Path, pixels: tuple[tuple[int, ...], ...]) -> None:
    """Protection and PSF identity retain the same oriented word-source control."""
    # Own the same last (16-bit) source explicitly, independent of loop assignment.
    retained_samples = tuple((p[0] * 257,) for p in pixels)
    source = TiffFixture(3, 2, retained_samples, depth=WORD_DEPTH, orientation=6)
    retained_source = oriented_fixture(Fixture(3, 2, retained_samples, depth=WORD_DEPTH), 6)
    logical = terminal_fixed([rgb[0] for rgb in linear(retained_source, 1, None)])
    mask = root / "binary-oriented-mask.png"
    mask_bytes = Fixture(2, 3, ((1,),) * 6, depth=1).encoded()
    mask.write_bytes(mask_bytes)
    kernel = root / "binary-kernel.png"
    kernel_bytes = Fixture(3, 3, ((0,),) * 4 + ((255,),) + ((0,),) * 4).encoded()
    kernel.write_bytes(kernel_bytes)
    _, response, data = process(
        exe,
        root,
        source.encoded(),
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
        "--illumination",
        "surface",
        "--protect-mask",
        str(mask),
        "--deblur",
        "wiener",
        "--psf",
        "kernel",
        "--psf-file",
        str(kernel),
        "--deblur-blend",
        "0",
    )
    check_binary(response, data, Fixture(2, 3, logical, depth=1))
    record = response["record"]
    expect(
        record["execution"]["illumination"]["counts"]["protected"] == PROTECTED_BINARY_SAMPLES,
        "mask protects photometry while all six pixels are still classified",
    )
    check_mask_kernel_assets(response, mask_bytes, kernel_bytes)
    expect(
        mask.read_bytes() == mask_bytes and kernel.read_bytes() == kernel_bytes,
        "processing preserves original mask and PSF bytes",
    )
    directory = Path(response["output"]).parent
    call_json(exe, ["verify", str(directory), "--json"])
    _, implicit, implicit_png = process(exe, root, source.encoded(), "--output-mode", "bw")
    _, explicit, explicit_png = process(
        exe, root, source.encoded(), "--output-mode", "bw", "--binarize", "sauvola"
    )
    check_default_sauvola((implicit, implicit_png), (explicit, explicit_png))
    for response in (implicit, explicit):
        call_json(exe, ["verify", str(Path(response["output"]).parent), "--json"])
    signed = source.encoded({339: (3, 1, struct.pack("<H", 2))})
    refused(exe, root, signed, "E_INPUT", "--output-mode", "bw", "--binarize", "fixed")


def main() -> None:
    """Run the actual executable boundary suite."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-tiff-") as directory:
        root = Path(directory)
        sample_matrix(exe, root)
        palette_alpha_orientation(exe, root)
        metadata_and_pipeline(exe, root)
        invalid_domains(exe, root)
        binary_outputs(exe, root)
        # Every process uses the shared staged reader; exercise its public read-only route once.
        call_json(exe, ["verify", str(min(root.glob("bundle-*"))), "--json"])
    print("TIFF independent samples, interpretation, pipeline and refusals passed")


if __name__ == "__main__":
    main()
