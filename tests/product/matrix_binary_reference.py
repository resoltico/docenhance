# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent terminal Q16 and direct B01/B02/B03 product sample oracles."""

from __future__ import annotations

import math
import statistics
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from continuous_fixtures import Fixture
    from matrix_types import Case

WHITE = 65535
BINS = 4096
ENCODED_TRANSFER_BREAK = 0.04045
LINEAR_TRANSFER_BREAK = 0.0031308
FIXED_THRESHOLD = 0.5
OTSU_FALLBACK = 2047


def terminal_codes(source: Fixture) -> list[int]:
    """These binary reference rows use declaration-free opaque gray sRGB source integers."""
    maximum = (1 << source.depth) - 1
    result = []
    for pixel in source.pixels:
        value = pixel[0] / maximum
        linear = (
            value / 12.92 if value <= ENCODED_TRANSFER_BREAK else ((value + 0.055) / 1.055) ** 2.4
        )
        encoded = (
            12.92 * linear
            if linear <= LINEAR_TRANSFER_BREAK
            else 1.055 * linear ** (1 / 2.4) - 0.055
        )
        result.append(math.floor(WHITE * encoded + 0.5))
    return result


def binary_reference(case: Case, source: Fixture) -> tuple[tuple[int, ...], ...]:
    """Direct threshold/window/histogram math; no production or earlier CLI output as expected."""
    values = terminal_codes(source)
    selector = case.options[case.options.index("--binarize") + 1]
    if selector == "fixed":
        return tuple((0 if value / WHITE <= FIXED_THRESHOLD else 1,) for value in values)
    if selector == "otsu":
        bins = [math.floor((BINS - 1) * value / WHITE + 0.5) for value in values]
        if len(set(bins)) == 1:
            return tuple((0 if value <= OTSU_FALLBACK else 1,) for value in bins)
        scores = []
        for threshold in range(BINS - 1):
            low = [v for v in bins if v <= threshold]
            high = [v for v in bins if v > threshold]
            if low and high:
                scores.append(
                    (
                        len(low)
                        * len(high)
                        / len(bins) ** 2
                        * (statistics.mean(low) - statistics.mean(high)) ** 2,
                        threshold,
                    )
                )
        best = max(score for score, _ in scores)
        selected = min(t for score, t in scores if best - score <= 1e-12 * max(1, best))
        return tuple((0 if value <= selected else 1,) for value in bins)
    return sauvola(values, source.width, source.height)


def reflect(coordinate: int, extent: int) -> int:
    """REFLECT_101 without duplicating edge samples."""
    if extent == 1:
        return 0
    reduced = coordinate % (2 * (extent - 1))
    return reduced if reduced < extent else 2 * (extent - 1) - reduced


def sauvola(values: list[int], width: int, height: int) -> tuple[tuple[int, ...], ...]:
    """Direct31-square exact integer moments, then the specified binary64 threshold."""
    result = []
    for y in range(height):
        for x in range(width):
            window = [
                values[reflect(y + dy, height) * width + reflect(x + dx, width)]
                for dy in range(-15, 16)
                for dx in range(-15, 16)
            ]
            total = sum(window)
            mean = total / len(window)
            variance = len(window) * sum(value * value for value in window) - total * total
            deviation = math.sqrt(variance) / len(window)
            threshold = mean * (1 + 0.2 * (deviation / (WHITE * 0.5) - 1))
            result.append((0 if values[y * width + x] <= threshold else 1,))
    return tuple(result)
