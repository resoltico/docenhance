# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed real linter configuration cannot hide inherited or secondary suppression routes."""

from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

import config_gates
import config_suppressions


class ConfigurationRouteTests(unittest.TestCase):
    """Mutate complete reviewed profiles rather than accepting partial passing fixtures."""

    @override
    def setUp(self) -> None:
        """Own standalone copies of the four actual profile formats."""
        temporary = tempfile.TemporaryDirectory(prefix="configuration-routes-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for name in (".clang-tidy", ".clang-format", "ruff.toml", "mypy.ini"):
            shutil.copyfile(ROOT / name, self.root / name)

    def test_tidy_extra_arguments_are_never_unregistered_routes(self) -> None:
        """Extra compiler arguments are outside the admitted root and nested profile fields."""
        path = self.root / ".clang-tidy"
        original = path.read_text()
        self.assertEqual(config_suppressions.scan(path, ".clang-tidy").errors, [])
        for field in ("ExtraArgs", "ExtraArgsBefore"):
            with self.subTest(field=field):
                path.write_text(original + f"{field}: ['-w']\n")
                self.assertTrue(
                    any(
                        field in error
                        for error in config_suppressions.scan(path, ".clang-tidy").errors
                    )
                )

    def test_ruff_inherited_and_formatter_exclusions_are_refused(self) -> None:
        """An inherited ignore or format-only exclusion cannot escape central admission."""
        path = self.root / "ruff.toml"
        original = path.read_text()
        self.assertEqual(config_suppressions.scan(path, "ruff.toml").errors, [])
        for altered in (
            'extend = "unreviewed.toml"\n' + original,
            original.replace("[format]\n", '[format]\nexclude = ["tools/**"]\n'),
            original.replace("[format]\n", "[format]\nskip-magic-trailing-comma = true\n"),
        ):
            path.write_text(altered)
            self.assertTrue(config_suppressions.scan(path, "ruff.toml").errors)

    def test_mypy_strictness_cannot_be_overridden_by_secondary_fields(self) -> None:
        """Strict=True is insufficient when explicit flags or required error codes weaken it."""
        path = self.root / "mypy.ini"
        original = path.read_text()
        self.assertEqual(config_gates.mypy_errors(self.root, []), [])
        for altered in (
            original + "\ndisallow_untyped_defs = False\n",
            original.replace("extra_checks = True", "extra_checks = False"),
            original.replace("warn_unreachable = True", "warn_unreachable = False"),
            original.replace("ignore-without-code, ", ""),
        ):
            path.write_text(altered)
            self.assertTrue(config_gates.mypy_errors(self.root, []))

    def test_formatter_flow_yaml_regex_and_inherited_style_are_refused(self) -> None:
        """Semantic formatting admission rejects disabling, duplicates and skipped-line patterns."""
        path = self.root / ".clang-format"
        original = path.read_text()
        self.assertEqual(config_gates.clang_format_errors(self.root, path), [])
        for altered in (
            "{DisableFormat: true}\n",
            original + 'OneLineFormatOffRegex: ".*"\n',
            original.replace("BasedOnStyle: LLVM", "BasedOnStyle: None"),
            original + "ColumnLimit: 0\n",
        ):
            path.write_text(altered)
            self.assertTrue(config_gates.clang_format_errors(self.root, path))
