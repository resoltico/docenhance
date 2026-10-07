# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent scalar C03 oracle with integer histogram arithmetic and explicit tile geometry."""

from __future__ import annotations

import bisect
import itertools
import math

BINS = 1024
MINIMUM_SAMPLES = 16
MINIMUM_RANGE = 1 / 4096


def mapping(values: list[float], clip: float) -> list[float] | None:
    """Redistribute integer mass, then calculate independent cumulative knots."""
    if len(values) < MINIMUM_SAMPLES or max(values) - min(values) < MINIMUM_RANGE:
        return None
    histogram = [0] * BINS
    for f in values:
        histogram[min(BINS - 1, math.floor(BINS * f))] += 1
    limit = max(1, math.floor(clip * len(values) / BINS))
    clipped = [min(count, limit) for count in histogram]
    quotient, remainder = divmod(sum(histogram) - sum(clipped), BINS)
    redistributed = [count + quotient + int(i < remainder) for i, count in enumerate(clipped)]
    if sum(redistributed) != len(values):
        msg = "Reference histogram lost mass"
        raise AssertionError(msg)
    return [0.0, *(count / len(values) for count in itertools.accumulate(redistributed))]


def contextual(f: float, knots: list[float] | None) -> float:
    """Identity tiles are exact; knot interpolation retains continuous sample precision."""
    if knots is None:
        return f
    scaled = f * BINS
    left = min(BINS - 1, math.floor(scaled))
    return knots[left] * (left + 1 - scaled) + knots[left + 1] * (scaled - left)


def axis(coordinate: int, centers: list[float]) -> tuple[int, int, float]:
    """Bracket in actual uneven-tile centers, with constant exterior extension."""
    right = bisect.bisect_right(centers, coordinate)
    if right == 0:
        return 0, 0, 0
    if right == len(centers):
        return right - 1, right - 1, 0
    left = right - 1
    return left, right, (coordinate - centers[left]) / (centers[right] - centers[left])


def clahe(
    values: list[float],
    extent: tuple[int, int],
    grid: tuple[int, int],
    clip: float,
    protected: list[bool] | None = None,
) -> tuple[list[float], int]:
    """Fit once from eligible values and explicitly interpolate four neighboring maps."""
    width, height = extent
    columns, rows = grid
    mask = protected if protected is not None else [False] * len(values)
    xs = [i * width // columns for i in range(columns + 1)]
    ys = [i * height // rows for i in range(rows + 1)]
    maps = [
        mapping(
            [
                values[y * width + x]
                for y in range(ys[j], ys[j + 1])
                for x in range(xs[i], xs[i + 1])
                if not mask[y * width + x]
            ],
            clip,
        )
        for j in range(rows)
        for i in range(columns)
    ]
    xc = [(a + b - 1) / 2 for a, b in itertools.pairwise(xs)]
    yc = [(a + b - 1) / 2 for a, b in itertools.pairwise(ys)]
    output = []
    for y in range(height):
        top, bottom, v = axis(y, yc)
        for x in range(width):
            f = values[y * width + x]
            if mask[y * width + x]:
                output.append(f)
                continue
            left, right, u = axis(x, xc)
            output.append(
                sum(
                    contextual(f, maps[j * columns + i]) * weight
                    for i, j, weight in (
                        (left, top, (1 - u) * (1 - v)),
                        (right, top, u * (1 - v)),
                        (left, bottom, (1 - u) * v),
                        (right, bottom, u * v),
                    )
                )
            )
    return output, maps.count(None)
