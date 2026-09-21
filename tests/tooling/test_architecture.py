# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Tests for the layer manifest and the rules that read it.

The rules that need a configured build are exercised by the `architecture` test inside the build.
What is tested here is everything else: that the committed manifest is consistent with the sources
and the documentation, and that each rule rejects what it claims to reject.
"""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any, override

from tools_path import ROOT

import architecture
import architecture_build

LAYER_COUNT = 9


def manifest_of(
    layers: dict[str, Any], packages: dict[str, dict[str, list[str]]] | None = None
) -> architecture.Manifest:
    """A manifest built from literal layers, for rules that must reject something."""
    return architecture.Manifest({"layers": layers, "packages": packages or {}})


def layer(**overrides: object) -> dict[str, Any]:
    """A layer declaration with every required field present."""
    return {
        "target": "de_probe",
        "summary": "probe",
        "uses": [],
        "external": [],
        "forbidden_headers": [],
        "forbidden_calls": [],
    } | overrides


class ManifestTests(unittest.TestCase):
    """The committed manifest is valid and describes the real layer graph."""

    @override
    def setUp(self) -> None:
        """Read the committed manifest."""
        self.manifest = architecture.load_manifest()

    def test_committed_manifest_is_valid(self) -> None:
        """Every layer names declared layers and packages, and nothing depends on itself."""
        self.assertEqual(architecture.manifest_errors(self.manifest), [])
        self.assertEqual(len(self.manifest.layers), LAYER_COUNT)

    def test_reach_follows_the_declared_graph(self) -> None:
        """A layer reaches what its dependencies reach, and no more."""
        self.assertEqual(self.manifest.reach("core"), set())
        self.assertEqual(self.manifest.reach("methods"), {"image", "exec", "core"})
        self.assertNotIn("cli", self.manifest.reach("app"))
        self.assertEqual(self.manifest.package_reach("core"), set())
        self.assertIn("nlohmann_json", self.manifest.package_reach("cli"))

    def test_cycles_are_rejected(self) -> None:
        """A layer that reaches itself is a failure, however long the path."""
        cycle = manifest_of(
            {
                "a": layer(uses=["b"]),
                "b": layer(uses=["c"]),
                "c": layer(uses=["a"]),
            }
        )
        self.assertEqual(len(architecture.manifest_errors(cycle)), len(cycle.layers))

    def test_undeclared_names_are_rejected(self) -> None:
        """A layer cannot use a layer or a package the manifest does not declare."""
        unknown = manifest_of({"a": layer(uses=["ghost"], external=["phantom"])})
        self.assertEqual(len(architecture.manifest_errors(unknown)), 2)

    def test_headers_map_to_their_package(self) -> None:
        """A header is attributed to the package that declares it, by prefix or by name."""
        self.assertEqual(self.manifest.package_of_header("CLI/CLI.hpp"), "CLI11")
        self.assertEqual(self.manifest.package_of_header("nlohmann/json_fwd.hpp"), "nlohmann_json")
        self.assertEqual(self.manifest.package_of_header("lcms2.h"), "lcms")
        self.assertIsNone(self.manifest.package_of_header("string_view"))


class SourceRuleTests(unittest.TestCase):
    """The rules that read the sources and the documentation."""

    @override
    def setUp(self) -> None:
        """Read the committed manifest."""
        self.manifest = architecture.load_manifest()

    def test_repository_passes_every_source_rule(self) -> None:
        """Layout, include directives and the documented layer table all agree."""
        self.assertEqual(architecture.source_violations(), [])

    def test_allowed_directives_are_accepted(self) -> None:
        """What the layers really include is what the manifest allows."""
        for directive, name in (
            ("docenhance/core/result.hpp", "contract"),
            ("nlohmann/json.hpp", "report"),
            ("CLI/CLI.hpp", "cli"),
            ("docenhance/version.hpp", "app"),
            ("string_view", "core"),
        ):
            self.assertEqual(architecture.directive_errors(self.manifest, name, directive), [])

    def test_forbidden_directives_are_rejected(self) -> None:
        """A layer, a package or a standard header the layer may not name is a failure."""
        for directive, name in (
            ("docenhance/cli/run.hpp", "app"),
            ("docenhance/app/dispatch.hpp", "core"),
            ("CLI/CLI.hpp", "app"),
            ("nlohmann/json.hpp", "app"),
            ("nlohmann/json.hpp", "image"),
            ("opencv2/core.hpp", "methods"),
            ("fstream", "core"),
            ("iostream", "app"),
            ("docenhance/ghost/thing.hpp", "cli"),
        ):
            self.assertEqual(
                len(architecture.directive_errors(self.manifest, name, directive)),
                1,
                f"{name} must not name {directive}",
            )

    def test_public_headers_may_not_name_a_package(self) -> None:
        """A third-party header is allowed in an implementation file, never in an interface."""
        directive = "#include <nlohmann/json.hpp>\n"
        self.assertEqual(
            architecture.file_errors(self.manifest, "report", directive, public=False), []
        )
        self.assertEqual(
            len(architecture.file_errors(self.manifest, "report", directive, public=True)), 1
        )

    def test_documentation_must_list_every_layer(self) -> None:
        """A layer missing from the documented table is reported against the document."""
        errors = architecture.document_errors(manifest_of({"ghost": layer(target="de_ghost")}))
        self.assertIn("architecture.md: the layer table has no row for de_ghost", errors)

    def test_documentation_must_agree_with_the_manifest(self) -> None:
        """A row that contradicts the manifest is reported, not silently believed."""
        wrong = manifest_of({"core": layer(target="de_core", summary="something else")})
        self.assertTrue(
            any("the manifest says" in error for error in architecture.document_errors(wrong))
        )

    def test_layout_must_match_the_manifest(self) -> None:
        """A declared layer with no directory of its own is reported."""
        errors = architecture.layout_errors(manifest_of({"ghost": layer()}))
        self.assertIn("layer ghost has no include/docenhance/ghost directory", errors)


class LinkRuleTests(unittest.TestCase):
    """Targets link what their own files use, and use what they link."""

    @override
    def setUp(self) -> None:
        """Read the committed manifest."""
        self.manifest = architecture.load_manifest()

    def test_direct_use_reads_the_include_directives(self) -> None:
        """Only what a target's own files name counts, and not its own layer."""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "probe.cpp"
            source.write_text(
                '#include "docenhance/core/result.hpp"\n'
                '#include "docenhance/app/dispatch.hpp"\n'
                "#include <CLI/CLI.hpp>\n"
                "// #include <nlohmann/json.hpp>\n",
                encoding="utf-8",
            )
            layers, packages = architecture_build.direct_use(self.manifest, [str(source)], "app")
            self.assertEqual(layers, {"core"})
            self.assertEqual(packages, {"CLI11"})

    def test_registration_covers_every_layer(self) -> None:
        """The manifest and the build's own registration name the same targets."""
        # The newest build tree: another preset's tree may predate a layer this one declares.
        registrations = sorted(
            ROOT.glob("out/*/app/architecture-targets.json"), key=lambda p: p.stat().st_mtime
        )
        if not registrations:
            self.skipTest("no configured build tree to read the registration from")
        registration = registrations[-1]
        records = json.loads(registration.read_text(encoding="utf-8"))
        self.assertEqual(
            {record["layer"] for record in records.values()}, set(self.manifest.layers)
        )


if __name__ == "__main__":
    unittest.main()
