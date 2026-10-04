# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real numerical composition checked against direct independent mathematical references."""

from __future__ import annotations

import json
import math
import struct
import sys
import tempfile
import zlib
from pathlib import Path

from composition_reference import (
    Nlm,
    denoise,
    float32,
    illuminate,
    linear,
    luminance,
    native_l1,
    quantize,
    roundtrip,
    transport,
)
from continuous_fixtures import (
    GRAY,
    GRAY_ALPHA,
    RGBA,
    Fixture,
    decode_output,
    exif,
    gamma_profile,
    profile_from_output,
)
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode
from test_jpeg import marker, oriented_fixture, prepend

WIDTH = 17
HEIGHT = 9
WORD_MAX = 65535
ICC_CODE_TOLERANCE = 1
ILLUMINATION = [
    "--illumination",
    "surface",
    "--background-cell",
    "64",
    "--background-target",
    "0.7",
    "--background-strength",
    "0.6",
    "--background-max-gain",
    "4",
    "--background-smooth",
    "2",
]
DENOISING = [
    "--denoise",
    "nlm",
    "--nlm-h",
    "25",
    "--nlm-patch",
    "3",
    "--nlm-search",
    "7",
    "--denoise-blend",
    "0.73",
]
DATA = Path(__file__).resolve().parents[1] / "fixtures/jpeg"


def process(exe: Path, root: Path, fixture: Fixture, encoded: bytes, orientation: int) -> None:
    """Check every integer and once-only observation through real commit and verify."""
    source = root / "source"
    source.write_bytes(encoded)
    oriented = oriented_fixture(fixture, orientation)
    protected = [i % 7 == 0 for i in range(len(oriented.pixels))]
    mask = root / "mask.png"
    mask.write_bytes(
        Fixture(oriented.width, oriented.height, tuple((int(p),) for p in protected)).encoded()
    )
    gamma = next(
        (int.from_bytes(data, "big") for tag, data in fixture.metadata if tag == b"gAMA"),
        None,
    )
    native_profile = any(tag == b"iCCP" for tag, _ in fixture.metadata)
    interpreted = linear(oriented, 0, gamma)
    entering = illuminate(interpreted, oriented.width, oriented.height, protected)
    final, q, qd = denoise(entering, oriented.width, oriented.height, protected, Nlm())
    variants: tuple[tuple[int, str], ...] = ((16, "preserve"), (16, "gray"), (8, "preserve"))
    if orientation != 1 and fixture.color in (RGBA, GRAY_ALPHA) and not native_profile:
        variants = ((16, "preserve"),)
    for depth, mode in variants:
        output = root / f"out-{depth}-{mode}"
        response = call_json(
            exe,
            [
                "process",
                str(source),
                "--out-dir",
                str(output),
                "--json",
                *ILLUMINATION,
                *DENOISING,
                "--protect-mask",
                str(mask),
                "--alpha",
                "black",
                "--bit-depth",
                str(depth),
                "--output-mode",
                mode,
            ],
        )
        call_json(exe, ["verify", str(output), "--json"])
        actual = decode_output((output / "result.png").read_bytes())
        expect(
            (actual.width, actual.height, actual.depth) == (oriented.width, oriented.height, depth),
            "oriented output extent and depth",
        )
        gray = mode == "gray" or fixture.color in (GRAY_ALPHA, GRAY)
        expected = quantize(final, depth, gray=gray)
        # Only the known gamma-two ICC fixture allows its native float32 boundary.
        tolerance = ICC_CODE_TOLERANCE if native_profile else 0
        differences = [
            (i, a, b)
            for i, (a, b) in enumerate(zip(actual.pixels, expected, strict=True))
            if any(abs(c - d) > tolerance for c, d in zip(a, b, strict=True))
        ]
        expect(
            not differences,
            f"composed codes {fixture.color}/{orientation}/{depth}/{mode}: {differences[:5]}",
        )
        baseline = quantize(interpreted, depth, gray=gray)
        if native_profile:
            baseline_path = root / f"baseline-{depth}-{mode}"
            call_json(
                exe,
                [
                    "process",
                    str(source),
                    "--out-dir",
                    str(baseline_path),
                    "--alpha",
                    "black",
                    "--bit-depth",
                    str(depth),
                    "--output-mode",
                    mode,
                    "--json",
                ],
            )
            baseline = decode_output((baseline_path / "result.png").read_bytes()).pixels
            expect(
                response["conversion"]["profile_decision"] == "icc",
                "native ICC interpretation selected",
            )
        expect(
            all(actual.pixels[i] == baseline[i] for i, p in enumerate(protected) if p),
            "protected final identity",
        )
        d01 = response["denoising"]
        expect(
            d01["native_calls"] == 1 and d01["complete"],
            "one native preparation, no verification rerun",
        )
        expect(
            d01["evaluated_samples"] == sum(not p for p in protected),
            "D01 observations counted once",
        )
        expect(
            d01["corrected_samples"]
            == sum(q[i] != qd[i] for i, p in enumerate(protected) if not p),
            "independent native correction count",
        )
        expect(
            d01["changed_samples"]
            == sum(final[i] != entering[i] for i, p in enumerate(protected) if not p),
            "independent working change count",
        )
        i01 = response["illumination"]
        expect(
            i01["complete"]
            and i01["application"]["evaluated_samples"] == sum(not p for p in protected),
            "I01 observations counted once",
        )
        expect(
            response["conversion"]["alpha_flattened_pixels"]
            == (
                sum(p[-1] != (1 << fixture.depth) - 1 for p in fixture.pixels)
                if fixture.color in (RGBA, GRAY_ALPHA)
                else 0
            ),
            "alpha counted once",
        )
        record = json.loads((output / "run.json").read_bytes())
        expect(
            record["execution"]["illumination"] == i01 and record["execution"]["denoising"] == d01,
            "response/record stage agreement",
        )
    expect(source.read_bytes() == encoded, "source identity preserved")
    if fixture.color == RGBA and orientation == 1:
        challenge_controls(interpreted, entering, final, protected, oriented)


