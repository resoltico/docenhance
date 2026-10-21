# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent declared-matrix oracle for the known gamma-two ICC composition fixtures."""

from __future__ import annotations

import math
import struct
import zlib
from itertools import product

Matrix = list[list[float]]
PCS_SCALE = 65536.0 / (65535.0 * 2)
FIXTURE_PROFILE_LIMIT = 1024 * 1024
ICC_NAME_MAX = 79
TAG_TABLE_OFFSET = 132
RGB_CHANNELS = 3
XYZ_TAG_BYTES = 20
ANCHOR_TOLERANCE = 1e-12
D50 = (0.9642, 1.0, 0.8249)
MATRIX_FIXTURE_TAGS = frozenset(
    (
        b"desc",
        b"cprt",
        b"wtpt",
        b"chad",
        b"chrm",
        b"rXYZ",
        b"gXYZ",
        b"bXYZ",
        b"rTRC",
        b"gTRC",
        b"bTRC",
        b"kTRC",
    )
)


def binary32(value: float) -> float:
    """Round one declared native evaluator boundary without importing a color engine."""
    return float(struct.unpack(">f", struct.pack(">f", value))[0])


def _product(left: Matrix, right: Matrix) -> Matrix:
    return [
        [
            sum(a * b for a, b in zip(row, column, strict=True))
            for column in zip(*right, strict=True)
        ]
        for row in left
    ]


def _inverse(matrix: Matrix) -> Matrix:
    rows = [list(row) + [float(i == j) for j in range(3)] for i, row in enumerate(matrix)]
    for column in range(3):
        pivot = max(range(column, 3), key=lambda i: abs(rows[i][column]))
        rows[column], rows[pivot] = rows[pivot], rows[column]
        divisor = rows[column][column]
        if divisor == 0:
            msg = "Independent destination matrix is singular"
            raise ValueError(msg)
        rows[column] = [value / divisor for value in rows[column]]
        for row in range(3):
            if row != column:
                factor = rows[row][column]
                rows[row] = [a - factor * b for a, b in zip(rows[row], rows[column], strict=True)]
    return [row[3:] for row in rows]


def destination_matrix() -> Matrix:
    """Derive D65 sRGB primaries and Bradford adaptation to the declared D50 PCS."""
    primaries = [
        [0.64, 0.30, 0.15],
        [0.33, 0.60, 0.06],
        [1 - 0.64 - 0.33, 1 - 0.30 - 0.60, 1 - 0.15 - 0.06],
    ]
    white = [[0.3127 / 0.3290], [1.0], [(1 - 0.3127 - 0.3290) / 0.3290]]
    scales = _product(_inverse(primaries), white)
    rgb_xyz = [[primaries[i][j] * scales[j][0] for j in range(3)] for i in range(3)]
    cones = [[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]]
    source = _product(cones, white)
    target = _product(cones, [[value] for value in D50])
    ratios = [[target[i][0] / source[i][0] if i == j else 0.0 for j in range(3)] for i in range(3)]
    return _product(_product(_product(_inverse(cones), ratios), cones), rgb_xyz)


def _tags(packed: bytes, channels: int) -> dict[bytes, bytes]:
    end = packed.index(0)
    if not 1 <= end <= ICC_NAME_MAX or packed[end + 1 : end + 2] != b"\0":
        msg = "Known ICC fixture needs a framed deflated profile"
        raise ValueError(msg)
    inflater = zlib.decompressobj()
    profile = inflater.decompress(packed[end + 2 :], FIXTURE_PROFILE_LIMIT + 1)
    if (
        len(profile) > FIXTURE_PROFILE_LIMIT
        or not inflater.eof
        or inflater.unused_data
        or inflater.unconsumed_tail
        or len(profile) < TAG_TABLE_OFFSET
    ):
        msg = "Known ICC fixture profile is incomplete or exceeds its finite fixture bound"
        raise ValueError(msg)
    model = b"RGB " if channels == RGB_CHANNELS else b"GRAY"
    if (
        channels not in (1, RGB_CHANNELS)
        or int.from_bytes(profile[:4], "big") != len(profile)
        or profile[12:24] != b"mntr" + model + b"XYZ "
        or profile[36:40] != b"acsp"
    ):
        msg = "Known ICC fixture needs a matching display matrix/gray XYZ profile"
        raise ValueError(msg)
    count = int.from_bytes(profile[128:132], "big")
    if count > (len(profile) - TAG_TABLE_OFFSET) // 12:
        msg = "Known ICC fixture tag table is truncated"
        raise ValueError(msg)
    result = {}
    for index in range(count):
        at = TAG_TABLE_OFFSET + 12 * index
        name = profile[at : at + 4]
        if name not in MATRIX_FIXTURE_TAGS:
            msg = "Known matrix fixture rejects transform-bearing or unsupported ICC tags"
            raise ValueError(msg)
        offset, size = struct.unpack(">II", profile[at + 4 : at + 12])
        if name in result or offset > len(profile) or size > len(profile) - offset:
            msg = "Known ICC fixture has duplicate or out-of-range tags"
            raise ValueError(msg)
        result[name] = profile[offset : offset + size]
    return result


def _xyz(body: bytes) -> list[float]:
    if len(body) != XYZ_TAG_BYTES or body[:8] != b"XYZ " + b"\0" * 4:
        msg = "Known ICC fixture colorant must be one signed16.16 XYZ value"
        raise ValueError(msg)
    return [value / 65536 for value in struct.unpack(">iii", body[8:XYZ_TAG_BYTES])]


