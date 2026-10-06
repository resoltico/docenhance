#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Negative controls for the TIFF allocator adapter's actual compiled and installed identity."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import audit_build


class TiffRecipeTests(unittest.TestCase):
    """A feature flag or two agreeing mutable copies cannot establish the reviewed build."""

    def test_source_header_and_compilation_are_bound_together(self) -> None:
        """Reject absent hooks, altered copies and compilation of an unadapted source."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "deps/tiff"
            self.assertTrue(audit_build.tiff_recipe_failures(binary))
            owned = binary / "owned-source/libtiff/tif_jpeg.c"
            header = owned.with_name("docenhance_tiff.h")
            installed = root / "prefix/include/docenhance_tiff.h"
            owned.parent.mkdir(parents=True)
            installed.parent.mkdir(parents=True)
            source = (
                b"static int TIFFjpeg_create_decompress(JPEGState *sp)\n"
                b"{ memory->install(memory->context, &sp->cinfo.d); }\n"
                b"static int TIFFjpeg_set_defaults(JPEGState *sp)\n{}\n"
            )
            owned.write_bytes(source)
            header.write_bytes((ROOT / "cmake/dependencies/tiff/docenhance_tiff.h").read_bytes())
            installed.write_bytes(header.read_bytes())
            database = binary / "compile_commands.json"
            database.write_text(json.dumps([{"file": str(owned)}]))
            self.assertTrue(audit_build.tiff_recipe_failures(binary))
            expected = hashlib.sha256(source).hexdigest()
            with patch("audit_build.TIFF_CHARGED_JPEG_SHA256", expected):
                self.assertFalse(audit_build.tiff_recipe_failures(binary))
                installed.write_bytes(b"different ABI")
                self.assertTrue(audit_build.tiff_recipe_failures(binary))
                installed.write_bytes(header.read_bytes())
                database.write_text(json.dumps([{"file": str(root / "unadapted/tif_jpeg.c")}]))
                self.assertTrue(audit_build.tiff_recipe_failures(binary))
                database.write_text(json.dumps([{"file": str(owned)}]))
                owned.write_bytes(source.replace(b"memory->install", b"uncharged->install"))
                self.assertTrue(audit_build.tiff_recipe_failures(binary))

    def test_deflate_completion_requires_the_compiled_adaptation(self) -> None:
        """Matching cache values cannot hide stock ZIP decoding or a changed guard."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "deps/tiff"
            self.assertTrue(audit_build.tiff_zip_failures(binary))
            owned = binary / "owned-source/libtiff/tif_zip.c"
            owned.parent.mkdir(parents=True)
            owned.write_bytes(b"reviewed complete Deflate decoder")
            database = binary / "compile_commands.json"
            database.write_text(json.dumps([{"file": str(owned)}]))
            expected = hashlib.sha256(owned.read_bytes()).hexdigest()
            with patch("audit_build.TIFF_COMPLETE_ZIP_SHA256", expected):
                self.assertFalse(audit_build.tiff_zip_failures(binary))
                database.write_text(json.dumps([{"file": str(root / "stock/tif_zip.c")}]))
                self.assertTrue(audit_build.tiff_zip_failures(binary))
                database.write_text(json.dumps([{"file": str(owned)}]))
                owned.write_bytes(b"incomplete decoder")
                self.assertTrue(audit_build.tiff_zip_failures(binary))


if __name__ == "__main__":
    unittest.main()