def challenge_controls(
    interpreted: list[tuple[float, ...]],
    entering: list[tuple[float, ...]],
    final: list[tuple[float, ...]],
    protected: list[bool],
    fixture: Fixture,
) -> None:
    """Fixtures must discriminate wrong order, early 8/16-bit rounding and masked context."""
    expected = quantize(final, 16, gray=False)
    for depth in (8, 16):
        early, _, _ = denoise(
            roundtrip(entering, depth), fixture.width, fixture.height, protected, Nlm()
        )
        expect(
            quantize(early, 16, gray=False) != expected,
            f"detect premature {depth}-bit quantization",
        )
    first, _, _ = denoise(interpreted, fixture.width, fixture.height, protected, Nlm())
    swapped = illuminate(first, fixture.width, fixture.height, protected)
    expect(quantize(swapped, 16, gray=False) != expected, "detect D01 before I01")
    _, q, correct = denoise(entering, fixture.width, fixture.height, protected, Nlm())
    masked = [0 if protected[i] else value for i, value in enumerate(q)]
    bad_context = native_l1(masked, fixture.width, fixture.height, Nlm())
    expect(
        any(
            a != b
            for i, (a, b) in enumerate(zip(correct, bad_context, strict=True))
            if not protected[i]
        ),
        "protected neighbors remain context",
    )


def native_extremes(exe: Path, root: Path) -> None:
    """Check extreme native windows/strength at direct-reference corners and an interior sample."""
    width, height = 45, 43
    fixture = Fixture(
        width,
        height,
        tuple((12000 + (i * 331) % 30000,) for i in range(width * height)),
        depth=16,
        metadata=((b"gAMA", struct.pack(">I", 100000)),),
    )
    source = root / "native-extremes.png"
    source.write_bytes(fixture.encoded())
    rgb = linear(fixture, 0, 100000)
    perceptual = [transfer_encode(luminance(p)) for p in rgb]
    q = [math.floor(WORD_MAX * f + 0.5) for f in perceptual]
    positions = [(0, 0), (width - 1, height - 1), (width // 2, height // 2)]
    for strength in (0.1, 25):
        settings = Nlm(h=strength, patch=15, search=41)
        filtered = native_l1(q, width, height, settings, positions=positions)
        output = root / f"native-extremes-{strength}"
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
            response["denoising"]["native_h"] == float32(257 * float32(strength)),
            "actual binary32 native strength",
        )
        call_json(exe, ["verify", str(output), "--json"])
        actual = decode_output((output / "result.png").read_bytes())
        for (x, y), qd in zip(positions, filtered, strict=True):
            index = y * width + x
            f = perceptual[index]
            candidate = min(1.0, max(0.0, f + (qd - q[index]) / WORD_MAX))
            blended = (1 - settings.blend) * f + settings.blend * candidate
            pixel = (
                rgb[index]
                if qd == q[index] or blended == f
                else transport(rgb[index], transfer_decode(blended))
            )
            expect(
                actual.pixels[index] == quantize([pixel], 16, gray=True)[0],
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
        native_extremes(exe, root)
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