def _power_two(body: bytes) -> None:
    if body != b"para" + b"\0" * 8 + struct.pack(">i", 2 * 65536):
        msg = "This independent oracle admits only the fixture's exact gamma-two parametric TRC"
        raise ValueError(msg)


def _stage(matrix: Matrix, values: list[float]) -> list[float]:
    result = []
    for row in matrix:
        value = 0.0
        for coefficient, sample in zip(row, values, strict=True):
            value += coefficient * sample
        result.append(binary32(value))
    return result


class GammaTwoProfile:
    """Read actual fixture colorants; model the declared float32 native stage boundaries."""

    def __init__(self, packed: bytes, channels: int) -> None:
        """Bind only this known power-two matrix/gray profile and its actual serialized tags."""
        tags = _tags(packed, channels)
        self.channels = channels
        curves = (b"rTRC", b"gTRC", b"bTRC") if channels == RGB_CHANNELS else (b"kTRC",)
        for name in curves:
            _power_two(tags.get(name, b""))
        if any(
            abs(a - b) > 0.5 / 65536 + 1e-12
            for a, b in zip(_xyz(tags.get(b"wtpt", b"")), D50, strict=True)
        ):
            msg = "Known fixture must retain its independently declared D50 white point"
            raise ValueError(msg)
        self.source: Matrix = []
        self.destination: Matrix = []
        if channels == RGB_CHANNELS:
            matrix = [
                list(row)
                for row in zip(
                    *(_xyz(tags.get(name, b"")) for name in (b"rXYZ", b"gXYZ", b"bXYZ")),
                    strict=True,
                )
            ]
            ideal = destination_matrix()
            if any(
                abs(matrix[i][j] - ideal[i][j]) > 0.5 / 65536 + 1e-12
                for i in range(3)
                for j in range(3)
            ):
                msg = "Known fixture colorants differ from independently rounded sRGB/D65 primaries"
                raise ValueError(msg)
            self.source = [[value * PCS_SCALE for value in row] for row in matrix]
            self.destination = [[value / PCS_SCALE for value in row] for row in _inverse(ideal)]

    def decode(self, encoded: list[float]) -> list[float]:
        """Retain binary32 ingress, TRC, PCS and output stages before binary64 alpha composition."""
        if len(encoded) != self.channels:
            msg = "ICC fixture channel count differs from its declared profile"
            raise ValueError(msg)
        curved = [binary32(math.pow(binary32(value), 2.0)) for value in encoded]
        if self.channels == 1:
            return [min(1.0, max(0.0, binary32(binary32(curved[0] * PCS_SCALE) / PCS_SCALE)))]
        pcs = _stage(self.source, curved)
        return [min(1.0, max(0.0, value)) for value in _stage(self.destination, pcs)]


def _input_lut_fixture(profile: bytes, channels: int) -> bytes:
    """Add one bounded intent-specific transform tag to challenge matrix-only admission."""
    count = int.from_bytes(profile[128:132], "big")
    table_end = TAG_TABLE_OFFSET + 12 * count
    changed = bytearray(profile)
    changed[table_end:table_end] = b"\0" * 12
    for index in range(count):
        at = TAG_TABLE_OFFSET + 12 * index
        offset = int.from_bytes(changed[at + 4 : at + 8], "big")
        changed[at + 4 : at + 8] = struct.pack(">I", offset + 12)
    identity = struct.pack(">9i", *(65536 * int(i == j) for i in range(3) for j in range(3)))
    table = bytes(range(256))
    grid = bytes(
        value
        for node in product((0, 255), repeat=channels)
        for value in (node if channels == RGB_CHANNELS else node * RGB_CHANNELS)
    )
    lut = (
        b"mft1"
        + b"\0" * 4
        + bytes((channels, RGB_CHANNELS, 2, 0))
        + identity
        + table * channels
        + grid
        + table * RGB_CHANNELS
    )
    changed[table_end : table_end + 12] = b"A2B1" + struct.pack(">II", len(changed), len(lut))
    changed.extend(lut)
    changed[128:132] = struct.pack(">I", count + 1)
    changed[:4] = struct.pack(">I", len(changed))
    changed[84:100] = b"\0" * 16
    return bytes(changed)


def fixture_controls(profile: bytes) -> None:
    """Retain independent endpoints and refuse intent-LUT precedence in the known fixture model."""
    channels = RGB_CHANNELS if profile[16:20] == b"RGB " else 1
    model = GammaTwoProfile(b"reference\0\0" + zlib.compress(profile), channels)
    if model.decode([0.0] * channels) != [0.0] * channels:
        msg = "Known gamma-two ICC matrix oracle lost its black endpoint"
        raise AssertionError(msg)
    if channels == RGB_CHANNELS:
        actual = model.decode([value / 65535 for value in (8825, 24975, 26925)])
        expected: tuple[float, ...] = (
            0.018139006569981575,
            0.14523032307624817,
            0.16879600286483765,
        )
    else:
        actual, expected = model.decode([0.25]), (0.0625,)
    if any(abs(a - b) > ANCHOR_TOLERANCE for a, b in zip(actual, expected, strict=True)):
        msg = "Independent known-profile float32 endpoint anchor changed"
        raise AssertionError(msg)
    altered = _input_lut_fixture(profile, channels)
    try:
        GammaTwoProfile(b"reference\0\0" + zlib.compress(altered), channels)
    except ValueError as error:
        if "transform-bearing" in str(error):
            return
        raise
    msg = "A known matrix oracle must reject an intent-specific input LUT instead of ignoring it"
    raise AssertionError(msg)
