# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Negative controls for the authoritative development Python minimum."""

from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

from workflow_config import python_errors, workflow


class PythonAuthorityTests(unittest.TestCase):
    """Actual workflow setup and static targets must agree with the reviewed minimum."""

    @override
    def setUp(self) -> None:
        """Own a source-only copy so mutations cannot affect concurrent checks."""
        directory = tempfile.TemporaryDirectory(prefix="python-authority-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for name in ("deps/tools.json", "ruff.toml", "mypy.ini", "README.md"):
            destination = self.root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, destination)
        shutil.copytree(ROOT / ".github/workflows", self.root / ".github/workflows")

    def test_reviewed_configuration(self) -> None:
        """Current declarations agree and GitHub's `on` remains a string key."""
        self.assertEqual(python_errors(self.root), [])
        self.assertIn("on", workflow(self.root / ".github/workflows/ci.yml"))

    def test_setup_drift_and_missing_version(self) -> None:
        """A newer setup or an omitted version fails even beside a correct comment."""
        path = self.root / ".github/workflows/ci.yml"
        original = path.read_text()
        for replacement in ("python-version: '3.13'", "# python-version: '3.12'"):
            with self.subTest(replacement=replacement):
                path.write_text(original.replace("python-version: '3.12'", replacement, 1))
                self.assertTrue(any("ci.yml/structural" in e for e in python_errors(self.root)))

    def test_missing_setup(self) -> None:
        """Removing setup cannot leave a runner-default interpreter as passing evidence."""
        path = self.root / ".github/workflows/ci.yml"
        path.write_text(path.read_text().replace("actions/setup-python@", "example/action@", 1))
        self.assertTrue(any("require one setup" in e for e in python_errors(self.root)))

    def test_static_target_drift(self) -> None:
        """Neither type checking nor syntax lint may silently raise the supported floor."""
        for name, old, new in (
            ("ruff.toml", '"py312"', '"py313"'),
            ("mypy.ini", "python_version = 3.12", "python_version = 3.13"),
        ):
            with self.subTest(name=name):
                path = self.root / name
                path.write_text(path.read_text().replace(old, new))
                self.assertTrue(any("target differs" in e for e in python_errors(self.root)))
