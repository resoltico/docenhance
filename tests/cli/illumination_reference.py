# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Independent direct quantiles, dense solve, and scalar interpolation for small I01 fixtures."""

from __future__ import annotations

import math
from dataclasses import dataclass

BACKGROUND_FLOOR = 0.02
GAIN_LIMIT = 4
REFERENCE_RANK = 0.9


def quantile(values: list[float], p: float) -> float:
    """Nearest rank from a full sort, not the production selection routine."""
    return sorted(values)[max(0, math.ceil(p * len(values)) - 1)]


def solve(matrix: list[list[float]], rhs: list[float]) -> list[float]:
    """Pivoted elimination of an explicitly assembled system, independent of PCG."""
    n = len(rhs)
    for i in range(n):
        pivot = max(range(i, n), key=lambda j: abs(matrix[j][i]))
        matrix[i], matrix[pivot] = matrix[pivot], matrix[i]
        rhs[i], rhs[pivot] = rhs[pivot], rhs[i]
        diagonal = matrix[i][i]
        if diagonal <= 0:
            msg = "Invalid oracle pivot"
            raise ValueError(msg)
        for j in range(i, n):
            matrix[i][j] /= diagonal
        rhs[i] /= diagonal
        for row in range(i + 1, n):
            scale = matrix[row][i]
            for j in range(i, n):
                matrix[row][j] -= scale * matrix[i][j]
            rhs[row] -= scale * rhs[i]
    for i in reversed(range(n)):
        rhs[i] -= sum(matrix[i][j] * rhs[j] for j in range(i + 1, n))
    return rhs


def interval(centers: list[float], x: int) -> tuple[int, int, float]:
    """Interpolate between actual clipped cell centers, clamping outside their hull."""
    if x <= centers[0]:
        return 0, 0, 0
    for i in range(1, len(centers)):
        if x <= centers[i]:
            return i - 1, i, (x - centers[i - 1]) / (centers[i] - centers[i - 1])
    return len(centers) - 1, len(centers) - 1, 0


@dataclass(frozen=True)
class Plane:
    """Linear luminance samples in row-major order and their protection flags."""

    pixels: list[float]
    width: int
    height: int
    protected: list[bool]


def surface(plane: Plane, cell: int, smooth: float) -> list[float]:
    """Reference background at q=.9: exact actual-cell eligibility and dark-cell exclusion."""
    pixels, width, height, protected = plane.pixels, plane.width, plane.height, plane.protected
    columns, rows = math.ceil(width / cell), math.ceil(height / cell)
    n = columns * rows
    measured: dict[int, tuple[float, float]] = {}
    for j in range(n):
        x0, y0 = (j % columns) * cell, (j // columns) * cell
        x1, y1 = min(width, x0 + cell), min(height, y0 + cell)
        eligible = [
            pixels[y * width + x]
            for y in range(y0, y1)
            for x in range(x0, x1)
            if not protected[y * width + x]
        ]
        area = (x1 - x0) * (y1 - y0)
        q = quantile(eligible, 0.9) if eligible else 0
        if len(eligible) >= max(1, min(16, area), math.ceil(0.25 * area)) and q >= BACKGROUND_FLOOR:
            measured[j] = (len(eligible) / area, math.log(q))
    if measured:
        reference = quantile([value for _, value in measured.values()], REFERENCE_RANK)
        dark = reference - math.log(GAIN_LIMIT)
        measured = {j: m for j, m in measured.items() if m[1] >= dark}
    matrix = [[0.0] * n for _ in range(n)]
    rhs = [0.0] * n
    for j in range(n):
        if j in measured:
            weight, value = measured[j]
            matrix[j][j] += weight
            rhs[j] += weight * value
        for neighbor in (
            *([j + 1] if j % columns + 1 < columns else []),
            *([j + columns] if j + columns < n else []),
        ):
            matrix[j][j] += smooth
            matrix[neighbor][neighbor] += smooth
            matrix[j][neighbor] -= smooth
            matrix[neighbor][j] -= smooth
    grid = solve(matrix, rhs)
    xs = [(x + min(x + cell, width) - 1) / 2 for x in range(0, width, cell)]
    ys = [(y + min(y + cell, height) - 1) / 2 for y in range(0, height, cell)]
    result = []
    for y in range(height):
        lo_y, hi_y, dy = interval(ys, y)
        for x in range(width):
            lo_x, hi_x, dx = interval(xs, x)
            top = (1 - dx) * grid[lo_y * columns + lo_x] + dx * grid[lo_y * columns + hi_x]
            bottom = (1 - dx) * grid[hi_y * columns + lo_x] + dx * grid[hi_y * columns + hi_x]
            result.append(math.exp((1 - dy) * top + dy * bottom))
    return result
