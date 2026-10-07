# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Pinned Clang observes independent unsuppressible body and ABI parameter budgets."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import suppressions
from architecture_api import find_clang_query


class FunctionSizeTests(unittest.TestCase):
    """Every reviewed body dimension still rejects an ABI signature exception."""

    def test_real_clang_preserves_every_body_dimension(self) -> None:
        """Fixed signatures receive no line, statement, branch, nesting or variable waiver."""
        tidy = Path(find_clang_query()).resolve().with_name("clang-tidy")
        cases = {
            "lines": "\n" * 81,
            "statements": "(void)0;" * 61,
            "branches": "if (true) {}" * 16,
            "nesting": "if (true) {" * 5 + "(void)0;" + "}" * 5,
            "variables": "".join(f"int local_{i};" for i in range(21)),
        }
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "abi.cpp"
            header = (
                "int callback(int, int, int, int, int, int) { "
                "// NOLINT(google-readability-function-size)\n"
            )
            command = [
                str(tidy),
                str(source),
                f"--config-file={ROOT / '.clang-tidy'}",
                "--checks=-*,readability-function-size,google-readability-function-size",
                "--warnings-as-errors=*",
                "--",
                "-std=c++23",
            ]
            source.write_text(header + "return 0;\n}\n")
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for dimension, body in cases.items():
                with self.subTest(dimension=dimension):
                    source.write_text(header + body + "\nreturn 0;\n}\n")
                    result = subprocess.run(command, capture_output=True, text=True, check=False)
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("[readability-function-size", result.stdout)
            source.write_text("int helper(int, int, int, int, int, int) { return 0; }\n")
            result = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("[google-readability-function-size", result.stdout)

    def test_body_suppression_is_forbidden_even_when_explicit(self) -> None:
        """Registration cannot legalize suppressing the body size rule."""
        for marker in ("NOLINT", "NOLINTNEXTLINE"):
            result = suppressions.scan_cxx(
                "src/callback.cpp", f"// {marker}(readability-function-size)\nint callback();\n"
            )
            self.assertTrue(any("no waivers" in error for error in result.errors))
            self.assertEqual(result.suppressions, [])
