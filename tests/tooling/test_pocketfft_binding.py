# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Execute the actual provider import on selected-file substitution counterexamples."""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT


class PocketfftBindingTests(unittest.TestCase):
    """A correct canonical installed copy cannot excuse a different selected header."""

    def test_selected_owned_header_bytes_and_file_symlink_are_admitted_truthfully(self) -> None:
        """Run the production import owner, without unrelated packages or numeric facades."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory(prefix="pocketfft-binding-") as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "deps").mkdir(parents=True)
            prefix = root / "prefix"
            canonical = prefix / "include/pocketfft"
            canonical.mkdir(parents=True)
            header = canonical / "pocketfft_hdronly.h"
            header.write_bytes(b"// Reviewed admission fixture, never a numerical FFT.\n")
            digest = hashlib.sha256(header.read_bytes()).hexdigest()
            (source / "deps/features.json").write_text(
                json.dumps(
                    {
                        "dependencies": {
                            "pocketfft": {
                                "POCKETFFT_NO_MULTITHREADING": True,
                                "POCKETFFT_CACHE_SIZE": "0",
                                "DOCENHANCE_POCKETFFT_HEADER_SHA256": digest,
                            }
                        },
                    }
                )
            )
            (source / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 4.4)
project(Provider NONE)
set(DE_DEPENDENCY_PREFIX "{prefix.as_posix()}")
list(APPEND CMAKE_MODULE_PATH "{ROOT.as_posix()}/cmake")
include(PocketFFT)
get_target_property(selected DE::pocketfft INTERFACE_INCLUDE_DIRECTORIES)
file(WRITE "${{PROJECT_BINARY_DIR}}/selected.txt" "${{selected}}")
""")
            result = self.configure(str(cmake), source, root / "canonical")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((root / "canonical/selected.txt").read_text(), canonical.as_posix())
            alternate = prefix / "alternate"
            alternate.mkdir()
            (alternate / header.name).write_bytes(b"// Unreviewed selected header.\n")
            result = self.configure(str(cmake), source, root / "wrong", alternate)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("selected header digest differs", result.stdout + result.stderr)
            self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), digest)
            foreign = root / "foreign.h"
            foreign.write_bytes(header.read_bytes())
            escaped = prefix / "escaped"
            escaped.mkdir()
            (escaped / header.name).symlink_to(foreign)
            result = self.configure(str(cmake), source, root / "symlink", escaped)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("outside the owned dependency prefix", result.stdout + result.stderr)

    @staticmethod
    def configure(
        cmake: str, source: Path, build: Path, selected: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        """Use the real cache override which the production find_path continues to admit."""
        command = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja"]
        if selected is not None:
            command.append(f"-DDE_POCKETFFT_INCLUDE={selected.as_posix()}")
        return subprocess.run(command, capture_output=True, text=True, check=False, timeout=90)
