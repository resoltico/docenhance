# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Direct two-dimensional I02 reference, independent of production separable queues."""

from __future__ import annotations

import math


def reflected(position: int, length: int) -> int:
    """Walk REFLECT_101 borders, including support larger than the image."""
    if length == 1:
        return 0
    while position < 0 or position >= length:
        position = -position if position < 0 else 2 * length - 2 - position
    return position


def square(
    values: list[float], width: int, height: int, radius: int, *, dilation: bool
) -> list[float]:
    """Enumerate the full square neighborhood without separable extrema."""
    select = max if dilation else min
    return [
        select(
            values[reflected(y + dy, height) * width + reflected(x + dx, width)]
            for dy in range(-radius, radius + 1)
            for dx in range(-radius, radius + 1)
        )
        for y in range(height)
        for x in range(width)
    ]


def background(values: list[float], width: int, height: int, radius: int) -> list[float]:
    """Close the gray plane, then convolve a directly normalized 2D Gaussian."""
    closed = square(
        square(values, width, height, radius, dilation=True), width, height, radius, dilation=False
    )
    sigma = max(0.5, radius / 2)
    support = math.ceil(3 * sigma)
    weights = [
        (dx, dy, math.exp(-(dx * dx + dy * dy) / (2 * sigma * sigma)))
        for dy in range(-support, support + 1)
        for dx in range(-support, support + 1)
    ]
    total = sum(w for _, _, w in weights)
    return [
        sum(
            w * closed[reflected(y + dy, height) * width + reflected(x + dx, width)]
            for dx, dy, w in weights
        )
        / total
        for y in range(height)
        for x in range(width)
    ]
