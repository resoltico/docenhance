# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Equivalent compiler traces and complete bound AST results protect faster architecture checks."""

from __future__ import annotations

import ntpath
import re
import subprocess
import tempfile
import unittest
from pathlib import Path, PureWindowsPath
from unittest.mock import patch

from tools_path import ROOT

from compiler_probes import compiler_probe

import architecture
import architecture_api
import architecture_boundary
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

    def test_path_patterns_preserve_literal_sets_without_repeating_common_prefix(self) -> None:
        """Escaping, exact spelling and prefix-related names survive smaller path predicates."""
        manifest = architecture.load_manifest()
        beginning = 'cxxThrowExpr(isExpansionInFileMatching("'
        ending = '"))'
        sets: tuple[set[str], ...] = (
            set(),
            {"/one.[scope]$.cpp"},
            {"/different/a.cpp", "Z:/other/b.cpp"},
            {"/shared/a", "/shared/ab"},
            {"/shared $[scope]/a.cpp", "/shared $[scope]/b.cpp"},
            {"/re\u0301/あ.cpp", "/re\u0301/い.cpp"},
            {r"C:\root\(scope)\a.cpp", r"C:\root\(scope)\b.cpp"},
        )
        for paths in sets:
            with self.subTest(paths=paths):
                matcher = architecture_api.matchers(manifest, {"image": paths})[0][1]
                self.assertTrue(matcher.startswith(beginning) and matcher.endswith(ending))
                expression = matcher[len(beginning) : -len(ending)]
                original = "^(" + "|".join(re.escape(path) for path in sorted(paths)) + ")$"
                if len(paths) <= 1:
                    self.assertEqual(expression, original)
                candidates = paths | {
                    "",
                    "/shared/a.cpp",
                    "/shared/aXcpp",
                    "/shared/a-foreign",
                    "/ré/あ.cpp",
                    "C:/root/(scope)/a.cpp",
                    *(path + "-foreign" for path in paths),
                }
                for spelling in candidates:
                    self.assertEqual(
                        bool(re.fullmatch(expression, spelling)),
                        bool(re.fullmatch(original, spelling)),
                        spelling,
                    )
                if paths == {"/shared $[scope]/a.cpp", "/shared $[scope]/b.cpp"}:
                    self.assertLess(len(expression), len(original))

    def test_grouped_matches_preserve_every_rule_and_overlap(self) -> None:
        """Compare exact rule counts against individually traversed predicates on real Clang."""
        with tempfile.TemporaryDirectory(prefix="architecture $[scope]-") as directory:
            root = Path(directory)
            entry = compiler_probe(
                root,
                root / "src/image/probe.cpp",
                '#include "probe.cpp.hpp"\n#include <cstdlib>\n'
                "namespace unrelated { void probe() { "
                "auto terminate=&std::exit; (void)terminate; "
                "try { throw 1; } catch(...) { auto*p=new int(1); delete p; } } }\n",
            )
            header = Path(entry["file"] + ".hpp")
            header.write_text(
                "#pragma once\nnamespace unrelated_header { inline void probe() { throw 2; } }\n"
            )
            trace = compile_db.included_headers(entry, syntax=True)
            observed = {
                spelling for pair in trace if Path(pair[1]) == header.resolve() for spelling in pair
            }
            self.assertTrue(observed, repr(trace)[-2048:])
            rules = architecture_api.matchers(
                architecture.load_manifest(), {"image": {entry["file"], *observed}}
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
            diagnostic = "\n".join(
                f"{name}: {str(value)[:1024]}\n...tail: {str(value)[-1024:]}"
                for name, value in (
                    ("observed header", observed),
                    ("expected counts", expected),
                    ("found reports", found),
                    ("query output", grouped.stdout),
                )
            )
            self.assertEqual(expected[rules[0][0]], 2, diagnostic)
            self.assertEqual(expected["overlapping throw restriction"], 2, diagnostic)
            throw_reports = [
                report
                for report in found
                if report.split("\n", 1)[0] == f"{entry['file']}: {rules[0][0]}"
            ]
            self.assertEqual(len(throw_reports), 1, diagnostic)
            throw_files = re.findall(
                r'^(.+):\d+:\d+: note: "rule_0" binds here$', throw_reports[0], re.MULTILINE
            )
            self.assertEqual(
                {Path(name).name for name in throw_files},
                {Path(entry["file"]).name, header.name},
                diagnostic,
            )
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
            self.assertTrue(any(header.name in report for report in found), diagnostic)
            Path(entry["file"]).write_text(
                "namespace broken { invalid syntax }\n", encoding="utf-8"
            )
            with self.assertRaises(architecture.ArchitectureError):
                architecture_api.api_violations(query, root, entry, rules)
            with self.assertRaises(architecture.ArchitectureError):
                architecture_build.header_violations(
                    architecture.load_manifest(), entry, root, "app", owner=None
                )

    def test_namespaces_follow_the_fully_qualified_layer_owner(self) -> None:
        """Private descendants are owned; leaf-name and similarly prefixed impostors are not."""
        manifest = architecture.load_manifest()
        query = architecture_api.find_clang_query()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for namespace, accepted in (
                ("docenhance::record", True),
                ("docenhance::record::scalar_read", True),
                ("docenhance::record::wire_read::detail", True),
                ("docenhance::recorded", False),
                ("record", False),
                ("foreign::record", False),
                ("docenhance::image::record", False),
                ("foreign::docenhance::record", False),
            ):
                with self.subTest(namespace=namespace):
                    entry = compiler_probe(
                        root, root / "src/record/probe.cpp", f"namespace {namespace} {{}}\n"
                    )
                    errors = architecture_api.api_violations(
                        query,
                        root,
                        entry,
                        architecture_api.matchers(manifest, {"record": {entry["file"]}}),
                    )
                    self.assertEqual(not errors, accepted, errors)
                    if not accepted:
                        self.assertTrue(any("declares a namespace" in error for error in errors))

    def test_exception_containment_stays_at_effect_owning_boundaries(self) -> None:
        """Host contains foreign exceptions; image and numeric kernels still cannot catch."""
        manifest = architecture.load_manifest()
        query = architecture_api.find_clang_query()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for layer in ("host", "image", "methods"):
                with self.subTest(layer=layer):
                    entry = compiler_probe(
                        root,
                        root / f"src/{layer}/probe.cpp",
                        f"namespace docenhance::{layer} {{ "
                        "void probe() { try {} catch(...) {} } }\n",
                    )
                    errors = architecture_api.api_violations(
                        query,
                        root,
                        entry,
                        architecture_api.matchers(manifest, {layer: {entry["file"]}}),
                    )
                    catches = [error for error in errors if "catches an exception" in error]
                    self.assertEqual(bool(catches), layer != "host", errors)
                    self.assertEqual(bool(errors), layer != "host", errors)

    def test_layer_roots_preserve_native_windows_boundaries(self) -> None:
        """Native case, separators, drives and UNC shares retain the old Path classification."""
        manifest = architecture.load_manifest()
        context = compile_db.LayerRoots(ROOT)
        for repository, paths in (
            (
                "C:/Repo",
                (
                    "c:/rEPO/src/image/a.cpp",
                    "C:/Repo/src/Image/a.cpp",
                    "D:/Repo/src/image/a.cpp",
                    "C:/Repo/src-other/image/a.cpp",
                    "C:/Repo/src",
                    "C:/Repo/include/docenhance/core/a.hpp",
                ),
            ),
            (
                "//Server/Share/Repo",
                (
                    "//server/share/repo/src/image/a.cpp",
                    "//server/other/Repo/src/image/a.cpp",
                    "//Server/Share/Repo-other/src/image/a.cpp",
                ),
            ),
        ):
            root = PureWindowsPath(repository)
            bases = (root / "include/docenhance", root / "src")
            for name in paths:
                resolved = PureWindowsPath(name)
                expected = None
                for base in bases:
                    if resolved.is_relative_to(base):
                        parts = resolved.relative_to(base).parts
                        candidate = parts[0] if parts else ""
                        expected = candidate if candidate in manifest.layers else None
                        break
                with (
                    patch.object(context, "bases", bases),
                    patch("compile_db.os.path", ntpath),
                    patch("compile_db.Path") as path,
                ):
                    path.return_value.resolve.return_value = resolved
                    self.assertEqual(context.layer_of(manifest, name), expected, name)

    def test_trace_classification_retains_fresh_symlink_identity(self) -> None:
        """One trace context cannot retain an old symlink target or admit a sibling-prefix path."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "src/image/a.hpp"
            core = root / "src/core/a.hpp"
            outside = root / "src-other/image/a.hpp"
            for path in (image, core, outside):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n")
            context = compile_db.LayerRoots(root)
            link = root / "observed.hpp"
            link.symlink_to(image)
            self.assertEqual(context.layer_of(manifest, str(link)), "image")
            link.unlink()
            link.symlink_to(core)
            self.assertEqual(context.layer_of(manifest, str(link)), "core")
            link.unlink()
            link.symlink_to(outside)
            self.assertIsNone(context.layer_of(manifest, str(link)))
            self.assertIsNone(context.layer_of(manifest, str(root / "src")))

    def test_trace_errors_and_ownership_equal_original_classifier(self) -> None:
        """Fresh native compiler traces retain error order, private ownership and path spellings."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / "src/core/private.hpp"
            foreign = root / "include/docenhance/host/probe.hpp"
            for header in (private, foreign):
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text("#pragma once\n")
            entry = compiler_probe(
                root,
                root / "src/image/probe.cpp",
                '#include "../core/private.hpp"\n'
                '#include "../../include/docenhance/host/probe.hpp"\n'
                '#include "../../include/docenhance/host/probe.hpp"\n',
            )

            class OriginalRoots:
                def __init__(self) -> None:
                    self.root = root

                def layer_of(self, model: architecture.Manifest, path: str) -> str | None:
                    resolved = Path(path).resolve()
                    for base in (
                        self.root.resolve() / "include/docenhance",
                        self.root.resolve() / "src",
                    ):
                        if resolved.is_relative_to(base):
                            parts = resolved.relative_to(base).parts
                            candidate = parts[0] if parts else ""
                            return candidate if candidate in model.layers else None
                    return None

            with (
                patch.object(architecture_build, "ROOT", root),
                patch.object(compile_db, "ROOT", root),
                patch.object(architecture_boundary, "ROOT", root),
            ):
                actual = architecture_build.reach_violations(manifest, entry, root)
                with patch.object(architecture_build, "LayerRoots", OriginalRoots):
                    original = architecture_build.reach_violations(manifest, entry, root)
            self.assertEqual(actual, original)
            self.assertTrue(any("private production header" in error for error in actual[0]))
            self.assertTrue(any("reaches host" in error for error in actual[0]))
            self.assertIn("core", actual[1])
            self.assertIn("host", actual[1])

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
