# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Equivalent compiler traces and complete bound AST results protect faster architecture checks."""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

from compiler_probes import compiler_probe

import architecture
import architecture_api
import architecture_build
import architecture_matches
import compile_db


class ArchitectureExecutionTests(unittest.TestCase):
    """No source, rule, overlapping match or syntax failure disappears when parsing is reduced."""

    def test_preprocessing_preserves_exact_actual_include_trace(self) -> None:
        """Conditional, transitive and repeated includes retain spelling, identity and order."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "first.hpp").write_text('#include "second.hpp"\n')
            (root / "second.hpp").write_text("#pragma once\n#include <cstddef>\n")
            (root / "inactive.hpp").write_text("#error inactive header must not be read\n")
            entry = compiler_probe(
                root,
                root / "src/image/probe.cpp",
                '#include "../../first.hpp"\n#include "../../first.hpp"\n'
                '#if 0\n#include "../../inactive.hpp"\n#endif\n'
                "namespace docenhance::image { int probe() { return 0; } }\n",
            )
            syntax = compile_db.included_headers(entry)
            preprocess = compile_db.included_headers(entry, syntax=False)
            self.assertEqual(preprocess, syntax)
            self.assertTrue(any(path.endswith("second.hpp") for _, path in preprocess))
            self.assertFalse(any(path.endswith("inactive.hpp") for _, path in preprocess))

    def test_grouped_matches_preserve_every_rule_and_overlap(self) -> None:
        """Compare exact rule counts against individually traversed predicates on real Clang."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = compiler_probe(
                root,
                root / "src/image/probe.cpp",
                "#include <cstdlib>\nnamespace unrelated { void probe() { "
                "auto terminate=&std::exit; (void)terminate; "
                "try { throw 1; } catch(...) { auto*p=new int(1); delete p; } } }\n",
            )
            rules = architecture_api.matchers(
                architecture.load_manifest(), {"image": {entry["file"]}}
            )
            rules.append(("overlapping throw restriction", rules[0][1]))
            query = architecture_api.find_clang_query()
            arguments = [arg for _, matcher in rules for arg in ("-c", f"match {matcher}")]
            baseline = subprocess.run(
                [query, "-p", str(root), entry["file"], *arguments],
                capture_output=True,
                text=True,
                check=True,
            )
            counts = re.findall(r"^(\d+) match(?:es)?\.$", baseline.stdout, re.MULTILINE)
            expected = {rule: int(count) for (rule, _), count in zip(rules, counts, strict=True)}
            grouped = subprocess.run(
                [query, "-p", str(root), entry["file"], *architecture_matches.commands(rules)],
                capture_output=True,
                text=True,
                check=True,
            )
            found = architecture_matches.violations(entry["file"], rules, grouped.stdout)
            for rule, count in expected.items():
                reports = [
                    text for text in found if text.split("\n", 1)[0] == f"{entry['file']}: {rule}"
                ]
                self.assertEqual(len(reports), int(count != 0), rule)
                if reports:
                    self.assertEqual(
                        len(re.findall(r'"rule_\d+" binds here', reports[0])), count, rule
                    )
            self.assertTrue(any("overlapping throw restriction" in report for report in found))
            compiler_probe(root, Path(entry["file"]), "namespace broken { invalid syntax }\n")
            with self.assertRaises(architecture.ArchitectureError):
                architecture_api.api_violations(query, root, entry, rules)
            with self.assertRaises(architecture.ArchitectureError):
                architecture_build.header_violations(
                    architecture.load_manifest(), entry, root, "contract", owner=None
                )

    def test_missing_or_unattributed_query_results_are_not_success(self) -> None:
        """Missing groups/bindings and altered summary counts fail closed."""
        rules = [("throw", 'cxxThrowExpr(isExpansionInFileMatching("owned"))')]
        for output in (
            "",
            "1 match.\n",
            'Match #1:\nfile:1:1: note: "rule_99" binds here\n1 match.\n',
        ):
            with self.assertRaises(architecture.ArchitectureError):
                architecture_matches.violations(str(ROOT / "src/probe.cpp"), rules, output)
