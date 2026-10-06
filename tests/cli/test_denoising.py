# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""D01 executable composition, protection, fidelity and complete current record refusal."""

from __future__ import annotations

import json
import statistics
import sys
import tempfile
from pathlib import Path
from typing import Any

from continuous_fixtures import RGB, Fixture, decode_output, exif
from test_cli import call_json, expect
from test_continuous import transfer_decode, transfer_encode

DATA = Path(__file__).resolve().parents[1] / "fixtures/jpeg"
SIDE = 64
RECORD_VERSION = 5
ASSESSMENT_THRESHOLD = 100
COMPOSITION_TOLERANCE = 3
OPTIONS = ["--denoise", "nlm", "--nlm-patch", "3", "--nlm-search", "7"]


def process(
    exe: Path, root: Path, source: Path, options: list[str]
) -> tuple[Fixture, dict[str, Any], Path]:
    """Process to a fresh bundle, validating response and complete standalone verification."""
    out = root / f"out-{len(list(root.iterdir()))}"
    response = call_json(exe, ["process", str(source), "--out-dir", str(out), *options, "--json"])
    call_json(exe, ["verify", str(out), "--json"])
    record = json.loads((out / "run.json").read_text())
    expect(record["record"]["version"] == RECORD_VERSION, "new record version")
    expect(
        record["execution"]["denoising"] == response["denoising"],
        "response and record observations agree",
    )
    expect(response["denoising"]["complete"], "completed D01")
    return decode_output((out / "result.png").read_bytes()), response, out


def source_fixture(root: Path, fixture: Fixture) -> Path:
    """Create an independent source fixture with a unique name."""
    path = root / f"source-{len(list(root.iterdir()))}.png"
    path.write_bytes(fixture.encoded())
    return path


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
    expect(report["denoising"]["status"] == "applied", "working-space modification reported")
    expect(report["denoising"]["native_calls"] == 1, "verification does not rerun native denoising")
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
            response["denoising"]["protected_samples"] == sum(bool(v[0]) for v in mask_values),
            "mask count",
        )
        zero, report, _ = process(exe, root, source, [*OPTIONS, "--denoise-blend", "0"])
        expect(zero.pixels == baseline.pixels, "zero blend exact")
        expect(report["denoising"]["reason"] == "zero_blend", "explicit no-op reason")
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
    expect(report["denoising"]["status"] == "no_change", "no correction status")
    mask = source_fixture(root, Fixture(SIDE, SIDE, ((255,),) * (SIDE * SIDE)))
    _, report, _ = process(exe, root, constant, [*OPTIONS, "--protect-mask", str(mask)])
    expect(report["denoising"]["native_calls"] == 0, "fully protected skips native work")


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
    """Compare illumination followed by D01 against a serialized illumination intermediate."""
    pixels = tuple(
        (round(65535 * (0.35 + 0.5 * (x / (SIDE - 1))) + (y % 3) * 100),)
        for y in range(SIDE)
        for x in range(SIDE)
    )
    source = source_fixture(root, Fixture(SIDE, SIDE, pixels, depth=16))
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
    combined, report, _ = process(exe, root, source, [*illumination, *OPTIONS])
    _, _, intermediate = process(exe, root, source, illumination)
    sequential, _, _ = process(exe, root, intermediate / "result.png", OPTIONS)
    stale, _, _ = process(exe, root, source, OPTIONS)
    expect(combined.pixels != stale.pixels, "D01 sees illumination rather than stale input")
    expect(
        max(abs(a[0] - b[0]) for a, b in zip(combined.pixels, sequential.pixels, strict=True))
        <= COMPOSITION_TOLERANCE,
        "composition agrees within intermediate quantization error",
    )
    expect(report["illumination"]["complete"], "illumination completed before D01")
    for mode in ("preserve", "gray"):
        source = root / f"jpeg-{mode}.jpg"
        source.write_bytes((DATA / "gray-document-progressive.jpeg").read_bytes())
        process(exe, root, source, [*OPTIONS, "--output-mode", mode])


def record_validation(exe: Path, root: Path) -> None:
    """Only the current closed record is admitted; old versions and malformed claims are refused."""
    source = source_fixture(root, Fixture(SIDE, SIDE, ((210,),) * (SIDE * SIDE)))
    _, _, output = process(exe, root, source, OPTIONS)
    path = output / "run.json"
    record = json.loads(path.read_text())
    for version in (1, 2, 3, 4, 6):
        old = json.loads(json.dumps(record))
        old["record"]["version"] = version
        path.write_text(json.dumps(old))
        call_json(exe, ["verify", str(output), "--json"], 3)
    for field, value in (
        ("native_h", 100),
        ("complete", False),
        ("tile_width", 512),
        ("changed_samples", 1),
        ("preparation_charge_peak", (1 << 30) + 1),
    ):
        bad = json.loads(json.dumps(record))
        bad["execution"]["denoising"][field] = value
        path.write_text(json.dumps(bad))
        call_json(exe, ["verify", str(output), "--json"], 3)
    path.write_text(json.dumps(record))


def rejection(exe: Path, root: Path) -> None:
    """Unsupported combinations, malformed presence, sources and masks cannot disappear."""
    source = source_fixture(root, Fixture(3, 3, ((200,),) * 9))
    for options in (
        ["--nlm-h", "3"],
        ["--denoise", "off", "--denoise-blend", "0"],
        ["--denoise", "tvl1"],
        ["--denoise", "nlm", "--nlm-h", "nan"],
        ["--denoise", "nlm", "--nlm-patch", "4"],
        ["--denoise", "nlm", "--nlm-search", "3"],
        ["--output-mode", "bw", "--denoise", "off"],
    ):
        call_json(
            exe, ["process", str(source), "--out-dir", str(root / "refused"), *options, "--json"], 2
        )
    result = call_json(
        exe, ["process", str(source), "--out-dir", str(root / "small"), *OPTIONS, "--json"], 4
    )
    expect(result["denoising"]["status"] == "failed", "shape refusal stage")
    process(exe, root, source, [*OPTIONS, "--denoise-blend", "0"])
    mask = root / "bad-mask.png"
    mask.write_bytes(b"invalid")
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
        record_validation(exe, root)
        rejection(exe, root)


if __name__ == "__main__":
    main()
