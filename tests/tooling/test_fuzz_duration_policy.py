# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Workload admission and execution must share the reviewed finite exposure."""

from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from workflow_integrity import integrity_errors


class FuzzDurationPolicyTests(unittest.TestCase):
    """Refuse reduced, unlimited, or drifting exposure in actual workflow steps."""

    def test_duration_drift_is_rejected(self) -> None:
        """PR planning/build and weekly input/fallback cannot drift independently."""
        for filename, old, new in (
            ("ci.yml", "--seconds 15 --jobs", "--seconds 0 --jobs"),
            ("ci.yml", "-DDE_FUZZ_SECONDS=15", "-DDE_FUZZ_SECONDS=1"),
            ("weekly.yml", "default: '300'", "default: '0'"),
            ("weekly.yml", "inputs.seconds || '300'", "inputs.seconds || '1800'"),
        ):
            with self.subTest(filename=filename, old=old), tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                for name in (
                    "deps/tools.json",
                    "ruff.toml",
                    "mypy.ini",
                    "README.md",
                    ".pre-commit-config.yaml",
                ):
                    destination = root / name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(ROOT / name, destination)
                shutil.copytree(ROOT / ".github/workflows", root / ".github/workflows")
                path = root / ".github/workflows" / filename
                original = path.read_text()
                self.assertIn(old, original)
                path.write_text(original.replace(old, new, 1))
                self.assertTrue(integrity_errors(root))
