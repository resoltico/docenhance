# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Generation uses the admitted formatter without mutating source or non-C++ outputs."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

from parameter_cpp import format_outputs


class GeneratedFormatTests(unittest.TestCase):
    """The same stylesheet/tool owner serves generation and verification."""

    def test_format_in_memory_only(self) -> None:
        """Canonical output is returned while existing files and JSON retain their bytes."""
        with tempfile.TemporaryDirectory(prefix="generated-format-") as directory:
            header = Path(directory) / "intended.hpp"
            header.write_text("preserve existing bytes\n")
            json_path = Path(directory) / "contract.json"
            outputs = {header: "struct Value{int field;};\n", json_path: '{"value": 1}\n'}
            result = format_outputs(outputs, ROOT)
            self.assertIn("    int field;", result[header])
            self.assertEqual(result[json_path], outputs[json_path])
            self.assertEqual(header.read_text(), "preserve existing bytes\n")
            self.assertFalse(json_path.exists())
            self.assertEqual(outputs[header], "struct Value{int field;};\n")

    def test_unavailable_or_wrong_major_refused(self) -> None:
        """No ambient fallback or unformatted success can conceal a missing admitted tool."""
        with (
            patch("parameter_cpp.find_clang_format", return_value=(None, "wrong major")),
            self.assertRaisesRegex(ValueError, "wrong major"),
        ):
            format_outputs({}, ROOT)


if __name__ == "__main__":
    unittest.main()
