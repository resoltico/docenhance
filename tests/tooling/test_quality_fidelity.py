# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Synthetic byte-bound fidelity controls, without natural-document quality claims."""

from __future__ import annotations

import copy
import hashlib
import tempfile
import unittest
from pathlib import Path
from typing import Any

from tools_path import prepare_tool_imports

from quality_evidence_fixture import continuous_fixtures

from quality_fidelity import check_comparator, fidelity

prepare_tool_imports()


def identity(path: str, payload: bytes) -> dict[str, Any]:
    """The verifier identity is fixed before any later payload substitution."""
    return {"path": path, "sha256": hashlib.sha256(payload).hexdigest(), "bytes": len(payload)}


def record(root: Path, name: str, *, binary: bool, mask: tuple[int, ...] | None) -> dict[str, Any]:
    """A small current-wire semantic fixture; no native success is simulated."""
    directory = root / name
    directory.mkdir()
    image = continuous_fixtures.Fixture(
        2, 1, ((0,), (1,)) if binary else ((0,), (65535,)), depth=1 if binary else 16
    )
    payload = image.encoded()
    (directory / "result.png").write_bytes(payload)
    request: dict[str, Any] = {
        "output": {
            "mode": "bw" if binary else "gray",
            "depth": None if binary else "16",
            "alpha": "white",
            "profile_policy": "embedded",
        },
        "rotation": 0,
        "protection": None,
        **dict.fromkeys(
            ("illumination", "denoising", "restoration", "contrast", "sharpening", "binarization")
        ),
    }
    execution: dict[str, Any] = dict.fromkeys(
        (
            "illumination",
            "denoising",
            "restoration",
            "contrast",
            "sharpening",
            "binarization",
        )
    )
    execution["interpretation"] = {
        "decoded": {"width": 2, "height": 1},
        "source_orientation": 1,
        "output": {"bit_depth": image.depth},
        "depth_reduced": binary,
    }
    assets = []
    if mask is not None:
        (directory / "assets").mkdir()
        encoded = continuous_fixtures.Fixture(2, 1, tuple((value,) for value in mask)).encoded()
        (directory / "assets/protect-mask.png").write_bytes(encoded)
        assets.append(
            {
                **identity("assets/protect-mask.png", encoded),
                "role": "protection_mask",
                "width": 2,
                "height": 1,
                "frame": "metadata_oriented_source",
                "protected_samples": sum(value != 0 for value in mask),
            }
        )
        request["protection"] = {"supplied": {"sha256": "a" * 64, "bytes": 1}}
    return {
        "record": {"run": name},
        "source": {"sha256": "b" * 64},
        "build": {},
        "request": request,
        "execution": execution,
        "assets": assets,
        "output": {
            **identity("result.png", payload),
            "width": 2,
            "height": 1,
            "depth": image.depth,
            "channels": 1,
        },
    }


def roles(root: Path, *, binary: bool, mask: tuple[int, ...] | None) -> dict[str, Any]:
    """Repeat differs only in its independent run identity."""
    candidate = record(root, "candidate", binary=binary, mask=mask)
    baseline = record(root, "baseline", binary=False, mask=mask)
    replicate = record(root, "replicate", binary=binary, mask=mask)
    return {"candidate": candidate, "baseline": baseline, "replicate": replicate}


class QualityFidelityTests(unittest.TestCase):
    """Identity and observability failures abstain rather than infer protection or harm."""

    def test_continuous_and_unmasked_or_zero_mask_binary(self) -> None:
        """Continuous and unmasked or zero mask binary."""
        for binary, mask in ((False, (1, 0)), (True, None), (True, (0, 0))):
            with self.subTest(binary=binary, mask=mask), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                records = roles(root, binary=binary, mask=mask)
                fidelity(root, {name: name for name in records}, records)

    def test_nonzero_binary_protection_is_missing_evidence(self) -> None:
        """Nonzero binary protection is missing evidence."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            records = roles(root, binary=True, mask=(1, 0))
            with self.assertRaisesRegex(ValueError, "Missing protection evidence"):
                fidelity(root, {name: name for name in records}, records)

    def test_valid_changed_output_or_mask_cannot_enter_decode_as_verified(self) -> None:
        """Valid changed output or mask cannot enter decode as verified."""
        for changed in ("result.png", "assets/protect-mask.png"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                records = roles(root, binary=False, mask=(1, 0))
                fixture = (
                    continuous_fixtures.Fixture(2, 1, ((0,), (1,)), depth=16)
                    if changed == "result.png"
                    else continuous_fixtures.Fixture(2, 1, ((0,), (1,)))
                )
                replacement = fixture.encoded()
                path = root / "candidate" / changed
                previous = path.read_bytes()
                expected = (
                    records["candidate"]["output"]
                    if changed == "result.png"
                    else records["candidate"]["assets"][0]
                )
                self.assertNotEqual(replacement, previous)
                self.assertEqual(hashlib.sha256(previous).hexdigest(), expected["sha256"])
                self.assertNotEqual(hashlib.sha256(replacement).hexdigest(), expected["sha256"])
                path.write_bytes(replacement)
                with self.assertRaisesRegex(ValueError, "Evidence .*declared (identity|length)"):
                    fidelity(root, {name: name for name in records}, records)

    def test_declared_zero_cannot_make_nonzero_protection_vacuous(self) -> None:
        """Declared zero cannot make nonzero protection vacuous."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            records = roles(root, binary=True, mask=(1, 0))
            for value in records.values():
                value["assets"][0]["protected_samples"] = 0
            with self.assertRaisesRegex(ValueError, "protected count differs"):
                fidelity(root, {name: name for name in records}, records)

    def test_png8_nonlogical_mask_bytes_are_not_canonical(self) -> None:
        """Png8 nonlogical mask bytes are not canonical."""
        for value in (2, 255):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                records = roles(root, binary=False, mask=(value, 0))
                with self.assertRaisesRegex(ValueError, "logical content"):
                    fidelity(root, {name: name for name in records}, records)

    def test_noop_selection_is_not_an_unenhanced_comparator(self) -> None:
        """Noop selection is not an unenhanced comparator."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            records = roles(root, binary=True, mask=None)
            baseline = copy.deepcopy(records["baseline"])
            baseline["request"]["sharpening"] = {"parameters": {"strength": 0}}
            with self.assertRaisesRegex(ValueError, "no photometric"):
                check_comparator(records["candidate"], baseline)

    def test_common_policy_and_geometry_errors_precede_binary_abstention(self) -> None:
        """Common policy and geometry errors precede binary abstention."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            records = roles(root, binary=True, mask=(1, 0))
            records["baseline"]["request"]["output"]["alpha"] = "black"
            with self.assertRaisesRegex(ValueError, "Representation policies differ"):
                fidelity(root, {name: name for name in records}, records)
