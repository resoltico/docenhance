# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent direct Fourier sums for known-PSF phase, DC, padding and blending."""

from __future__ import annotations

import cmath
import math
from functools import lru_cache


def reflect(index: int, size: int) -> int:
    """Repeated mirrors avoid production's folded index implementation."""
    if size == 1:
        return 0
    while index < 0 or index >= size:
        index = -index if index < 0 else 2 * size - index - 2
    return index


def optimal(size: int) -> int:
    """Select the smallest integer whose prime factors belong to 2, 3, 5."""
    while True:
        remainder = size
        for divisor in (2, 3, 5):
            while remainder % divisor == 0:
                remainder //= divisor
        if remainder == 1:
            return size
        size += 1


def gaussian(sigma: float) -> tuple[int, int, list[float]]:
    """Sample the analytic isotropic Gaussian directly in two dimensions."""
    radius = math.ceil(3 * sigma)
    values = [
        math.exp(-(x * x + y * y) / (2 * sigma * sigma))
        for y in range(-radius, radius + 1)
        for x in range(-radius, radius + 1)
    ]
    total = math.fsum(values)
    return 2 * radius + 1, 2 * radius + 1, [v / total for v in values]


def motion(length: float, angle: float) -> tuple[int, int, list[float]]:
    """Midpoint exposure deposits clockwise bilinear mass, then normalizes."""
    radius = math.ceil(length / 2) + 1
    size = 2 * radius + 1
    samples = max(64, math.ceil(32 * length))
    values = [0.0] * (size * size)
    theta = math.radians(angle)
    for n in range(samples):
        t = ((n + 0.5) / samples - 0.5) * length
        x, y = radius + t * math.cos(theta), radius + t * math.sin(theta)
        left, top = math.floor(x), math.floor(y)
        for dy in (0, 1):
            for dx in (0, 1):
                values[(top + dy) * size + left + dx] += (1 - abs(x - left - dx)) * (
                    1 - abs(y - top - dy)
                )
    total = math.fsum(values)
    return size, size, [v / total for v in values]


@lru_cache(maxsize=16)
def waves(size: int, *, inverse: bool) -> tuple[tuple[complex, ...], ...]:
    """Precompute direct-sum exponentials, independently of an FFT algorithm."""
    sign = 1 if inverse else -1
    return tuple(
        tuple(cmath.exp(sign * 2j * math.pi * k * n / size) for n in range(size))
        for k in range(size)
    )


def dft2(values: list[complex], width: int, height: int, *, inverse: bool = False) -> list[complex]:
    """Separable direct sums are O(HW(H+W)); no imaging or FFT dependency."""
    wx, wy = waves(width, inverse=inverse), waves(height, inverse=inverse)
    rows = [
        sum(values[y * width + n] * wx[k][n] for n in range(width))
        for y in range(height)
        for k in range(width)
    ]
    scale = width * height if inverse else 1
    return [
        sum(rows[n * width + x] * wy[k][n] for n in range(height)) / scale
        for k in range(height)
        for x in range(width)
    ]


def restore(
    values: list[float],
    extent: tuple[int, int],
    psf: tuple[int, int, list[float]],
    k: float,
    blend: float,
) -> tuple[list[float], list[float]]:
    """Return raw candidate and unclamped blend using the analytic kernel spectrum."""
    width, height = extent
    kw, kh, taps = psf
    guard = max(32, 4 * max(kw // 2, kh // 2))
    fw, fh = optimal(width + 2 * guard), optimal(height + 2 * guard)
    padded = [
        values[reflect(y - guard, height) * width + reflect(x - guard, width)]
        for y in range(fh)
        for x in range(fw)
    ]
    mean = math.fsum(padded) / len(padded)
    spectrum = dft2([complex(v - mean) for v in padded], fw, fh)
    # Analytic Fourier sum of spatial displacements; never copy production's modular placement.
    xwaves, ywaves = waves(fw, inverse=False), waves(fh, inverse=False)
    xindices = [(x - kw // 2) % fw for x in range(kw)]
    yindices = [(y - kh // 2) % fh for y in range(kh)]
    filtered = []
    for y in range(fh):
        for x in range(fw):
            h = sum(
                taps[j * kw + i] * xwaves[x][xindices[i]] * ywaves[y][yindices[j]]
                for j in range(kh)
                for i in range(kw)
            )
            filtered.append(h.conjugate() * spectrum[y * fw + x] / (abs(h) ** 2 + k))
    spatial = dft2(filtered, fw, fh, inverse=True)
    candidate = [
        mean + spatial[(y + guard) * fw + x + guard].real
        for y in range(height)
        for x in range(width)
    ]
    return candidate, [(1 - blend) * v + blend * u for v, u in zip(values, candidate, strict=True)]
