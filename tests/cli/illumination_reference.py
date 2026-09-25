# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Independent direct quantiles, dense solve, and scalar interpolation for small I01 fixtures."""

from __future__ import annotations

import math

BACKGROUND_FLOOR = 0.02


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


def surface(
    pixels: list[float], width: int, height: int, cell: int, protected: list[bool]
) -> list[float]:
    """Reference background: beta=2 and q=.9; exact actual-cell eligibility."""
    columns, rows = math.ceil(width / cell), math.ceil(height / cell)
    n = columns * rows
    matrix = [[0.0] * n for _ in range(n)]
    rhs = [0.0] * n
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
            weight = len(eligible) / area
            matrix[j][j] += weight
            rhs[j] += weight * math.log(q)
        for neighbor in (
            *([j + 1] if j % columns + 1 < columns else []),
            *([j + columns] if j + columns < n else []),
        ):
            matrix[j][j] += 2
            matrix[neighbor][neighbor] += 2
            matrix[j][neighbor] -= 2
            matrix[neighbor][j] -= 2
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
