# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Concrete independent composition case, output variants and once-only observations."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING, Any

from codec_samples import oriented_fixture
from composition_reference import (
    Nlm,
    Rgb,
    denoise,
    final_codes,
    illuminate,
    linear,
    luminance,
    roundtrip,
    terminal_fixed,
    weight_entries,
    weighted_l1,
)
from continuous_fixtures import GRAY, GRAY_ALPHA, RGBA, Fixture, decode_output
from test_cli import call_json, expect

if TYPE_CHECKING:
    from pathlib import Path


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


@dataclass(frozen=True)
class CompositionCase:
    """One interpreted source and its independently composed unquantized Y planes."""

    fixture: Fixture
    oriented: Fixture
    original: list[Rgb]
    initial: list[float]
    entering: list[float]
    final: list[float]
    codes: list[int]
    means: list[float]
    protected: list[bool]
    source: Path
    mask: Path
    encoded: bytes
    orientation: int
    native_profile: bool


def prepare_case(root: Path, fixture: Fixture, encoded: bytes, orientation: int) -> CompositionCase:
    """Retain original RGB; I01 and D01 mutate only the independent luminance plane."""
    source = root / "source"
    source.write_bytes(encoded)
    oriented = oriented_fixture(fixture, orientation)
    protected = [i % 7 == 0 for i in range(len(oriented.pixels))]
    mask = root / "mask.png"
    mask.write_bytes(
        Fixture(oriented.width, oriented.height, tuple((int(p),) for p in protected)).encoded()
    )
    gamma = next(
        (int.from_bytes(data, "big") for tag, data in fixture.metadata if tag == b"gAMA"), None
    )
    original = linear(replace(oriented, metadata=fixture.metadata), 0, gamma)
    initial = [p[0] if fixture.color in (GRAY, GRAY_ALPHA) else luminance(p) for p in original]
    entering = illuminate(initial, oriented.width, oriented.height, protected)
    final, codes, means = denoise(entering, oriented.width, oriented.height, protected, Nlm())
    return CompositionCase(
        fixture,
        oriented,
        original,
        initial,
        entering,
        final,
        codes,
        means,
        protected,
        source,
        mask,
        encoded,
        orientation,
        any(tag == b"iCCP" for tag, _ in fixture.metadata),
    )


def options(case: CompositionCase) -> list[str]:
    """The original frozen enhancement argv and source/protection policies."""
    return [*ILLUMINATION, *DENOISING, "--protect-mask", str(case.mask), "--alpha", "black"]


def observations(response: dict[str, Any], case: CompositionCase) -> None:
    """Check full persistent value, source identity and exact once-only work cohorts."""
    record = response["record"]
    execution = record["execution"]
    d01, i01 = execution["denoising"], execution["illumination"]
    eligible = sum(not keep for keep in case.protected)
    expect(
        d01["method"] == "D01" and d01["weight_table_entries"] == weight_entries(Nlm()),
        "actual v2 exact-distance table and completed facts",
    )
    expect(d01["counts"]["evaluated"] == eligible, "D01 observations counted once")
    expect(
        d01["corrected_samples"]
        == sum(case.codes[i] != case.means[i] for i, keep in enumerate(case.protected) if not keep),
        "independent fractional weighted-mean correction count",
    )
    expect(
        d01["counts"]["changed"]
        == sum(
            case.final[i] != case.entering[i] for i, keep in enumerate(case.protected) if not keep
        ),
        "independent working change count",
    )
    expect(
        i01["method"] == "I01" and i01["counts"]["evaluated"] == eligible,
        "I01 observations counted once",
    )
    expected_alpha = (
        sum(p[-1] != (1 << case.fixture.depth) - 1 for p in case.fixture.pixels)
        if case.fixture.color in (RGBA, GRAY_ALPHA)
        else 0
    )
    interpretation = execution["interpretation"]
    expect(interpretation["alpha_composited_pixels"] == expected_alpha, "alpha counted once")
    expect(
        interpretation["source_orientation"] == case.orientation
        and interpretation["rotation"] == 0,
        "G01/G02 are applied once in order",
    )
    expect(
        record["source"]["sha256"] == hashlib.sha256(case.encoded).hexdigest()
        and record["source"]["bytes"] == len(case.encoded),
        "exact acquired source identity",
    )
    expect(
        record["request"]["denoising"]["parameters"]
        == {"h": 25, "patch": 3, "search": 7, "blend": 0.73},
        "frozen D01 argv",
    )


