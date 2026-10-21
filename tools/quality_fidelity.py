# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent final-integer fidelity using identity-bound immutable PNG payloads."""

from __future__ import annotations

import importlib
import sys
from typing import TYPE_CHECKING, Any

from quality_acquisition import acquire_payload, confined
from quality_collection import SEMANTIC_FACTS
from quality_json import ROOT
from quality_protocol import BUNDLE_ROLES

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path

    from continuous_fixtures import Fixture


sys.path.insert(0, str(ROOT / "tests/cli"))
decode_output: Callable[[bytes], Fixture] = importlib.import_module(
    "continuous_fixtures"
).decode_output
MASK_DEPTH = 8
PHOTOMETRIC = ("illumination", "denoising", "restoration", "contrast", "sharpening")


def acquired_payload(directory: Path, identity: dict[str, Any]) -> bytes:
    """Bound one acquisition by its verified expected size; decode these same bytes."""
    return acquire_payload(directory, identity)


def mask_asset(record: dict[str, Any]) -> dict[str, Any] | None:
    """Select only canonical protection, independently of an optional candidate PSF."""
    masks = [asset for asset in record["assets"] if asset["role"] == "protection_mask"]
    if len(masks) > 1 or bool(masks) != (record["request"]["protection"] is not None):
        message = "Protection request and canonical asset disagree"
        raise ValueError(message)
    return masks[0] if masks else None


def check_comparator(candidate: dict[str, Any], baseline: dict[str, Any]) -> None:
    """Require an unenhanced comparator at the actual common terminal boundary."""
    request, other = candidate["request"], baseline["request"]
    output, comparator = request["output"], other["output"]
    expected = {**output, "mode": "gray", "depth": "16"} if output["mode"] == "bw" else output
    if comparator != expected:
        message = "Representation policies differ; enhancement cannot claim conversion benefit"
        raise ValueError(message)
    if (
        candidate["source"] != baseline["source"]
        or candidate["build"] != baseline["build"]
        or request["rotation"] != other["rotation"]
        or request["protection"] != other["protection"]
        or interpretation_policy(candidate) != interpretation_policy(baseline)
        or mask_asset(candidate) != mask_asset(baseline)
    ):
        message = "Comparator source, geometry, interpretation or protection differs"
        raise ValueError(message)
    for name in (*PHOTOMETRIC, "binarization"):
        if other[name] is not None or baseline["execution"][name] is not None:
            message = "Comparator must contain no photometric enhancement or binarization"
            raise ValueError(message)


def interpretation_policy(record: dict[str, Any]) -> dict[str, Any]:
    """Terminal representation differs intentionally for binary versus gray16."""
    return {
        key: value
        for key, value in record["execution"]["interpretation"].items()
        if key not in {"output", "depth_reduced"}
    }


def check_repetition(candidate: dict[str, Any], replicate: dict[str, Any]) -> None:
    """Fresh run identity and identical actual semantic facts are both mandatory."""
    if candidate["record"]["run"] == replicate["record"]["run"]:
        message = "Repeatability requires independently identified executions"
        raise ValueError(message)
    if any(candidate[name] != replicate[name] for name in SEMANTIC_FACTS):
        message = "Repeated processing differs in source, protection or processing/build facts"
        raise ValueError(message)


def decoded_role(root: Path, destination: str, record: dict[str, Any]) -> Fixture:
    """Check encoded identity before independent integer decoding and shape agreement."""
    image = decode_output(acquired_payload(confined(root, destination), record["output"]))
    output = record["output"]
    channels = 1 if image.color == 0 else 3
    if (image.width, image.height, image.depth, channels) != (
        output["width"],
        output["height"],
        output["depth"],
        output["channels"],
    ):
        message = "Decoded output shape differs from verified record"
        raise ValueError(message)
    return image


def protection_pixels(root: Path, destination: str, record: dict[str, Any]) -> Fixture | None:
    """A declared zero count is insufficient: inspect the bound canonical mask itself."""
    asset = mask_asset(record)
    if asset is None:
        return None
    mask = decode_output(acquired_payload(confined(root, destination), asset))
    if (
        mask.color != 0
        or mask.depth != MASK_DEPTH
        or (mask.width, mask.height) != (asset["width"], asset["height"])
        or any(pixel[0] not in (0, 1) for pixel in mask.pixels)
        or sum(pixel[0] != 0 for pixel in mask.pixels) != asset["protected_samples"]
    ):
        message = "Canonical mask shape, logical content or protected count differs"
        raise ValueError(message)
    turns = record["request"]["rotation"] // 90
    expected = (mask.height, mask.width) if turns % 2 else (mask.width, mask.height)
    if expected != (record["output"]["width"], record["output"]["height"]):
        message = "Canonical B-frame mask and rotated output geometry differ"
        raise ValueError(message)
    return mask


def protected_integers(image: Fixture, reference: Fixture, mask: Fixture, turns: int) -> None:
    """Observable final integer equality does not establish hidden binary64 luminance."""
    for v in range(mask.height):
        for u in range(mask.width):
            if mask.pixels[v * mask.width + u][0] == 0:
                continue
            x, y = (
                (u, v),
                (mask.height - 1 - v, u),
                (mask.width - 1 - u, mask.height - 1 - v),
                (v, mask.width - 1 - u),
            )[turns]
            index = y * image.width + x
            if image.pixels[index] != reference.pixels[index]:
                message = "Protected destination differs from the unenhanced comparator"
                raise ValueError(message)


def fidelity(root: Path, assessment: dict[str, Any], records: dict[str, Any]) -> None:
    """Validate every role before conservative missing-evidence handling for protected BW."""
    candidate, baseline, replicate = (records[name] for name in BUNDLE_ROLES)
    check_repetition(candidate, replicate)
    check_comparator(candidate, baseline)
    images = {name: decoded_role(root, assessment[name], records[name]) for name in BUNDLE_ROLES}
    image, reference = images["candidate"], images["baseline"]
    if (image.width, image.height) != (reference.width, reference.height):
        message = "Candidate and comparator geometry differ"
        raise ValueError(message)
    if images["replicate"] != image:
        message = "Independently decoded repeat pixels differ"
        raise ValueError(message)
    masks = {
        name: protection_pixels(root, assessment[name], records[name]) for name in BUNDLE_ROLES
    }
    mask = masks["candidate"]
    if masks["baseline"] != mask or masks["replicate"] != mask:
        message = "Independently decoded canonical masks differ"
        raise ValueError(message)
    if mask is None or not any(pixel[0] for pixel in mask.pixels):
        return
    if candidate["request"]["output"]["mode"] == "bw":
        message = (
            "Missing protection evidence: protected final binary integers cannot be compared "
            "for exact preservation against gray16 integers"
        )
        raise ValueError(message)
    protected_integers(image, reference, mask, candidate["request"]["rotation"] // 90)
