# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Synthetic negative controls for the actual compiled JPEG closure."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from audit_build import jpeg_turbo_failures


class JpegTurboClosureTests(unittest.TestCase):
    """An innocent CMake option alone must not authorize a reviewed exclusion."""

    def test_compiled_and_installed_turbojpeg_are_refused(self) -> None:
        """Refuse upstream TurboJPEG compilation and installed library files."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "deps/jpeg"
            build.mkdir(parents=True)
            libraries = root / "prefix/lib"
            libraries.mkdir(parents=True)
            compile_database = build / "compile_commands.json"
            compile_database.write_text(
                json.dumps([{"file": "/source/src/jdapimin.c"}]), encoding="utf-8"
            )
            (libraries / "libjpeg.a").write_bytes(b"normal static JPEG")
            self.assertEqual(jpeg_turbo_failures(build), [])

            for source in ("/source/src/turbojpeg.c", "/source/src/turbojpeg-mp.c"):
                with self.subTest(source=source):
                    compile_database.write_text(json.dumps([{"file": source}]), encoding="utf-8")
                    self.assertIn("translation unit", jpeg_turbo_failures(build)[0])
            compile_database.write_text(
                json.dumps([{"file": "/source/src/jdapimin.c"}]), encoding="utf-8"
            )
            for library_name in ("libturbojpeg.a", "turbojpeg-static.lib"):
                with self.subTest(library_name=library_name):
                    library = libraries / library_name
                    library.write_bytes(b"forbidden")
                    self.assertIn("API library", jpeg_turbo_failures(build)[0])
                    library.unlink()
            (libraries / "libjpeg.a").unlink()
            libraries.rmdir()
            self.assertIn("directory is missing", jpeg_turbo_failures(build)[0])