def continuous_variant(exe: Path, root: Path, case: CompositionCase, depth: int, mode: str) -> None:
    """Every final integer is independent; protected identity is separately checked."""
    output = root / f"out-{depth}-{mode}"
    response = call_json(
        exe,
        [
            "process",
            str(case.source),
            "--out-dir",
            str(output),
            "--json",
            *options(case),
            "--bit-depth",
            str(depth),
            "--output-mode",
            mode,
        ],
    )
    call_json(exe, ["verify", str(output), "--json"])
    actual = decode_output((output / "result.png").read_bytes())
    expect(
        (actual.width, actual.height, actual.depth)
        == (case.oriented.width, case.oriented.height, depth),
        "oriented output extent and depth",
    )
    gray = mode == "gray" or case.fixture.color in (GRAY_ALPHA, GRAY)
    expected = final_codes(case.original, case.final, depth, gray=gray)
    tolerance = ICC_CODE_TOLERANCE if case.native_profile else 0
    differences = [
        (i, a, b)
        for i, (a, b) in enumerate(zip(actual.pixels, expected, strict=True))
        if any(abs(c - d) > tolerance for c, d in zip(a, b, strict=True))
    ]
    expect(not differences, f"composed integer codes: {differences[:5]}")
    baseline = final_codes(case.original, case.initial, depth, gray=gray)
    if case.native_profile:
        # This unenhanced native baseline checks exact protected identity only. Every enhanced
        # integer above still has the independent known-gamma oracle and unchanged1code bound.
        baseline_path = root / f"baseline-{depth}-{mode}"
        call_json(
            exe,
            [
                "process",
                str(case.source),
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
            response["record"]["execution"]["interpretation"]["profile_decision"] == "icc",
            "native ICC interpretation selected",
        )
    expect(
        all(actual.pixels[i] == baseline[i] for i, keep in enumerate(case.protected) if keep),
        "protected final identity",
    )
    observations(response, case)
    expect(
        json.loads((output / "run.json").read_bytes()) == response["record"],
        "full persistent/response record agreement",
    )


def binary_variant(exe: Path, root: Path, case: CompositionCase) -> None:
    """The same photometric prefix feeds terminal Q16 and classifies protected pixels too."""
    output = root / "out-bw"
    response = call_json(
        exe,
        [
            "process",
            str(case.source),
            "--out-dir",
            str(output),
            "--json",
            *options(case),
            "--output-mode",
            "bw",
            "--binarize",
            "fixed",
            "--fixed-threshold",
            "0.5",
        ],
    )
    call_json(exe, ["verify", str(output), "--json"])
    actual = decode_output((output / "result.png").read_bytes())
    expect(
        actual.depth == 1 and actual.pixels == terminal_fixed(case.final),
        "independent full-prefix terminal classification",
    )
    facts = response["record"]["execution"]["binarization"]
    expect(
        facts["black_samples"] + facts["white_samples"] == len(case.final),
        "every pixel including protected photometric values is classified",
    )
    observations(response, case)


def challenge_controls(case: CompositionCase) -> None:
    """Discriminate wrong order, early8/16-bit Y rounding and excluded mask context."""
    expected = final_codes(case.original, case.final, 16, gray=False)
    for depth in (8, 16):
        early, _, _ = denoise(
            roundtrip(case.entering, depth),
            case.oriented.width,
            case.oriented.height,
            case.protected,
            Nlm(),
        )
        expect(
            final_codes(case.original, early, 16, gray=False) != expected,
            f"detect premature {depth}-bit quantization",
        )
    first, _, _ = denoise(
        case.initial, case.oriented.width, case.oriented.height, case.protected, Nlm()
    )
    swapped = illuminate(first, case.oriented.width, case.oriented.height, case.protected)
    expect(final_codes(case.original, swapped, 16, gray=False) != expected, "detect D01 before I01")
    masked = [0 if case.protected[i] else code for i, code in enumerate(case.codes)]
    bad_context = weighted_l1(masked, case.oriented.width, case.oriented.height, Nlm())
    expect(
        any(
            a != b
            for i, (a, b) in enumerate(zip(case.means, bad_context, strict=True))
            if not case.protected[i]
        ),
        "protected neighbors remain context",
    )


def process(exe: Path, root: Path, fixture: Fixture, encoded: bytes, orientation: int) -> None:
    """Run the original current continuous matrix and one full-prefix binary control."""
    case = prepare_case(root, fixture, encoded, orientation)
    variants: tuple[tuple[int, str], ...] = ((16, "preserve"), (16, "gray"), (8, "preserve"))
    if orientation != 1 and fixture.color in (RGBA, GRAY_ALPHA) and not case.native_profile:
        variants = ((16, "preserve"),)
    for depth, mode in variants:
        continuous_variant(exe, root, case, depth, mode)
    if orientation == 1 and not case.native_profile:
        binary_variant(exe, root, case)
    expect(case.source.read_bytes() == encoded, "source identity preserved")
    if fixture.color == RGBA and orientation == 1:
        challenge_controls(case)
