# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Compile the actual owned tokenizer and compare its roles with an independent Python oracle."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from parameter_fixtures import compile_and_run

sys.path.insert(0, str(ROOT / "tests/cli"))
from argument_grammar import analyze, reviewed_syntax

PROBE = r"""
#include "docenhance/cli/tokenize.hpp"
#include "docenhance/app/options.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
std::string unhex(std::string_view encoded) {
    std::string result;
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t i = 0; i < encoded.size(); i += 2) {
        result.push_back(static_cast<char>((16 * digits.find(encoded[i])) +
                                          digits.find(encoded[i + 1])));
    }
    return result;
}
std::string hex(std::string_view value) {
    std::string result;
    constexpr std::string_view digits = "0123456789abcdef";
    for (unsigned char byte : value) {
        result += digits[byte / 16];
        result += digits[byte % 16];
    }
    return result;
}
int main(int argc, char** argv) {
    std::vector<std::string> owned;
    for (int i = 1; i < argc; ++i) owned.push_back(unhex(argv[i]));
    std::vector<std::string_view> args;
    for (const auto& arg : owned) args.push_back(arg);
    const auto parsed = docenhance::cli::tokenize(args);
    const auto command = parsed ? parsed->command : parsed.error().command;
    const bool json = parsed ? parsed->contains(docenhance::app::OptionId::json)
                             : docenhance::cli::syntax_json(
                                   std::span<const std::string_view>{args});
    std::cout << parsed.has_value() << ' ' << json << ' '
              << docenhance::app::command_name(command) << '\n';
    if (parsed) {
        for (const auto& operand : parsed->operands) std::cout << "O:" << hex(operand) << '\n';
        for (const auto& option : docenhance::app::option_catalog) {
            if (const auto& value = parsed->option(option.id)) {
                std::cout << "V:" << option.name << ':' << hex(*value) << '\n';
            }
        }
    }
}
"""


class TokenizerOracleTests(unittest.TestCase):
    """Native token roles must agree across every option and retained CLI fuzz input."""

    def test_actual_tokenizer_against_independent_roles(self) -> None:
        """Check delimiters, consumed flags, joined empties, repeats, UTF-8 and saved inputs."""
        cli = json.loads((ROOT / "spec/cli-contract.json").read_text())
        methods = json.loads((ROOT / "spec/method-contract.json").read_text())
        options, maximum = reviewed_syntax(cli, methods)
        cases = [
            [],
            ["--json"],
            ["--version"],
            ["--version", "--json"],
            ["process", "--out-dir", "--json"],
            ["process", "--out-dir", "--", "--json"],
            ["process", "--out-dir=", "--json"],
            ["process", "--", "--json"],
            ["process", "--", "--input.png"],
            ["process", "-"],
            ["process", "-h"],
            ["process", "-input.png"],
            ["--json", "process"],
            ["root"],
            ["process", "a", "b", "--json"],
            ["process", "\udcff", "--json"],
            ["verify", "--json"],
        ]
        for name, option in options.items():
            prefix = [] if "root" in option.commands else ["process"]
            cases.extend(
                [
                    [*prefix, name],
                    [*prefix, name, name],
                    [*prefix, name + "="],
                    [*prefix, name + "=--json"],
                    [*prefix, name, "--json"],
                ]
            )
            if not option.flag:
                cases.append([*prefix, name, "x", name, "y"])
        for directory in ("corpus", "regressions"):
            for path in sorted((ROOT / "fuzz" / directory / "cli").iterdir()):
                data = path.read_bytes()[: 12 * 97]
                cases.append(
                    [
                        part[:96].decode("utf-8", errors="surrogateescape")
                        for part in data.split(b"\0")[:12]
                    ]
                )
        with tempfile.TemporaryDirectory(prefix="tokenizer-oracle-") as temporary:
            root = Path(temporary)
            compile_and_run(root, PROBE, sources=(ROOT / "src/cli/tokenize.cpp",))
            executable = root / ("probe.exe" if sys.platform == "win32" else "probe")
            for arguments in cases:
                with self.subTest(arguments=arguments):
                    expected = analyze(arguments, options, maximum)
                    actual = subprocess.run(
                        [
                            str(executable),
                            *[arg.encode("utf-8", "surrogateescape").hex() for arg in arguments],
                        ],
                        check=True,
                        capture_output=True,
                        text=True,
                        timeout=10,
                    ).stdout.splitlines()
                    accepted, selected_json, command = actual[0].split()
                    self.assertEqual(
                        (accepted == "1", selected_json == "1", command),
                        (expected.accepted, expected.json, expected.command),
                    )
                    if expected.accepted:
                        operands, values = [], {}
                        for line in actual[1:]:
                            if line.startswith("O:"):
                                operands.append(bytes.fromhex(line[2:]).decode("utf-8"))
                            else:
                                _, name, value = line.split(":", 2)
                                values[name] = bytes.fromhex(value).decode("utf-8")
                        self.assertEqual(operands, list(expected.operands))
                        self.assertEqual(values, expected.values)
