# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real formatter and Git boundaries must refuse missing work and moving source tags."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_format
import install_aflplusplus


class DeveloperControlTests(unittest.TestCase):
    """Use actual pinned formatting and native Git rather than fabricated success outputs."""

    def test_real_formatter_refuses_bad_and_absent_sources(self) -> None:
        """A valid source passes; unformatted bytes and zero discovered work are refused."""
        with tempfile.TemporaryDirectory(prefix="formatter-control-") as directory:
            root = Path(directory)
            (root / "deps").mkdir()
            shutil.copy2(ROOT / "deps/tools.json", root / "deps/tools.json")
            shutil.copy2(ROOT / ".clang-format", root / ".clang-format")
            source = root / "main.cpp"
            source.write_text("int main() {\n    return 0;\n}\n")
            with patch.object(check_format, "ROOT", root), patch("sys.argv", ["check_format"]):
                self.assertEqual(check_format.main(), 0)
                source.write_text("int main( ){return 0;}\n")
                self.assertNotEqual(check_format.main(), 0)
                self.assertEqual(source.read_text(), "int main( ){return 0;}\n")
                source.unlink()
                self.assertNotEqual(check_format.main(), 0)

    def test_real_git_refuses_afl_tag_drift(self) -> None:
        """The installer fetches its expected commit and rejects a tag moved to another one."""
        git = shutil.which("git")
        self.assertIsNotNone(git, "The source-tag control requires native Git")
        with tempfile.TemporaryDirectory(prefix="afl-source-control-") as directory:
            root = Path(directory)
            origin = root / "origin"
            origin.mkdir()
            commands = [
                str(git),
                "-C",
                str(origin),
                "-c",
                "core.hooksPath=",
                "-c",
                "user.name=Control",
                "-c",
                "user.email=control@example.invalid",
            ]
            subprocess.run(
                [*commands, "init", "--initial-branch=main"], check=True, capture_output=True
            )
            subprocess.run(
                [*commands, "commit", "--allow-empty", "-m", "Pinned source"],
                check=True,
                capture_output=True,
            )
            pinned = subprocess.check_output([*commands, "rev-parse", "HEAD"], text=True).strip()
            subprocess.run([*commands, "tag", "pinned"], check=True, capture_output=True)
            pin = {"repository": str(origin), "ref": "refs/tags/pinned", "object": pinned}
            target = root / "checkout"
            install_aflplusplus.fetch(pin, target)
            subprocess.run(
                [*commands, "commit", "--allow-empty", "-m", "Different source"],
                check=True,
                capture_output=True,
            )
            subprocess.run([*commands, "tag", "--force", "pinned"], check=True, capture_output=True)
            with self.assertRaisesRegex(install_aflplusplus.InstallError, "not the pinned"):
                install_aflplusplus.fetch(pin, target)
