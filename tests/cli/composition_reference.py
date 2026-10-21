# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Direct mathematical composition oracle; no native or production numerical code."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import TYPE_CHECKING

from continuous_fixtures import GRAY_ALPHA, RGB, RGBA, Fixture
from icc_reference import GammaTwoProfile
from illumination_reference import Plane, surface
from test_continuous import transfer_decode, transfer_encode

if TYPE_CHECKING:
    from collections.abc import Sequence


Rgb = tuple[float, ...]
WORD_MAX = 65535
RGB_CHANNELS = 3
CONTROL_EDGE = 3
LAST_NONZERO_HIGH = 77
REFERENCE_TOLERANCE = 1e-12
LUMA = (0.2126, 0.7152, 0.0722)


def luminance(rgb: Rgb) -> float:
    """Relative linear D65 luminance, independent of image::numeric."""
    return sum(c * v for c, v in zip(LUMA, rgb, strict=True))


def linear(fixture: Fixture, matte: int, gamma: int | None) -> list[Rgb]:
    """Interpret oriented integers and composite straight alpha before enhancement."""
    maximum = (1 << fixture.depth) - 1
    count = RGB_CHANNELS if fixture.color in (RGB, RGBA) else 1
    packed = next((body for tag, body in fixture.metadata if tag == b"iCCP"), None)
    profile = GammaTwoProfile(packed, count) if packed is not None else None
    result = []
    for pixel in fixture.pixels:
        alpha = pixel[-1] / maximum if fixture.color in (GRAY_ALPHA, RGBA) else 1.0
        encoded = [c / maximum if alpha else 0.0 for c in pixel[:count]]
        decoded = (
            profile.decode(encoded)
            if profile is not None
            else [v ** (100000 / gamma) if gamma else transfer_decode(v) for v in encoded]
        )
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


def illuminate(values: list[float], width: int, height: int, protected: list[bool]) -> list[float]:
    """I01 acts on Y: q=.9, smooth=2, target=.7, strength=.6, gain<=4."""
    field = surface(Plane(values, width, height, protected), 64, 2)
    return [
        y if protected[i] else min(1.0, y * min(4.0, max(1.0, 0.7 / max(b, 0.02))) ** 0.6)
        for i, (y, b) in enumerate(zip(values, field, strict=True))
    ]


def reflect(index: int, size: int) -> int:
    """Direct repeated reflection; no production modulo/folding routine."""
    if size == 1:
        return 0
    while not 0 <= index < size:
        index = -index if index < 0 else 2 * size - 2 - index
    return index


@dataclass(frozen=True)
class Nlm:
    """Reviewed D01v2 scalar settings, independent of native or production types."""

    h: float = 25
    patch: int = 3
    search: int = 7
    blend: float = 0.73


def weight_entries(settings: Nlm) -> int:
    """Finite exact-distance table ends before the first integer at or above three sigmas."""
    return math.ceil(3 * (settings.h / 255) * (settings.patch * settings.patch) * WORD_MAX)


