# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Root-only artifact reservation must not conceal legitimate nested source names."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_gates
import package_source


class SourceReservationTests(unittest.TestCase):
    """Use negative controls for discovery and a real committed-source archive for retention."""

    def test_nested_artifact_names_remain_checked_by_gates_and_ruff(self) -> None:
        """Oversized nested sources and unused Python imports remain real gate failures."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            names = ("src/out", "src/.cache", "docs/dist")
            for name in (*names, "out", ".cache", "dist"):
                directory = root / name
                directory.mkdir(parents=True)
                (directory / "large.cpp").write_text(
                    "x\n" * (check_gates.LINE_LIMITS["production"] + 1), encoding="utf-8"
                )
                (directory / "unused.py").write_text("import os\n", encoding="utf-8")
            failures = check_gates.god_file_errors(root)
            self.assertEqual(
                {line.split()[2] for line in failures}, {f"{name}/large.cpp" for name in names}
            )
            (root / "ruff.toml").write_bytes((ROOT / "ruff.toml").read_bytes())
            result = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "ruff",
                    "check",
                    "--select",
                    "F401",
                    "--output-format",
                    "json",
                    str(root),
                ],
                capture_output=True,
                text=True,
                check=False,
                timeout=30,
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            diagnostics = json.loads(result.stdout)
            self.assertEqual(
                {Path(item["filename"]).relative_to(root).as_posix() for item in diagnostics},
                {f"{name}/unused.py" for name in names},
            )

    def test_committed_nested_sources_are_in_the_actual_source_archive(self) -> None:
        """Select immutable Git blobs and inspect delivered tar members and bytes independently."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            git = shutil.which("git")
            self.assertIsNotNone(git)
            files = {
                "CMakeLists.txt": b"project(DocEnhance VERSION 1.2.3)\n",
                "src/out/kernel.cpp": b"nested numerical source\n",
                "src/.cache/reference.txt": b"nested source fixture\n",
                "docs/dist/design.md": b"nested design\n",
                "out/generated.txt": b"root build output\n",
                ".cache/acquired.txt": b"root cached input\n",
                "dist/retained.txt": b"root exported artifact\n",
            }
            for name, data in files.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            for arguments in (
                [str(git), "init", str(source)],
                [str(git), "-C", str(source), "add", "--force", "."],
                [
                    str(git),
                    "-C",
                    str(source),
                    "-c",
                    "user.name=Fixture",
                    "-c",
                    "user.email=fixture@example.invalid",
                    "-c",
                    "commit.gpgsign=false",
                    "commit",
                    "-m",
                    "source fixture",
                ],
            ):
                subprocess.run(arguments, capture_output=True, check=True, timeout=30)
            with patch.object(package_source, "ROOT", source):
                target = package_source.make_archive(root / "export")
            with tarfile.open(target) as archive:
                included = {member.name for member in archive.getmembers()}
                for name, data in files.items():
                    if name.split("/")[0] in package_source.CHECKOUT_ARTIFACTS:
                        self.assertNotIn(f"docenhance/{name}", included)
                    else:
                        member = archive.extractfile(f"docenhance/{name}")
                        self.assertIsNotNone(member)
                        if member is not None:
                            self.assertEqual(member.read(), data)
