# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Private stream-only configuration refusal and compiled gzip reintroduction controls."""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from audit_build import zlib_stream_failures


class ZlibRecipeTests(unittest.TestCase):
    """Source drift and cached feature labels cannot bypass the actual source boundary."""

    def test_recipe_refuses_disabled_or_changed_inputs(self) -> None:
        """Run the production adapter: refusal occurs before unreviewed source configuration."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory(prefix="stream-recipe-") as temporary:
            root = Path(temporary)
            upstream = root / "upstream"
            upstream.mkdir()
            source = upstream / "CMakeLists.txt"
            source.write_text('message(FATAL_ERROR "unreviewed source executed")\n')
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            for index, (enabled, expected, diagnostic) in enumerate(
                (
                    ("OFF", digest, "requires stream-only"),
                    ("ON", "a" * 64, "locked upstream recipe"),
                )
            ):
                command = [
                    str(cmake),
                    "-S",
                    str(ROOT / "cmake/dependencies/zlib"),
                    "-B",
                    str(root / f"build-{index}"),
                    "-G",
                    "Ninja",
                    f"-DDE_UPSTREAM_SOURCE={upstream}",
                    f"-DDOCENHANCE_ZLIB_STREAM_ONLY={enabled}",
                    f"-DDOCENHANCE_ZLIB_CMAKE_SHA256={expected}",
                    "-DZLIB_BUILD_SHARED=OFF",
                    "-DZLIB_BUILD_TESTING=OFF",
                ]
                result = subprocess.run(
                    command, capture_output=True, text=True, check=False, timeout=90
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stdout + result.stderr)
                self.assertNotIn("unreviewed source executed", result.stdout + result.stderr)

    def test_actual_gzip_compilation_is_refused(self) -> None:
        """Reintroducing any excluded translation unit fails independently of cache labels."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "compile_commands.json"
            path.write_text(json.dumps([{"file": "/source/deflate.c"}]))
            self.assertEqual(zlib_stream_failures(root), [])
            for name in ("gzclose.c", "gzlib.c", "gzread.c", "gzwrite.c"):
                with self.subTest(name=name):
                    path.write_text(json.dumps([{"file": "/source/" + name}]))
                    self.assertTrue(zlib_stream_failures(root))
