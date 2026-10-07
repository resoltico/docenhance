# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Negative controls for effective size policy and ignore-independent Python discovery."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import check_gates
import config_gates


class SizePolicyTests(unittest.TestCase):
    """Configuration mutations must fail even when their comments explain the relaxation."""

    def test_tidy_root_rules_and_every_budget_are_protected(self) -> None:
        """Removing each real check or raising any existing budget is refused."""
        original = (ROOT / ".clang-tidy").read_text()
        changes = {
            "readability-*,": "clang-analyzer-*,",
            "Threshold: '20'": "Threshold: '200'",
            "IgnoreMacros: 'true'": "IgnoreMacros: 'false'",
            "LineThreshold: '80'": "LineThreshold: '800'",
            "StatementThreshold: '60'": "StatementThreshold: '600'",
            "BranchThreshold: '15'": "BranchThreshold: '150'",
            "ParameterThreshold: '5'": "ParameterThreshold: '50'",
            "NestingThreshold: '4'": "NestingThreshold: '40'",
            "VariableThreshold: '20'": "VariableThreshold: '200'",
            "google-readability-function-size,": "",
            "ParameterThreshold: '4294967295'": "ParameterThreshold: '5'",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / ".clang-tidy"
            path.write_text(original)
            self.assertEqual(config_gates.clang_tidy_errors(root, path), [])
            for before, after in changes.items():
                with self.subTest(before=before):
                    self.assertIn(before, original)
                    path.write_text(original.replace(before, after))
                    self.assertTrue(config_gates.clang_tidy_errors(root, path))
            path.write_text(original + "\nExcludeHeaderFilterRegex: '.*'\n")
            self.assertTrue(config_gates.clang_tidy_errors(root, path))

    def test_nested_effective_check_order_is_guarded(self) -> None:
        """Disable patterns cannot hide checks; later explicit re-enabling remains admitted."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "src/.clang-tidy"
            path.parent.mkdir()
            prefix = (
                "# readability-function-size: an explained relaxation\nInheritParentConfig: true\n"
            )
            for checks in (
                "-readability-function-size",
                "-google-readability-function-size",
                "-readability-*",
                "-*",
                (
                    "-*,readability-function-siz?,readability-function-cognitive-complexity,"
                    "google-readability-function-size"
                ),
                (
                    "-*,readability-[f]unction-size,readability-function-cognitive-complexity,"
                    "google-readability-function-size"
                ),
            ):
                path.write_text(prefix + f"Checks: '{checks}'\n")
                self.assertTrue(config_gates.clang_tidy_errors(root, path))
            path.write_text(
                prefix + "Checks: '-readability-function-size,readability-function-size'\n"
            )
            self.assertEqual(config_gates.clang_tidy_errors(root, path), [])

    def test_python_limits_and_ignored_prefixes_are_protected(self) -> None:
        """Documented exact, prefix and per-file ignore forms cannot silence complexity."""
        original = (ROOT / "ruff.toml").read_text()
        mutations = [
            original.replace(f"{key} = {value}", f"{key} = {value * 10}")
            for key, value in (
                ("max-complexity", 10),
                ("max-args", 5),
                ("max-branches", 12),
                ("max-returns", 6),
                ("max-statements", 50),
            )
        ]
        mutations += [
            original.replace('    "COM812",', f'    "{code}", # Explained\n    "COM812",')
            for code in ("C901", "C", "PLR09", "PLR0915")
        ]
        mutations.append(original.replace("respect-gitignore = false", "respect-gitignore = true"))
        mutations.append(original.replace("[lint]\n", '[lint]\nexclude = ["tools/**"]\n'))
        mutations.append(original + '\n[lint.extend-per-file-ignores]\n"tools/*.py" = ["C901"]\n')
        mutations.append(
            original.replace('    "INP001",', '    "C901", # Explained\n    "INP001",')
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "ruff.toml"
            path.write_text(original)
            self.assertEqual(config_gates.ruff_errors(root), [])
            for changed in mutations:
                path.write_text(changed)
                self.assertTrue(config_gates.ruff_errors(root))

    def test_production_test_prefix_does_not_raise_file_limit(self) -> None:
        """The test allowance follows actual responsibility roots rather than basename."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("src/test_adapter.cpp", "tests/adapter.cpp"):
                path = root / name
                path.parent.mkdir()
                path.write_text("// bounded fixture\n" * 350)
            errors = check_gates.god_file_errors(root)
            self.assertEqual(len(errors), 1)
            self.assertIn("src/test_adapter.cpp", errors[0])
            self.assertIn("production limit 300", errors[0])

    def test_real_ruff_checks_gitignored_python_sources(self) -> None:
        """Actual Ruff rejects an unused import hidden by Git while root artifacts stay excluded."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            git = shutil.which("git")
            if git is None:
                self.fail("Git is required for the ignored-source boundary fixture")
            subprocess.run([git, "init", "-q", str(root)], check=True)
            (root / "ruff.toml").write_bytes((ROOT / "ruff.toml").read_bytes())
            (root / ".gitignore").write_text("hidden.py\n")
            (root / "hidden.py").write_text("import os\n")
            (root / "out").mkdir()
            (root / "out/artifact.py").write_text("import os\n")
            result = subprocess.run(
                [sys.executable, "-m", "ruff", "check", "--output-format", "concise", str(root)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("hidden.py:1:8: F401", result.stdout)
            self.assertNotIn("artifact.py", result.stdout)


if __name__ == "__main__":
    unittest.main()
