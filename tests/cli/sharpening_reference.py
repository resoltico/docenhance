# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent direct 2D Gaussian and piecewise soft-threshold oracle."""

from __future__ import annotations

import math


def reflect(index: int, size: int) -> int:
    """Repeated mirror construction, independently of folded production indexing."""
    if size == 1:
        return 0
    while not 0 <= index < size:
        index = -index if index < 0 else 2 * size - index - 2
    return index


def unsharp(
    values: list[float], extent: tuple[int, int], sigma: float, amount: float, threshold: float
) -> list[float]:
    """Retain unclamped candidates for excursion and clipping references."""
    width, height = extent
    radius = math.ceil(3 * sigma)
    taps = [
        (i, j, math.exp(-(i * i + j * j) / (2 * sigma * sigma)))
        for j in range(-radius, radius + 1)
        for i in range(-radius, radius + 1)
    ]
    normalization = sum(weight for _, _, weight in taps)
    output = []
    for y in range(height):
        for x in range(width):
            blur = (
                sum(
                    weight * values[reflect(y + j, height) * width + reflect(x + i, width)]
                    for i, j, weight in taps
                )
                / normalization
            )
            f = values[y * width + x]
            d = f - blur
            residual = (
                0
                if abs(d) <= threshold / 255
                else d - (threshold / 255 if d > 0 else -threshold / 255)
            )
            output.append(f + amount * residual)
    return output
