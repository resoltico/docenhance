#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real TIFF-to-PNG bundle boundaries and independent source/decoded-sample expectations."""

from __future__ import annotations

import dataclasses
import hashlib
import json
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any

from continuous_fixtures import Fixture, decode_output, gamma_profile, profile_from_output
from test_cli import call_json, expect
from test_jpeg import oriented_fixture
from tiff_fixtures import Field, TiffFixture, container

WIRE_VERSION = 4
BYTE_DEPTH = 8
WORD_DEPTH = 16
FLATTENED_PIXELS = 3
RGBA_CHANNELS = 4

DATA = Path(__file__).resolve().parents[1] / "fixtures/tiff"


def process(
    exe: Path, root: Path, encoded: bytes, *options: str
) -> tuple[Fixture, dict[str, Any], bytes]:
    """Identify the exact snapshot and verify a coherent, immutable published bundle."""
    index = len(list(root.iterdir()))
    source = root / f"input-{index}.data"
    output = root / f"bundle-{index}"
    source.write_bytes(encoded)
    response = call_json(
        exe, ["process", str(source), "--out-dir", str(output), *options, "--json"]
    )
    record = json.loads((output / "run.json").read_bytes())
    expect(response["publication"] == "completed", "TIFF bundle is published")
    expect(
        response["schema_version"] == WIRE_VERSION and record["record"]["version"] == WIRE_VERSION,
        "current closed wire contracts",
    )
    expect(response["source_decoding"]["format"] == "tiff", "signature selects TIFF")
    expect(record["source"]["decoding"] == response["source_decoding"], "one source mapping")
    expect(record["source"]["sha256"] == hashlib.sha256(encoded).hexdigest(), "snapshot identity")
    expect(response["conversion"]["verified"], "real PNG output sample verification")
    expect(source.read_bytes() == encoded, "source remains untouched")
    result = (output / "result.png").read_bytes()
    return decode_output(result), response, result


def refused(
    exe: Path,
    root: Path,
    encoded: bytes,
    error: str = "E_INPUT",
    *options: str,
    publication: str = "not_started",
) -> None:
    """Refusal reports its actual stage and preserves the source bytes."""
    index = len(list(root.iterdir()))
    source, output = root / f"bad-{index}.tif", root / f"refusal-{index}"
    source.write_bytes(encoded)
    response = call_json(
        exe,
        ["process", str(source), "--out-dir", str(output), *options, "--json"],
        3 if error == "E_INPUT" else 4,
    )
    expect(response["error"]["code"] == error, "explicit refusal class")
    expect(
        response["publication"] == publication
        and not output.exists()
        and not list(root.glob(f"{output.name}.staging-*")),
        "no published/staged effects",
    )
    expect(source.read_bytes() == encoded, "refused source is untouched")


def sample_matrix(exe: Path, root: Path) -> None:
    """Independent exact samples span all lossless container alternatives."""
    for path in sorted(DATA.glob("d*-c*-t*-p*-b*-l*.tif")):
        decoded, response, _ = process(exe, root, path.read_bytes())
        depth = response["source_decoding"]["precision"]
        channels = response["source_decoding"]["samples"]
        if channels in (2, RGBA_CHANNELS):
            expect(response["conversion"]["alpha_flattened_pixels"] > 0, "alpha is interpreted")
            continue
        expected = tuple(
            tuple(
                (x * 19 + y * 37 + c * 53) % 256
                if depth == BYTE_DEPTH
                else (x * 1493 + y * 2039 + c * 8191 + 1) % 65536
                for c in range(channels)
            )
            for y in range(17)
            for x in range(19)
        )
        expect(decoded.pixels == expected and decoded.depth == depth, f"exact {path.name} samples")
    for path in sorted(DATA.glob("compression-*.tif")):
        decoded, response, _ = process(exe, root, path.read_bytes())
        depth = response["source_decoding"]["precision"]
        expected = tuple(
            tuple(
                (x * 19 + y * 37 + c * 53) % 256
                if depth == BYTE_DEPTH
                else (x * 1493 + y * 2039 + c * 8191 + 1) % 65536
                for c in range(3)
            )
            for y in range(17)
            for x in range(19)
        )
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
    expected = tuple((palette[i], palette[255 - i], palette[255 - i]) for i in (0, 1, 128, 255))
    expect(
        decoded.depth == WORD_DEPTH and decoded.pixels == expected,
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
            png = root / f"alpha-reference-{depth}-{matte}.png"
            png.write_bytes(straight.encoded())
            output = root / f"alpha-reference-{depth}-{matte}"
            call_json(
                exe, ["process", str(png), "--out-dir", str(output), "--alpha", matte, "--json"]
            )
            expect(
                decoded.pixels == decode_output((output / "result.png").read_bytes()).pixels,
                "unassociate first, then existing linear-light compositing",
            )
            expect(
                response["conversion"]["alpha_flattened_pixels"] == FLATTENED_PIXELS,
                "flatten count",
            )
        refused(
            exe, root, source.encoded(), "E_INPUT", "--alpha", "reject", publication="not_published"
        )
        white = TiffFixture(
            2, 1, ((0, alpha_sample), (alpha_sample, alpha_sample)), photo=0, alpha=1, depth=depth
        )
        decoded, _, _ = process(exe, root, white.encoded(), "--alpha", "black")
        expect(
            decoded.pixels[0][0] > decoded.pixels[1][0],
            "MINISWHITE inversion follows unassociation",
        )
        invalid_alpha = TiffFixture(1, 1, ((maximum, alpha_sample),), photo=1, alpha=1, depth=depth)
        refused(exe, root, invalid_alpha.encoded(), "E_INPUT", publication="not_published")
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
            response["source_decoding"]["orientation"] == orientation, "actual orientation recorded"
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
        response["conversion"]["resolution"] == {"x_ppm": 5000, "y_ppm": 10000},
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
    expect(response["conversion"]["profile_decision"] == "icc", "embedded TIFF ICC applies")
    overridden, response, _ = process(exe, root, source.encoded(fields), "--profile-policy", "srgb")
    expect(
        decoded.pixels != overridden.pixels
        and response["conversion"]["profile_decision"] == "overridden_srgb",
        "profile override is deliberate and observable",
    )
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
    _, response, _ = process(
        exe,
        root,
        lit.encoded(),
        "--illumination",
        "surface",
        "--background-cell",
        "8",
        "--denoise",
        "nlm",
        "--nlm-patch",
        "3",
        "--nlm-search",
        "7",
    )
    expect(
        response["illumination"]["complete"] and response["denoising"]["complete"],
        "TIFF composes real I01 and D01 with verified PNG publication",
    )
    refused(exe, root, source.encoded(), "E_NOT_IMPLEMENTED", "--output-mode", "bw")


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


def main() -> None:
    """Run the actual executable boundary suite."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-tiff-") as directory:
        root = Path(directory)
        sample_matrix(exe, root)
        palette_alpha_orientation(exe, root)
        metadata_and_pipeline(exe, root)
        invalid_domains(exe, root)
        # Every process uses the shared staged reader; exercise its public read-only route once.
        call_json(exe, ["verify", str(min(root.glob("bundle-*"))), "--json"])
    print("TIFF independent samples, interpretation, pipeline and refusals passed")


if __name__ == "__main__":
    main()
