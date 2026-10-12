# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""A minimum-runtime lane must use an actually present matching declared toolchain."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import tools_path

from select_xcode import confirm_version, developer_directory


class XcodeSelectionTests(unittest.TestCase):
    """Missing or mislabeled Xcode cannot silently use the runner default."""

    def test_missing_bundle_and_wrong_version_refuse(self) -> None:
        """Select an existing exact bundle, then independently validate its reported version."""
        self.assertTrue(tools_path.ROOT.is_dir())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, "missing"):
                developer_directory(root, "26.0.1")
            binary = root / "Xcode_26.0.1.app/Contents/Developer/usr/bin/xcodebuild"
            binary.parent.mkdir(parents=True)
            binary.write_text("fixture")
            self.assertEqual(developer_directory(root, "26.0.1"), binary.parent.parent.parent)
        confirm_version("Xcode 26.0.1\nBuild version 17A400\n", "26.0.1")
        for observed in (
            "Xcode 16.4\nBuild version 16F6\n",
            "Xcode 26.0.10\nBuild version 17A400\n",
        ):
            with self.assertRaisesRegex(ValueError, "differs"):
                confirm_version(observed, "26.0.1")
