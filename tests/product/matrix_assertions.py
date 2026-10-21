# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent sample rules and narrowly defined synthetic benefit measurements."""

from __future__ import annotations

import math
import statistics
import struct
from typing import TYPE_CHECKING, Any

from continuous_fixtures import Fixture, chunks
from contract_reference import reviewed_methods
from matrix_binary_reference import binary_reference
from matrix_fixtures import HEIGHT, MARKS, WIDTH, png_sources, tiff_sources

if TYPE_CHECKING:
    from matrix_types import Case

STAGE_REPORT = {
    "I01": "illumination",
    "I02": "illumination",
    "D01": "denoising",
    "D02": "denoising",
    "C01": "contrast",
    "C02": "contrast",
    "C03": "contrast",
    "S01": "sharpening",
    "R01": "restoration",
}
RGB_CHANNELS = 3
BYTE_BITS = 8
QUARTER_TURN = 90
LINEAR_TRANSFER_BREAK = 0.0031308
ENCODED_TRANSFER_BREAK = 0.04045
MARK_ROW = 8
MARK_THRESHOLD = 100
ORIENTATION_TRANSPOSE = 5


def require(condition: object, message: str) -> None:
    """Never let optimized Python strip a product assertion."""
    if not condition:
        raise AssertionError(message)


def option(case: Case, name: str, default: str) -> str:
    """Read a supplied fixture expectation without replaying product admission rules."""
    return case.options[case.options.index(name) + 1] if name in case.options else default


