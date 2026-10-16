# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent role cases freeze the owned grammar before native adapter integration."""

from __future__ import annotations

import sys
import unittest

from tools_path import ROOT

sys.path.insert(0, str(ROOT / "tests/cli"))
from argument_grammar import OptionSyntax, analyze

COMMANDS = {"root": 0, "process": 1, "verify": 1, "methods": 1, "version": 0}
OPTIONS = {
    "--json": OptionSyntax(flag=True, commands=frozenset(COMMANDS)),
    "--help": OptionSyntax(flag=True, commands=frozenset(COMMANDS)),
    "--version": OptionSyntax(flag=True, commands=frozenset({"root"})),
    "--out-dir": OptionSyntax(flag=False, commands=frozenset({"process"})),
    "--output-mode": OptionSyntax(flag=False, commands=frozenset({"process"})),
}


class ArgumentGrammarTests(unittest.TestCase):
    """Token roles precede domain admission; transport fallback is restricted to syntax errors."""

    def test_value_roles_and_missing_operand_admission(self) -> None:
        """A consumed JSON spelling remains a value when tokenization succeeded."""
        roles = analyze(["process", "--out-dir", "--json"], OPTIONS, COMMANDS)
        self.assertTrue(roles.accepted)
        self.assertFalse(roles.json)
        self.assertEqual(roles.operands, ())
        self.assertEqual(roles.values["--out-dir"], "--json")
        roles = analyze(["process", "--out-dir", "D", "--json"], OPTIONS, COMMANDS)
        self.assertTrue(roles.accepted)
        self.assertTrue(roles.json)
        self.assertEqual(roles.operands, ())
        for command in ("process", "verify"):
            self.assertTrue(analyze([command], OPTIONS, COMMANDS).accepted)
            self.assertTrue(analyze([command, "--help"], OPTIONS, COMMANDS).accepted)

    def test_empty_joined_value_does_not_consume_next_flag(self) -> None:
        """Empty presence is app input, rather than an invitation to consume another token."""
        roles = analyze(["process", "--out-dir=", "--json"], OPTIONS, COMMANDS)
        self.assertTrue(roles.accepted)
        self.assertTrue(roles.json)
        self.assertEqual(roles.values["--out-dir"], "")

    def test_syntax_failures_and_raw_fallback(self) -> None:
        """Unknown, repeated, missing-valued and joined flags use the exact raw boundary."""
        cases = (
            (["process", "--out-dir"], False),
            (["process", "--json", "--out-dir"], True),
            (["process", "--out-dir", "--json", "--wat"], True),
            (["process", "--out-dir", "--", "--json", "--wat"], False),
            (["version", "--json", "--json"], True),
            (["version", "--json="], False),
            (["version", "--json=false"], False),
            (["version", "--", "--json"], False),
            (["process", "a", "b", "--json"], True),
            (["--json", "process"], True),
            (["--version", "--json"], True),
            (["process", "bad\udc80", "--json"], True),
        )
        for arguments, json_mode in cases:
            with self.subTest(arguments=arguments):
                roles = analyze(arguments, OPTIONS, COMMANDS)
                self.assertFalse(roles.accepted)
                self.assertEqual(roles.json, json_mode)

    def test_delimiter_and_single_dash_operand_identity(self) -> None:
        """Only double-dash names are options; literal paths retain their byte spelling."""
        for name in ("-", "-h", "-input.png", "e\u0301.png"):
            roles = analyze(["process", name, "--out-dir", "D"], OPTIONS, COMMANDS)
            self.assertTrue(roles.accepted)
            self.assertEqual(roles.operands, (name,))
            self.assertNotIn("--help", roles.values)
        roles = analyze(["process", "--out-dir", "D", "--", "--input.png"], OPTIONS, COMMANDS)
        self.assertTrue(roles.accepted)
        self.assertEqual(roles.operands, ("--input.png",))
        self.assertFalse(analyze(["process", "--input.png"], OPTIONS, COMMANDS).accepted)

    def test_successful_flag_can_follow_a_consumed_delimiter(self) -> None:
        """A valued '--' is not a delimiter after successful tokenization."""
        roles = analyze(["process", "--out-dir", "--", "--json"], OPTIONS, COMMANDS)
        self.assertTrue(roles.accepted)
        self.assertTrue(roles.json)
        self.assertEqual(roles.values["--out-dir"], "--")


if __name__ == "__main__":
    unittest.main()
