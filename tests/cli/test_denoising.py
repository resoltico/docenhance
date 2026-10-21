# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""D01 executable composition, protection, fidelity and complete current record refusal."""

from __future__ import annotations

import statistics
import sys
import tempfile
from pathlib import Path

from codec_samples import oriented_fixture
from composition_reference import Nlm, denoise, final_codes, linear, terminal_fixed, weight_entries
from continuous_fixtures import RGB, Fixture, exif
from denoising_admission import OPTIONS, SIDE, process, record_validation, source_fixture
from illumination_reference import Plane, surface
from morphology_reference import background
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode

DATA = Path(__file__).resolve().parents[1] / "fixtures/jpeg"
ASSESSMENT_THRESHOLD = 100
COMPOSITION_TOLERANCE = 3


def quality(exe: Path, root: Path) -> None:
    """Fixed-seed noise and punctuation have separately assessed acceptance criteria."""
    state = 1729
    values = []
    for _ in range(SIDE * SIDE):
        # Independent fixed-seed integer generator: six uniforms approximate white noise.
        noise = 0
        for _ in range(6):
            state = (1664525 * state + 1013904223) % (1 << 32)
            noise += (state >> 24) % 11
        values.append(210 + round((noise - 30) / 2))
    marks = [(8, 8), (20, 8), (32, 8)]
    for x, y in marks:
        values[y * SIDE + x] = 30
    fixture = Fixture(SIDE, SIDE, tuple((v,) for v in values))
    source = source_fixture(root, fixture)
    output, report, _ = process(exe, root, source, OPTIONS)
    region = [y * SIDE + x for y in range(32, 56) for x in range(8, 56)]
    before = statistics.pvariance(values[i] for i in region)
    after = statistics.pvariance(output.pixels[i][0] for i in region)
    expect(after <= 0.9 * before, f"P04 variance reduction: {before} -> {after}")
    for x, y in marks:
        expect(
            output.pixels[y * SIDE + x][0] < ASSESSMENT_THRESHOLD,
            "disconnected punctuation retained",
        )
        for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            expect(
                output.pixels[(y + dy) * SIDE + x + dx][0] >= ASSESSMENT_THRESHOLD,
                "punctuation remains disconnected",
            )
    expect(
        report["record"]["execution"]["denoising"]["disposition"] == "applied",
        "working-space modification reported",
    )
    expect(
        report["record"]["execution"]["denoising"]["counts"]["evaluated"] == SIDE * SIDE
        and report["record"]["execution"]["denoising"]["weight_table_entries"]
        == weight_entries(Nlm(h=3)),
        "actual scalar evaluations and exact-distance table are observed once",
    )
    print(f"P04 synthetic variance: {before:.6f} -> {after:.6f}; punctuation: 3/3")


def protection(exe: Path, root: Path) -> None:
    """All depth/category/orientation choices preserve protected entering samples exactly."""
    for depth in (8, 16):
        maximum = (1 << depth) - 1
        values = tuple(((i * 331) % maximum,) for i in range(SIDE * SIDE))
        fixture = Fixture(SIDE, SIDE, values, depth=depth)
        source = source_fixture(root, fixture)
        baseline, _, _ = process(exe, root, source, [])
        mask_values = tuple((255 if i % 5 == 0 else 0,) for i in range(SIDE * SIDE))
        mask = source_fixture(root, Fixture(SIDE, SIDE, mask_values))
        output, response, _ = process(exe, root, source, [*OPTIONS, "--protect-mask", str(mask)])
        for i, value in enumerate(mask_values):
            if value[0]:
                expect(output.pixels[i] == baseline.pixels[i], "protected output exact")
        expect(
            response["record"]["execution"]["denoising"]["counts"]["protected"]
            == sum(bool(v[0]) for v in mask_values),
            "mask count",
        )
        zero, report, _ = process(exe, root, source, [*OPTIONS, "--denoise-blend", "0"])
        expect(zero.pixels == baseline.pixels, "zero blend exact")
        expect(
            report["record"]["execution"]["denoising"]["reason"] == "zero_blend",
            "explicit no-op reason",
        )
    color = Fixture(
        SIDE,
        SIDE,
        tuple((200, 123 + i % 8, 77) for i in range(SIDE * SIDE)),
        color=RGB,
        metadata=((b"eXIf", exif(6)),),
    )
    source = source_fixture(root, color)
    process(exe, root, source, OPTIONS)
    process(exe, root, source, [*OPTIONS, "--output-mode", "gray"])
    constant = source_fixture(root, Fixture(SIDE, SIDE, ((210,),) * (SIDE * SIDE)))
    output, report, _ = process(exe, root, constant, OPTIONS)
    expect(output.pixels == ((210,),) * (SIDE * SIDE), "already-good constant page exact")
    expect(
        report["record"]["execution"]["denoising"]["disposition"] == "no_change",
        "no correction status",
    )
    mask = source_fixture(root, Fixture(SIDE, SIDE, ((255,),) * (SIDE * SIDE)))
    _, report, _ = process(exe, root, constant, [*OPTIONS, "--protect-mask", str(mask)])
    facts = report["record"]["execution"]["denoising"]
    expect(
        facts["reason"] == "no_eligible_samples"
        and facts["counts"]["evaluated"] == 0
        and facts["weight_table_entries"] == 0,
        "fully protected bypass creates no table or evaluations",
    )