def oriented(fixture: Fixture, orientation: int, turn: int) -> Fixture:
    """Scatter unique samples through explicit independently specified permutations."""
    width, height = fixture.width, fixture.height
    extent = (height, width) if orientation >= ORIENTATION_TRANSPOSE else (width, height)
    values = [fixture.pixels[0]] * len(fixture.pixels)
    for v in range(height):
        for u in range(width):
            positions = (
                (u, v),
                (width - 1 - u, v),
                (width - 1 - u, height - 1 - v),
                (u, height - 1 - v),
                (v, u),
                (height - 1 - v, u),
                (height - 1 - v, width - 1 - u),
                (v, width - 1 - u),
            )
            x, y = positions[orientation - 1]
            values[y * extent[0] + x] = fixture.pixels[v * width + u]
    width, height = extent
    rotated_extent = (height, width) if turn % 180 else (width, height)
    result = [values[0]] * len(values)
    for v in range(height):
        for u in range(width):
            rotations = (
                (u, v),
                (height - 1 - v, u),
                (width - 1 - u, height - 1 - v),
                (v, width - 1 - u),
            )
            x, y = rotations[turn // QUARTER_TURN]
            result[y * rotated_extent[0] + x] = values[v * width + u]
    return Fixture(*rotated_extent, tuple(result), depth=fixture.depth, color=fixture.color)


def transfer(value: float, *, encode: bool) -> float:
    """Standard piecewise sRGB scalar reference in double precision."""
    if encode:
        return (
            12.92 * value if value <= LINEAR_TRANSFER_BREAK else 1.055 * value ** (1 / 2.4) - 0.055
        )
    return value / 12.92 if value <= ENCODED_TRANSFER_BREAK else ((value + 0.055) / 1.055) ** 2.4


def alpha_gray(case: Case, source: Fixture, output: Fixture) -> None:
    """Check linear compositing and relative luminance against independent scalar equations."""
    maximum = (1 << source.depth) - 1
    gray = option(case, "--output-mode", "preserve") == "gray" or source.color in (0, 4)
    matte = 0 if option(case, "--alpha", "white") == "black" else 1
    expected = []
    for pixel in source.pixels:
        alpha = pixel[-1] / maximum if source.color in (4, 6) else 1
        colors = pixel[:3] if source.color in (2, 6) else pixel[:1]
        linear = [alpha * transfer(c / maximum, encode=False) + (1 - alpha) * matte for c in colors]
        if gray and len(linear) == RGB_CHANNELS:
            linear = [sum(c * w for c, w in zip(linear, (0.2126, 0.7152, 0.0722), strict=True))]
        expected.append(tuple(math.floor(maximum * transfer(c, encode=True) + 0.5) for c in linear))
    require(output.pixels == tuple(expected), "linear alpha/luminance samples disagree")


def sample_check(case: Case, output: Fixture, raw: bytes) -> None:
    """Precise representation/geometry/binary checks use independent source samples."""
    source = png_sources().get(case.source)
    if case.check == "tiff":
        fixture = tiff_sources()[case.source]
        multiplier = 255 if fixture.depth == 1 else 1
        require(
            output.pixels == tuple(tuple(c * multiplier for c in p) for p in fixture.pixels),
            "TIFF precision/layout/compression samples disagree",
        )
    elif source is not None:
        source_check(case, source, output, raw)


def source_check(case: Case, source: Fixture, output: Fixture, raw: bytes) -> None:
    """Check each exact source rule without interpreting a successful exit as correctness."""
    if case.check == "samples":
        maximum = (1 << source.depth) - 1
        expected = (
            source.pixels
            if source.depth >= BYTE_BITS
            else tuple((p[0] * 255 // maximum,) for p in source.pixels)
        )
        require(output.pixels == expected, "stored sample identity disagrees")
    elif case.check == "palette":
        palette = ((0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255))
        require(
            output.pixels == tuple(palette[p[0]] for p in source.pixels),
            "palette RGB samples disagree",
        )
    elif case.check == "alpha-or-gray":
        alpha_gray(case, source, output)
    elif case.check == "depth":
        depth = int(option(case, "--bit-depth", "16"))
        expected = tuple((round(p[0] * ((1 << depth) - 1) / 65535),) for p in source.pixels)
        require(
            output.depth == depth and output.pixels == expected,
            "explicit precision conversion disagrees",
        )
    elif case.check == "geometry":
        orientation = int(case.source.split("-")[-1])
        turn = int(option(case, "--rotate", "0"))
        require(
            output == oriented(source, orientation, turn),
            "G01/G02 direction, mirror or extent disagrees",
        )
        density = (
            (2000, 1000)
            if (orientation >= ORIENTATION_TRANSPOSE) != bool(turn % 180)
            else (1000, 2000)
        )
        require(
            struct.unpack(">IIB", chunks(raw)[b"pHYs"][0]) == (*density, 1),
            "physical density transpose disagrees",
        )
    elif case.check == "binary":
        source = oriented(source, 1, int(option(case, "--rotate", "0")))
        require(
            output.pixels == binary_reference(case, source),
            "independent binary threshold samples disagree",
        )


def stage_check(case: Case, response: dict[str, Any]) -> None:
    """Enabled method identities and complete reports must agree with admitted scenarios."""
    execution = response["record"]["execution"]
    for method in case.methods:
        family = STAGE_REPORT.get(method, "binarization" if method.startswith("B") else "")
        if family:
            facts = execution[family]
            require(
                facts is not None and facts["method"] == method,
                f"{method} identity/completion disagrees",
            )
    if case.group == "benefit":
        facts = execution[STAGE_REPORT[case.methods[0]]]
        require(
            facts["disposition"] == "applied",
            "enabled beneficial scenario performed no active work",
        )
    if case.check == "auto-skip":
        facts = execution["illumination"]
        require(
            facts is not None and facts["disposition"] == "skipped",
            "already-good flat page should skip automatic correction",
        )
    if option(case, "--output-mode", "preserve") == "bw":
        facts = execution["binarization"]
        output = response["record"]["output"]
        require(
            output["depth"] == 1 and output["channels"] == 1,
            "binary output must be physical grayscale PNG1",
        )
        require(
            facts["black_samples"] + facts["white_samples"] == output["width"] * output["height"],
            "binary classification must cover every output pixel",
        )


def compare_outputs(case: Case, output: Fixture, baseline: Fixture) -> None:
    """Identity/protection assertions compare the same representation path exactly."""
    if case.check == "same":
        require(output == baseline, "identity differs from no-filter representation")
    elif case.check in ("protected", "composition"):
        mask = png_sources()["mask"]
        if "--rotate" in case.options:
            mask = oriented(mask, 1, int(option(case, "--rotate", "0")))
        require(
            all(
                pixel == reference
                for pixel, reference, protected in zip(
                    output.pixels, baseline.pixels, mask.pixels, strict=True
                )
                if protected[0]
            ),
            "protected destinations changed",
        )
        require(
            output.pixels != baseline.pixels, "active protected/composed path made no pixel changes"
        )


def quality(case: Case, output: Fixture) -> dict[str, Any]:
    """Report synthetic gains separately; these measurements cannot certify real documents."""
    source = png_sources().get(case.source)
    if case.group != "benefit" or source is None:
        return {}
    before, after = [p[0] for p in source.pixels], [p[0] for p in output.pixels]
    if case.check == "paper":
        region = [y * WIDTH + x for y in range(HEIGHT) for x in range(WIDTH) if y % 16 != MARK_ROW]
        initial = statistics.pstdev(before[i] for i in region) / statistics.mean(
            before[i] for i in region
        )
        final = statistics.pstdev(after[i] for i in region) / statistics.mean(
            after[i] for i in region
        )
        return {
            "metric": "encoded paper coefficient of variation",
            "before": initial,
            "after": final,
            "accepted": final <= 0.6 * initial,
        }
    if case.check == "noise":
        region = [y * WIDTH + x for y in range(32, 56) for x in range(8, 56)]
        initial, final = (
            statistics.pvariance(before[i] for i in region),
            statistics.pvariance(after[i] for i in region),
        )
        retained = sum(
            after[y * WIDTH + x] < MARK_THRESHOLD
            and all(
                after[(y + dy) * WIDTH + x + dx] >= MARK_THRESHOLD
                for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))
            )
            for x, y in MARKS
        )
        return {
            "metric": "flat-region variance",
            "before": initial,
            "after": final,
            "dark_disconnected_marks_of_3": retained,
            "dark_mark_threshold": MARK_THRESHOLD,
            "marks": mark_measurements(before, after),
            "noise_reduction_accepted": final <= 0.9 * initial,
            "accepted": final <= 0.9 * initial and retained == len(MARKS),
            "mark_threshold_caution": retained != len(MARKS),
        }
    if case.check in ("edge", "local-contrast"):
        initial = max(abs(before[i] - before[i - 1]) for i in range(1, WIDTH))
        final = max(abs(after[i] - after[i - 1]) for i in range(1, WIDTH))
        return {
            "metric": "maximum adjacent contrast on first row",
            "before": initial,
            "after": final,
            "accepted": final > initial,
        }
    expected = [value / 255 for value in before]
    if case.check == "gamma":
        expected = [value * value for value in expected]
    elif case.check == "levels":
        ordered = sorted(expected)
        low, high = (
            ordered[math.ceil(0.005 * len(ordered)) - 1],
            ordered[math.ceil(0.995 * len(ordered)) - 1],
        )
        expected = [min(1, max(0, (value - low) / (high - low))) for value in expected]
    error = max(abs(actual / 255 - wanted) for actual, wanted in zip(after, expected, strict=True))
    return {
        "metric": "independent normalized mapping maximum error",
        "error": error,
        "accepted": error <= 1 / 255,
    }


def mark_measurements(before: list[int], after: list[int]) -> list[dict[str, Any]]:
    """Record visible local contrast separately from the predefined dark-mark threshold."""
    measurements = []
    for x, y in MARKS:
        neighbors = [(y + dy) * WIDTH + x + dx for dx, dy in ((0, 1), (0, -1), (1, 0), (-1, 0))]
        initial, final = before[y * WIDTH + x], after[y * WIDTH + x]
        measurements.append(
            {
                "x": x,
                "y": y,
                "before": initial,
                "after": final,
                "local_contrast_before": statistics.mean(before[i] for i in neighbors) - initial,
                "local_contrast_after": statistics.mean(after[i] for i in neighbors) - final,
            }
        )
    return measurements


def discovery_check(case: Case, response: dict[str, Any]) -> None:
    """Discovery must match reviewed capability contracts, including no planned promotion."""
    if case.id in ("version", "methods") or (case.id.startswith("method-") and not case.exit_code):
        expected = [
            method
            for method in reviewed_methods()
            if not case.id.startswith("method-") or method["id"] == case.options[0]
        ]
        require(
            response["methods"] == expected,
            "runtime implemented catalog differs from reviewed contract",
        )
