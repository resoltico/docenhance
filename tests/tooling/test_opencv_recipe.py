# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual FFT source identity and compilation admission, independent of feature cache flags."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import audit_build


class OpencvRecipeTests(unittest.TestCase):
    """The private corrections must be the ones compiled from unchanged locked input."""

    def test_fft_source_identity_and_actual_compilation(self) -> None:
        """Reject absent, stock, altered and duplicated native FFT build inputs."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original = root / "source/modules/core/src" / "dxt.cpp"
            owned = root / "deps/opencv/owned-source/dxt.cpp"
            original.parent.mkdir(parents=True)
            owned.parent.mkdir(parents=True)
            original.write_bytes(b"locked original")
            digest = hashlib.sha256(original.read_bytes()).hexdigest()

            def check() -> list[str]:
                return audit_build.opencv_dft_failures(
                    root / "deps/opencv", root / "source", digest
                )

            self.assertTrue(check())
            owned.write_bytes(b"reviewed ownership and typed dispatch corrections")
            expected = hashlib.sha256(owned.read_bytes()).hexdigest()
            database = root / "deps/opencv/compile_commands.json"
            database.write_text(json.dumps([{"file": str(owned)}]))
            with patch("audit_build.OPENCV_OWNED_DXT_SHA256", expected):
                self.assertFalse(check())
                database.write_text(json.dumps([{"file": str(original)}]))
                self.assertTrue(check())
                database.write_text(json.dumps([{"file": str(owned)}, {"file": str(original)}]))
                self.assertTrue(check())
                database.write_text(json.dumps([{"file": str(owned)}]))
                original.write_bytes(b"changed source cache")
                self.assertTrue(check())
                original.write_bytes(b"locked original")
                owned.write_bytes(b"stock raw pointer factory")
                self.assertTrue(check())

    def test_required_fft_source_binding_is_complete(self) -> None:
        """The CPU recipe requires ownership, typed dispatch and a complete source SHA."""
        features = json.loads((ROOT / "deps/features.json").read_text())["dependencies"]["opencv"]
        self.assertTrue(features["DOCENHANCE_OPENCV_OWNED_DFT_CONTEXTS"] is True)
        self.assertTrue(features["DOCENHANCE_OPENCV_TYPED_DFT_DISPATCH"] is True)
        self.assertRegex(features["DOCENHANCE_OPENCV_DXT_SHA256"], r"^[0-9a-f]{64}$")