def fidelity(exe: Path, root: Path) -> None:
    """Assess weak strokes and colored annotations independently of background smoothing."""
    width, height = 64, 48
    values = [(210 + ((x * 13 + y * 7) % 7) - 3,) * 3 for y in range(height) for x in range(width)]
    weak = [20 * width + x for x in range(16, 32)]
    annotations = [30 * width + x for x in range(16, 32)]
    for index in weak:
        values[index] = (194, 194, 194)
    for index in annotations:
        values[index] = (180, 40, 40)
    source = source_fixture(root, Fixture(width, height, tuple(values), color=RGB))
    output, _, _ = process(exe, root, source, OPTIONS)

    def luminance(pixel: tuple[int, ...]) -> float:
        linear = [transfer_decode(value / 255) for value in pixel]
        return transfer_encode(0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2])

    for index in weak:
        expect(
            luminance(output.pixels[index]) < luminance(output.pixels[index + width]),
            "defined weak stroke remains darker than its adjacent paper",
        )
    for index in annotations:
        expect(
            output.pixels[index][0] > output.pixels[index][1],
            "defined colored annotation retains red ordering",
        )
    before = statistics.mean(luminance(values[i + width]) - luminance(values[i]) for i in weak)
    after = statistics.mean(
        luminance(output.pixels[i + width]) - luminance(output.pixels[i]) for i in weak
    )
    print(f"Weak-stroke perceptual contrast: {before:.6f} -> {after:.6f}; red annotations: 16/16")


def orientations(exe: Path, root: Path) -> None:
    """All metadata orientations use protection in the actual entering coordinate frame."""
    width, height = 17, 11
    for orientation in range(1, 9):
        fixture = Fixture(
            width,
            height,
            tuple((80 + i % 120,) for i in range(width * height)),
            metadata=((b"eXIf", exif(orientation)),),
        )
        source = source_fixture(root, fixture)
        baseline, _, _ = process(exe, root, source, [])
        mask_values = tuple((255 if i % 3 == 0 else 0,) for i in range(width * height))
        mask = source_fixture(root, Fixture(baseline.width, baseline.height, mask_values))
        output, _, _ = process(exe, root, source, [*OPTIONS, "--protect-mask", str(mask)])
        expect(
            all(
                output.pixels[i] == baseline.pixels[i]
                for i, value in enumerate(mask_values)
                if value[0]
            ),
            "oriented protected pixels are exact",
        )


def composition(exe: Path, root: Path, selection: str = "surface") -> None:
    """Independent floating I01/I02→D01 reference retains Y until the final quantizer."""
    pixels = tuple(
        (round(65535 * (0.35 + 0.5 * (x / (SIDE - 1))) + (y % 3) * 100),)
        for y in range(SIDE)
        for x in range(SIDE)
    )
    fixture = Fixture(SIDE, SIDE, pixels, depth=16)
    source = source_fixture(root, fixture)
    illumination = [
        "--illumination",
        selection,
        "--background-cell" if selection == "surface" else "--background-radius",
        "8",
        "--background-target",
        "0.9",
        "--bit-depth",
        "16",
    ]
    original = linear(fixture, 1, None)
    values = [p[0] for p in original]
    field = (
        surface(Plane(values, SIDE, SIDE, [False] * len(values)), 8, 1)
        if selection == "surface"
        else background(values, SIDE, SIDE, 8)
    )
    illuminated = [
        min(1, y * min(2, max(1, 0.9 / max(b, 0.02)))) for y, b in zip(values, field, strict=True)
    ]
    filtered, _, _ = denoise(
        illuminated, SIDE, SIDE, [False] * len(values), Nlm(h=3, patch=3, search=7, blend=0.5)
    )
    expected = final_codes(original, filtered, 16, gray=True)
    combined, report, _ = process(exe, root, source, [*illumination, *OPTIONS])
    stale, _, _ = process(exe, root, source, OPTIONS)
    expect(combined.pixels != stale.pixels, "D01 sees illumination rather than stale input")
    expect(
        max(abs(a[0] - b[0]) for a, b in zip(combined.pixels, expected, strict=True))
        <= COMPOSITION_TOLERANCE,
        "independent full floating composition",
    )
    facts = report["record"]["execution"]
    expect(
        facts["illumination"]["counts"]["evaluated"] == len(values)
        and facts["denoising"]["counts"]["evaluated"] == len(values),
        "illumination completes eagerly and each stage is observed once",
    )
    for mode in ("preserve", "gray"):
        source = root / f"jpeg-{mode}.jpg"
        source.write_bytes((DATA / "gray-document-progressive.jpeg").read_bytes())
        process(exe, root, source, [*OPTIONS, "--output-mode", mode])


