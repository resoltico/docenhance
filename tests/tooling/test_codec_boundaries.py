# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Codec compilation stays independent of filesystem effects and reference cases retain owners."""

from __future__ import annotations

import unittest

from tools_path import ROOT

import architecture
import check_reference_suite


class CodecBoundaryTests(unittest.TestCase):
    """Enforce the effect split without shrinking either native or independent coverage."""

    def test_codec_and_cli_closures_exclude_filesystem_effects(self) -> None:
        """Actual manifest closures keep publication out of raw decoder and CLI clients."""
        manifest = architecture.load_manifest()
        self.assertNotIn("fs", manifest.reach("codec"))
        for layer in ("host", "codec", "color", "fs"):
            self.assertNotIn(layer, manifest.reach("cli"))
        for header in ("filesystem", "fstream"):
            self.assertTrue(architecture.directive_errors(manifest, "codec", header))
            self.assertFalse(architecture.directive_errors(manifest, "fs", header))
        self.assertIn("fopen", manifest.forbidden("codec", "calls"))
        self.assertNotIn("fopen", manifest.forbidden("fs", "calls"))

    def test_relocated_parser_and_admission_remain_independently_compiled(self) -> None:
        """The real standalone compiler source census includes the moved parser and admission."""
        sources = check_reference_suite.reference_sources()
        for name in ("src/app/values.cpp", "src/app/process.cpp", "src/app/dispatch.cpp"):
            self.assertIn(name, sources)
            self.assertTrue((ROOT / name).is_file())
        manifest = architecture.load_manifest()
        self.assertTrue(all(name.split("/")[1] in manifest.layers for name in sources))


if __name__ == "__main__":
    unittest.main()
