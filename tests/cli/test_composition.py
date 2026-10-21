# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real numerical composition checked against direct independent mathematical references."""

from __future__ import annotations

import math
import struct
import sys
import tempfile
import zlib
from pathlib import Path

from composition_cases import process
from composition_reference import (
    Nlm,
    final_codes,
    linear,
    reference_controls,
    weight_entries,
    weighted_l1,
)
from continuous_fixtures import (
    GRAY_ALPHA,
    RGBA,
    Fixture,
    decode_output,
    decoder_controls,
    exif,
    gamma_profile,
    profile_from_output,
)
from icc_reference import fixture_controls
from jpeg_io import marker, prepend
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode

WIDTH = 17
HEIGHT = 9
WORD_MAX = 65535
DATA = Path(__file__).resolve().parents[1] / "fixtures/jpeg"


def nlm_extremes(exe: Path, root: Path) -> None:
    """Check v2 exact-distance extrema at independent corners and an interior sample."""
    width, height = 45, 43
    fixture = Fixture(
        width,
        height,
        tuple((12000 + (i * 331) % 30000,) for i in range(width * height)),
        depth=16,
        metadata=((b"gAMA", struct.pack(">I", 100000)),),
    )
    source = root / "nlm-extremes.png"
    source.write_bytes(fixture.encoded())
    rgb = linear(fixture, 0, 100000)
    perceptual = [transfer_encode(p[0]) for p in rgb]
    q = [math.floor(WORD_MAX * f + 0.5) for f in perceptual]
    positions = [(0, 0), (width - 1, height - 1), (width // 2, height // 2)]
    for strength in (0.1, 25):
        settings = Nlm(h=strength, patch=15, search=41)
        filtered = weighted_l1(q, width, height, settings, positions=positions)
        output = root / f"nlm-extremes-{strength}"
        response = call_json(
            exe,
            [
                "process",
                str(source),
                "--out-dir",
                str(output),
                "--json",
                "--denoise",
                "nlm",
                "--nlm-h",
                str(strength),
                "--nlm-patch",
                "15",
                "--nlm-search",
                "41",
                "--denoise-blend",
                str(settings.blend),
            ],
        )
        expect(
            response["record"]["execution"]["denoising"]["weight_table_entries"]
            == weight_entries(settings),
            "actual binary64 exact-distance table cardinality",
        )
        call_json(exe, ["verify", str(output), "--json"])
        actual = decode_output((output / "result.png").read_bytes())
        for (x, y), qd in zip(positions, filtered, strict=True):
            index = y * width + x
            f = perceptual[index]
            candidate = min(1.0, max(0.0, f + (qd - q[index]) / WORD_MAX))
            blended = (1 - settings.blend) * f + settings.blend * candidate
            pixel = rgb[index][0] if qd == q[index] or blended == f else transfer_decode(blended)
            expect(
                actual.pixels[index] == final_codes([rgb[index]], [pixel], 16, gray=True)[0],
                "maximal direct NLM window and boundary reflection",
            )


def icc_composition(exe: Path, root: Path, fixture: Fixture) -> None:
    """Known gamma-two ICC ingress keeps its documented native precision boundary."""
    root = root / f"icc-{fixture.color}"
    root.mkdir()
    source = root / "profile-source.png"
    source.write_bytes(fixture.encoded())
    profile_path = root / "profile-output"
    call_json(exe, ["process", str(source), "--out-dir", str(profile_path), "--json"])
    profile = gamma_profile(profile_from_output((profile_path / "result.png").read_bytes()), 2.0)
    fixture_controls(profile)
    described = Fixture(
        fixture.width,
        fixture.height,
        fixture.pixels,
        fixture.color,
        fixture.depth,
        metadata=(
            (b"iCCP", b"reference\0\0" + zlib.compress(profile)),
            (b"gAMA", struct.pack(">I", 50000)),
            (b"eXIf", exif(6)),
        ),
    )
    scoped = root / "icc"
    scoped.mkdir()
    process(exe, scoped, described, described.encoded(), 6)


def main() -> int:
    """Run scoped independent composition fixtures against the supplied production executable."""
    decoder_controls()
    reference_controls()
    exe = Path(sys.argv[1]).resolve()
    rgba = Fixture(
        WIDTH,
        HEIGHT,
        tuple(
            (
                8000 + (i * 331) % 24000,
                12000 + (i * 173) % 20000,
                3000 + (i * 719) % 30000,
                (0, 32768, 45877, WORD_MAX)[i % 4],
            )
            for i in range(WIDTH * HEIGHT)
        ),
        RGBA,
        16,
    )
    gray = Fixture(
        WIDTH,
        HEIGHT,
        tuple((10000 + (i * 419) % 30000, (32768, WORD_MAX)[i % 2]) for i in range(WIDTH * HEIGHT)),
        GRAY_ALPHA,
        16,
    )
    with tempfile.TemporaryDirectory(prefix="docenhance-composition-") as directory:
        root = Path(directory)
        rgba8 = Fixture(WIDTH, HEIGHT, tuple(tuple(c // 257 for c in p) for p in rgba.pixels), RGBA)
        srgb = Fixture(WIDTH, HEIGHT, rgba.pixels, RGBA, 16, metadata=((b"sRGB", b"\0"),))
        for name, fixture in (
            ("linear-rgba16", rgba),
            ("linear-gray-alpha16", gray),
            ("linear-rgba8", rgba8),
            ("srgb-rgba16", srgb),
        ):
            for orientation in range(1, 9):
                scoped = root / f"png-{name}-{orientation}"
                scoped.mkdir()
                described = Fixture(
                    fixture.width,
                    fixture.height,
                    fixture.pixels,
                    fixture.color,
                    fixture.depth,
                    orientation % 2 == 0,
                    (fixture.metadata or ((b"gAMA", struct.pack(">I", 100000)),))
                    + ((b"eXIf", exif(orientation)),),
                )
                process(exe, scoped, described, described.encoded(), orientation)
        nlm_extremes(exe, root)
        icc_composition(exe, root, gray)
        icc_composition(exe, root, rgba)
        pixels = tuple(
            (48 + ((x // 8 * 29 + y // 8 * 43) % 160),) for y in range(HEIGHT) for x in range(WIDTH)
        )
        fixture = Fixture(WIDTH, HEIGHT, pixels)
        for kind in ("baseline", "progressive"):
            scoped = root / kind
            scoped.mkdir()
            encoded = prepend(
                (DATA / f"gray-1x1-{kind}.jpg").read_bytes(), marker(225, b"Exif\0\0" + exif(6))
            )
            process(exe, scoped, fixture, encoded, 6)
    print("PASS: independent PNG/JPEG composition and numerical negative controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
