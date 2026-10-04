# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""License checks reject altered terms and obsolete first-party declarations."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_project
import generate_spec
import license_inventory


class ProjectLicenseTests(unittest.TestCase):
    """Check canonical terms independently from generated license declarations."""

    def test_license_text_rejects_replacement_and_amendment(self) -> None:
        """A new label or additional restriction cannot pass the canonical text check."""
        original = (ROOT / "LICENSE").read_bytes()
        self.assertEqual(check_project.license_errors(), [])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for invalid in (b"MIT license", original + b"\nAdditional restriction\n"):
                (root / "LICENSE").write_bytes(invalid)
                with patch.object(check_project, "ROOT", root):
                    self.assertTrue(check_project.license_errors())

    def test_generated_declarations_use_current_license(self) -> None:
        """Generated declarations and checked headers agree on the current terms."""
        self.assertIn("SPDX-License-Identifier: MPL-2.0", generate_spec.HEADER_PREAMBLE)
        self.assertEqual(check_project.LICENSE_TAG, "SPDX-License-Identifier: MPL-2.0")
        self.assertIn("MPL-2.0-licensed", "\n".join(license_inventory.NOTICE_PREAMBLE))

    def test_obsolete_first_party_header_is_refused(self) -> None:
        """An old MIT header cannot pass first-party ownership admission."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "source.cpp"
            path.write_text(
                "// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis\n"
                "// SPDX-License-Identifier: MIT\n"
            )
            with patch.object(check_project, "code_files", return_value=[(path, path.name, "cxx")]):
                self.assertIn(
                    "Missing SPDX-License-Identifier header: source.cpp",
                    check_project.metadata_errors(),
                )
