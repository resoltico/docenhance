# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Direct mathematical composition oracle; no native or production numerical code."""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass

from continuous_fixtures import GRAY_ALPHA, RGB, RGBA, Fixture
from illumination_reference import Plane, surface
from test_continuous import transfer_decode, transfer_encode

Rgb = tuple[float, ...]
WORD_MAX = 65535
WEIGHT_MAX = (1 << 31) - 1
RGB_CHANNELS = 3
LUMA = (0.2126, 0.7152, 0.0722)


def luminance(rgb: Rgb) -> float:
    """Relative linear D65 luminance, independent of image::numeric."""
    return sum(c * v for c, v in zip(LUMA, rgb, strict=True))


def linear(fixture: Fixture, matte: int, gamma: int | None) -> list[Rgb]:
    """Interpret oriented integers and composite straight alpha before enhancement."""
    maximum = (1 << fixture.depth) - 1
    count = RGB_CHANNELS if fixture.color in (RGB, RGBA) else 1
    result = []
    for pixel in fixture.pixels:
        alpha = pixel[-1] / maximum if fixture.color in (GRAY_ALPHA, RGBA) else 1.0
        encoded = [c / maximum if alpha else 0.0 for c in pixel[:count]]
        decoded = [v ** (100000 / gamma) if gamma else transfer_decode(v) for v in encoded]
        opaque = tuple(alpha * v + (1 - alpha) * matte for v in decoded)
        result.append(opaque if count == RGB_CHANNELS else opaque * RGB_CHANNELS)
    return result


def transport(rgb: Rgb, target: float) -> Rgb:
    """Transport on the neutral axis; identity retains the entering doubles."""
    y = luminance(rgb)
    if target == y:
        return rgb
    if target < y:
        return tuple(c * target / y for c in rgb)
    return tuple(min(1.0, max(0.0, c + (target - y) * (1 - c) / (1 - y))) for c in rgb)


def illuminate(rgb: list[Rgb], width: int, height: int, protected: list[bool]) -> list[Rgb]:
    """Explicit single-cell I01 with q=.9, smooth=2, target=.7, strength=.6, gain<=4."""
    background = surface(Plane([luminance(p) for p in rgb], width, height, protected), 64, 2)
    return [
        p
        if protected[i]
        else transport(p, min(1.0, luminance(p) * min(4.0, max(1.0, 0.7 / max(b, 0.02))) ** 0.6))
        for i, (p, b) in enumerate(zip(rgb, background, strict=True))
    ]


def float32(value: float) -> float:
    """One explicit IEEE binary32 rounding at the native strength arithmetic boundary."""
    return float(struct.unpack("f", struct.pack("f", value))[0])


def reflect(index: int, size: int) -> int:
    """Direct repeated reflection; no production modulo/folding routine."""
    while not 0 <= index < size:
        index = -index if index < 0 else 2 * size - 2 - index
    return index


@dataclass(frozen=True)
class Nlm:
    """Reviewed native scalar settings, independent of host/method types."""

    h: float = 25
    patch: int = 3
    search: int = 7
    blend: float = 0.73


def native_l1(
    values: list[int],
    width: int,
    height: int,
    settings: Nlm,
    *,
    positions: list[tuple[int, int]] | None = None,
) -> list[int]:
    """Exhaustive patch means and integer weights; no rolling sums or native replay."""
    shift = (settings.patch * settings.patch - 1).bit_length()
    bin_scale = (1 << shift) / (settings.patch * settings.patch)
    strength = float32(257 * float32(settings.h))
    squared_strength = float32(strength * strength)
    patch = range(-(settings.patch // 2), settings.patch // 2 + 1)
    search = range(-(settings.search // 2), settings.search // 2 + 1)
    weights: dict[int, int] = {}

    def sample(x: int, y: int) -> int:
        return values[reflect(y, height) * width + reflect(x, width)]

    result = []
    for x, y in positions or [(x, y) for y in range(height) for x in range(width)]:
        numerator = denominator = 0
        for dy in search:
            for dx in search:
                distance = (
                    sum(
                        abs(sample(x + px, y + py) - sample(x + dx + px, y + dy + py))
                        for py in patch
                        for px in patch
                    )
                    >> shift
                )
                if distance not in weights:
                    mean = distance * bin_scale
                    weight = round(WEIGHT_MAX * math.exp(-(mean * mean) / squared_strength))
                    weights[distance] = weight if weight >= 0.001 * WEIGHT_MAX else 0
                weight = weights[distance]
                numerator += weight * sample(x + dx, y + dy)
                denominator += weight
        result.append((numerator + denominator // 2) // denominator)
    return result


def denoise(
    rgb: list[Rgb], width: int, height: int, protected: list[bool], settings: Nlm
) -> tuple[list[Rgb], list[int], list[int]]:
    """Retain doubles, signed correction, fractional blend and destination protection."""
    perceptual = [transfer_encode(luminance(p)) for p in rgb]
    entering = [math.floor(WORD_MAX * f + 0.5) for f in perceptual]
    filtered = native_l1(entering, width, height, settings)
    result = []
    for i, p in enumerate(rgb):
        if protected[i] or entering[i] == filtered[i] or settings.blend == 0:
            result.append(p)
            continue
        f = perceptual[i]
        corrected = min(1.0, max(0.0, f + (filtered[i] - entering[i]) / WORD_MAX))
        blended = (1 - settings.blend) * f + settings.blend * corrected
        result.append(p if blended == f else transport(p, transfer_decode(blended)))
    return result, entering, filtered


def quantize(rgb: list[Rgb], depth: int, *, gray: bool) -> tuple[tuple[int, ...], ...]:
    """One final representation quantizer with upward half-code ties."""
    maximum = (1 << depth) - 1
    return tuple(
        tuple(
            math.floor(maximum * transfer_encode(c) + 0.5) for c in ((luminance(p),) if gray else p)
        )
        for p in rgb
    )


def roundtrip(rgb: list[Rgb], depth: int) -> list[Rgb]:
    """Deliberately wrong intermediate output quantization for a sensitivity control."""
    maximum = (1 << depth) - 1
    return [
        tuple(transfer_decode(c / maximum) for c in p) for p in quantize(rgb, depth, gray=False)
    ]