def weighted_l1(
    values: list[int],
    width: int,
    height: int,
    settings: Nlm,
    *,
    positions: list[tuple[int, int]] | None = None,
) -> list[float]:
    """Direct integer patch sums and binary64 exp/weighted means in fixed dy/dx search order."""
    normalization = (settings.patch * settings.patch) * WORD_MAX * (settings.h / 255)
    limit = weight_entries(settings)
    patch = range(-(settings.patch // 2), settings.patch // 2 + 1)
    search = range(-(settings.search // 2), settings.search // 2 + 1)
    weights: dict[int, float] = {}

    def sample(x: int, y: int) -> int:
        return values[reflect(y, height) * width + reflect(x, width)]

    result = []
    locations = (
        positions
        if positions is not None
        else [(x, y) for y in range(height) for x in range(width)]
    )
    for x, y in locations:
        numerator = denominator = 0.0
        for dy in search:
            for dx in search:
                distance = sum(
                    abs(sample(x + px, y + py) - sample(x + dx + px, y + dy + py))
                    for py in patch
                    for px in patch
                )
                if distance >= limit:
                    continue
                if distance not in weights:
                    ratio = distance / normalization
                    weights[distance] = math.exp(-(ratio * ratio))
                weight = weights[distance]
                numerator += weight * sample(x + dx, y + dy)
                denominator += weight
        result.append(numerator / denominator)
    return result


def denoise(
    values: list[float], width: int, height: int, protected: list[bool], settings: Nlm
) -> tuple[list[float], list[int], list[float]]:
    """Retain binary64 Y, signed fractional Q16 correction, blend and exact protected/no-op Y."""
    perceptual = [transfer_encode(y) for y in values]
    entering = [math.floor(WORD_MAX * f + 0.5) for f in perceptual]
    filtered = weighted_l1(entering, width, height, settings)
    result = []
    for i, y in enumerate(values):
        if protected[i] or entering[i] == filtered[i] or settings.blend == 0:
            result.append(y)
            continue
        f = perceptual[i]
        corrected = min(1.0, max(0.0, f + (filtered[i] - entering[i]) / WORD_MAX))
        blended = (1 - settings.blend) * f + settings.blend * corrected
        result.append(y if blended == f else transfer_decode(blended))
    return result, entering, filtered


def final_codes(
    original: Sequence[Rgb], values: list[float], depth: int, *, gray: bool
) -> tuple[tuple[int, ...], ...]:
    """One final quantizer; color transports once from the retained interpreted original RGB."""
    maximum = (1 << depth) - 1
    return tuple(
        tuple(
            math.floor(maximum * transfer_encode(c) + 0.5)
            for c in ((y,) if gray else transport(rgb, y))
        )
        for rgb, y in zip(original, values, strict=True)
    )


def roundtrip(values: list[float], depth: int) -> list[float]:
    """Deliberately wrong intermediate Y quantization for the sensitivity control."""
    maximum = (1 << depth) - 1
    return [
        transfer_decode(math.floor(maximum * transfer_encode(y) + 0.5) / maximum) for y in values
    ]


def terminal_fixed(values: list[float], threshold: float = 0.5) -> tuple[tuple[int], ...]:
    """One terminal Q16 boundary, then exact normalized Fixed classification into logical0/1."""
    codes = [math.floor(WORD_MAX * transfer_encode(y) + 0.5) for y in values]
    return tuple((0 if code / WORD_MAX <= threshold else 1,) for code in codes)


def reference_controls() -> None:
    """Analytic small signals distinguish fractional means and the exact exponential cutoff."""
    impulse = [0] * 49
    impulse[24] = 1
    fractional = weighted_l1(impulse, 7, 7, Nlm(h=3), positions=[(0, 0)])[0]
    if not 0 < fractional < 1:
        message = "NLM reference lost its unrounded fractional mean"
        raise AssertionError(message)
    settings = Nlm(h=0.1)
    normalization = 9 * WORD_MAX * (0.1 / 255)
    for high in (77, 78):
        values = [0 if x < CONTROL_EDGE else high for _ in range(7) for x in range(7)]
        actual = weighted_l1(values, 7, 7, settings, positions=[(1, 3)])[0]
        a = math.exp(-((3 * high / normalization) ** 2))
        b = math.exp(-((6 * high / normalization) ** 2))
        c = math.exp(-((9 * high / normalization) ** 2)) if high == LAST_NONZERO_HIGH else 0.0
        # At centerx1, mirror101 gives three0-distance centers, two3*high centers,
        # one6*high center and one9*high center. The seven identical rows cancel.
        expected = high * (b + c) / (3 + 2 * a + b + c)
        if abs(actual - expected) >= REFERENCE_TOLERANCE:
            message = "NLM reference changed exact-distance exponential/cutoff math"
            raise AssertionError(message)