def binary_composition(exe: Path, root: Path) -> None:
    """NLM modifies Y before terminal Fixed; all eight orientations retain mask coordinates."""
    width, height = 17, 11
    for orientation in range(1, 9):
        fixture = Fixture(
            width,
            height,
            tuple((55 + (i * 37) % 170,) for i in range(width * height)),
            metadata=((b"eXIf", exif(orientation)),),
        )
        source = source_fixture(root, fixture)
        oriented = oriented_fixture(fixture, orientation)
        protected = [i % 3 == 0 for i in range(len(oriented.pixels))]
        mask = source_fixture(
            root,
            Fixture(oriented.width, oriented.height, tuple((int(keep),) for keep in protected)),
        )
        original = linear(oriented, 1, None)
        filtered, _, _ = denoise(
            [p[0] for p in original],
            oriented.width,
            oriented.height,
            protected,
            Nlm(h=3, patch=3, search=7, blend=0.5),
        )
        expected = terminal_fixed(filtered)
        output, response, _ = process(
            exe,
            root,
            source,
            [
                *OPTIONS,
                "--protect-mask",
                str(mask),
                "--output-mode",
                "bw",
                "--binarize",
                "fixed",
                "--fixed-threshold",
                "0.5",
            ],
        )
        expect(
            (output.width, output.height, output.depth) == (oriented.width, oriented.height, 1)
            and output.pixels == expected,
            "independent oriented protected NLM terminal logical bits",
        )
        facts = response["record"]["execution"]["binarization"]
        expect(
            facts["black_samples"] == sum(p == (0,) for p in expected)
            and facts["white_samples"] == sum(p == (1,) for p in expected),
            "every pixel including protected destinations is classified",
        )


def rejection(exe: Path, root: Path) -> None:
    """Unsupported combinations, malformed presence, sources and masks cannot disappear."""
    source = source_fixture(root, Fixture(3, 3, ((200,),) * 9))
    before = set(root.iterdir())
    for options in (
        ["--nlm-h", "3"],
        ["--denoise", "off", "--denoise-blend", "0"],
        ["--denoise", "nlm", "--tv-lambda", "1"],
        ["--denoise", "nlm", "--nlm-h", "nan"],
        ["--denoise", "nlm", "--nlm-patch", "4"],
        ["--denoise", "nlm", "--nlm-search", "3"],
    ):
        call_json(
            exe, ["process", str(source), "--out-dir", str(root / "refused"), *options, "--json"], 2
        )
    result = call_json(
        exe, ["process", str(source), "--out-dir", str(root / "small"), *OPTIONS, "--json"], 4
    )
    expect(
        result["failed_stage"] == "denoising"
        and result["completed"] == {}
        and result["publication"] == "not_started",
        "shape refusal has no invented completed facts",
    )
    expect(
        set(root.iterdir()) == before, "grammar and shape refusals leave no final or staging path"
    )
    process(exe, root, source, [*OPTIONS, "--denoise-blend", "0"])
    mask = root / "bad-mask.png"
    mask.write_bytes(b"invalid")
    before = set(root.iterdir())
    call_json(
        exe,
        [
            "process",
            str(source),
            "--out-dir",
            str(root / "bad-mask"),
            *OPTIONS,
            "--denoise-blend",
            "0",
            "--protect-mask",
            str(mask),
            "--json",
        ],
        3,
    )
    expect(set(root.iterdir()) == before, "zero blend still validates mask without publication")


def main() -> None:
    """Run complete real-executable D01 evidence."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        quality(exe, root)
        protection(exe, root)
        fidelity(exe, root)
        orientations(exe, root)
        composition(exe, root)
        binary_composition(exe, root)
        record_validation(exe, root)
        rejection(exe, root)


if __name__ == "__main__":
    main()
