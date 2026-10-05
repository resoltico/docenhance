# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Counterexamples at compiler, include ownership and final CMake target boundaries."""

from __future__ import annotations

import copy
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

from compiler_probes import compiler_probe, fixture_compiler

import architecture
import architecture_api
import architecture_boundary
import architecture_build
import compile_db


class RestrictionPolicyTests(unittest.TestCase):
    """A shared baseline cannot silently lose enforcement at a native adapter."""

    def test_analysis_tools_only_layout_does_not_invent_a_compiler(self) -> None:
        """Intel macOS uses Apple clang while versioned Linux/Windows installations own clang."""
        with tempfile.TemporaryDirectory() as directory:
            tools = Path(directory) / "tools"
            tools.mkdir()
            query = tools / "clang-query"
            query.write_text("analysis-tool layout", encoding="utf-8")
            self.assertFalse(query.with_name("clang++").exists())
            self.assertEqual(fixture_compiler("Darwin", query), Path("/usr/bin/clang++"))
            self.assertEqual(fixture_compiler("Linux", query), query.resolve().with_name("clang++"))
            self.assertEqual(
                fixture_compiler("Windows", query), query.resolve().with_name("clang++.exe")
            )

    def test_fatal_parse_failure_cannot_be_zero_match_success(self) -> None:
        """A real missing header can answer every matcher with zero; the fatal diagnostic wins."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = compiler_probe(
                root, root / "src/image/probe.cpp", '#include "intentionally-absent.hpp"\n'
            )
            with self.assertRaisesRegex(architecture.ArchitectureError, "clang-query failed"):
                architecture_api.api_violations(
                    architecture_api.find_clang_query(),
                    root,
                    entry,
                    architecture_api.matchers(manifest, {"image": {entry["file"]}}),
                )

    def test_native_adapter_inherits_all_baseline_restrictions(self) -> None:
        """Denoising cannot allocate C blocks, escape process control or own new threads."""
        manifest = architecture.load_manifest()
        for call in ("calloc", "realloc", "aligned_alloc", "strdup", "popen", "exit"):
            self.assertIn(call, manifest.forbidden("denoise", "calls"))
        for header in ("latch", "semaphore", "stop_token"):
            self.assertIn(header, manifest.forbidden("denoise", "headers"))
        self.assertNotIn("stop_token", manifest.forbidden("core", "headers"))
        self.assertNotIn("thread", manifest.forbidden("exec", "headers"))
        self.assertNotIn("filesystem", manifest.forbidden("io", "headers"))
        self.assertIn("fopen", manifest.forbidden("exec", "calls"))

    def test_obsolete_or_misspelled_policy_is_rejected(self) -> None:
        """Unknown policy, nonbaseline exceptions and invalid interface permissions fail closed."""
        original = json.loads(architecture.MANIFEST.read_text(encoding="utf-8"))
        for field, value in (
            ("forbidden_calls", []),
            ("may_threads", True),
            ("allow_calls", ["unreviewed"]),
            ("interface_packages", ["opencv"]),
            ("may_thread", "false"),
        ):
            changed = copy.deepcopy(original)
            changed["layers"]["image"][field] = value
            with (
                self.subTest(field=field),
                self.assertRaises(architecture.ArchitectureError),
            ):
                architecture.Manifest(changed)
        changed = copy.deepcopy(original)
        changed.pop("restrictions")
        with self.assertRaises(architecture.ArchitectureError):
            architecture.Manifest(changed)

    def test_real_ast_detects_transitive_thread_aliases_macros_and_function_pointers(
        self,
    ) -> None:
        """Library declarations cannot evade ownership through aliases or transitive includes."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "provider.hpp").write_text(
                "#include <thread>\n#include <future>\n#include <cstdlib>\n",
                encoding="utf-8",
            )
            entry = compiler_probe(
                root,
                root / "src/image/probe.cpp",
                '#include "../../provider.hpp"\n'
                "#define START(Kind) Kind worker{[] {}}\n"
                "namespace docenhance::image { void probe() { using Worker=std::jthread; "
                "START(Worker); auto task=std::async([] {}); auto terminate=&std::exit; "
                "(void)task; (void)terminate; } }\n",
            )
            errors = architecture_api.api_violations(
                architecture_api.find_clang_query(),
                root,
                entry,
                architecture_api.matchers(manifest, {"image": {entry["file"]}}),
            )
            self.assertTrue(any("constructs a thread" in error for error in errors), errors)
            self.assertTrue(any("thread-launch API" in error for error in errors), errors)
            self.assertTrue(any("forbidden function" in error for error in errors), errors)
            # The same thread declarations are authorized at the scheduler, without running them.
            entry = compiler_probe(
                root,
                root / "src/exec/probe.cpp",
                '#include "../../provider.hpp"\nnamespace docenhance::exec { '
                "void probe() { using Worker=std::jthread; Worker worker{[] {}}; "
                "auto task=std::async([] {}); (void)task; } }\n",
            )
            errors = architecture_api.api_violations(
                architecture_api.find_clang_query(),
                root,
                entry,
                architecture_api.matchers(manifest, {"exec": {entry["file"]}}),
            )
            self.assertEqual(errors, [])

    def test_ast_scope_uses_owned_file_identity_not_a_src_directory_fragment(self) -> None:
        """A foreign throw stays outside policy even when the checkout's ancestor is named src."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            project = base / "src/project [scope]"
            project.mkdir(parents=True)
            vendor = base / "src/vendor.hpp"
            vendor.write_text(
                "namespace vendor { inline void fail() { throw 1; } }\n", encoding="utf-8"
            )
            source = project / "src/image/probe.cpp"
            text = (
                f'#include "{vendor.as_posix()}"\n'
                "namespace docenhance::image { void probe() {} }\n"
            )
            entry = compiler_probe(project, source, text)
            files = {"image": {entry["file"]}}
            errors = architecture_api.api_violations(
                architecture_api.find_clang_query(),
                project,
                entry,
                architecture_api.matchers(manifest, files),
            )
            self.assertEqual(errors, [])
            compiler_probe(
                project, source, text.replace("void probe() {}", "void probe() { throw 1; }")
            )
            errors = architecture_api.api_violations(
                architecture_api.find_clang_query(),
                project,
                entry,
                architecture_api.matchers(manifest, files),
            )
            self.assertTrue(any("throws instead" in error for error in errors), errors)


class HeaderOwnershipTests(unittest.TestCase):
    """Private implementation headers cannot become public or cross-layer interfaces."""

    def test_private_header_spelling_and_interface_ownership(self) -> None:
        """Parent traversal, absolute and src-root spellings are all resolved before admission."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / "src/io/lease.hpp"
            private.parent.mkdir(parents=True)
            private.write_text("#pragma once\n", encoding="utf-8")
            (root / "src/host").mkdir()
            with patch.object(architecture_boundary, "ROOT", root):
                for spelled in ("../io/lease.hpp", str(private), "src/io/lease.hpp"):
                    with self.subTest(spelled=spelled):
                        self.assertIsNotNone(
                            architecture_boundary.private_directive_error(
                                "host", root / "src/host/probe.cpp", spelled
                            )
                        )
                self.assertIsNone(
                    architecture_boundary.private_directive_error(
                        "io", root / "src/io/probe.cpp", "lease.hpp"
                    )
                )
                self.assertIsNotNone(
                    architecture_boundary.private_directive_error(
                        "io",
                        root / "include/docenhance/io/api.hpp",
                        str(private),
                        public=True,
                    )
                )

    def test_compiler_trace_rejects_transitive_private_header(self) -> None:
        """A public layer edge never grants access to that layer's private implementation."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / "src/io/lease.hpp"
            private.parent.mkdir(parents=True)
            private.write_text("#pragma once\n", encoding="utf-8")
            (root / "bridge.hpp").write_text('#include "src/io/lease.hpp"\n', encoding="utf-8")
            entry = compiler_probe(
                root, root / "src/host/probe.cpp", '#include "../../bridge.hpp"\n'
            )
            with (
                patch.object(compile_db, "ROOT", root),
                patch.object(architecture_boundary, "ROOT", root),
            ):
                errors, _ = architecture_build.reach_violations(manifest, entry, root)
            self.assertTrue(any("private production header" in error for error in errors), errors)


class ClientReachTests(unittest.TestCase):
    """The CLI harness's actual includes and links respect its reviewed pure root closure."""

    def test_real_client_includes_and_links_are_checked(self) -> None:
        """Actual host includes, links and missing registrations fail; a pure client passes."""
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            host = root / "include/docenhance/host/probe.hpp"
            host.parent.mkdir(parents=True)
            host.write_text("#pragma once\n", encoding="utf-8")
            source = root / "fuzz/cli.cpp"
            compiler_probe(root, source, '#include "../include/docenhance/host/probe.hpp"\n')
            record = root / "architecture-client-probe.json"
            record.write_text(
                json.dumps(
                    {
                        "target": "probe",
                        "source": "fuzz/cli.cpp",
                        "links": "de_cli;de_host",
                    }
                ),
                encoding="utf-8",
            )
            with (
                patch.object(architecture_build, "ROOT", root),
                patch.object(compile_db, "ROOT", root),
                patch.object(architecture_boundary, "ROOT", root),
            ):
                errors = architecture_build.client_violations(manifest, root)
                self.assertTrue(any("cannot link de_host" in e for e in errors), errors)
                self.assertTrue(any("reaches host" in e for e in errors), errors)
                compiler_probe(root, source, "// A client with no production include.\n")
                record.write_text(
                    json.dumps({"target": "probe", "source": "fuzz/cli.cpp", "links": "de_cli"}),
                    encoding="utf-8",
                )
                self.assertEqual(architecture_build.client_violations(manifest, root), [])
                entry = compiler_probe(
                    root,
                    source,
                    '#ifdef LEAK_HOST\n#include "../include/docenhance/host/probe.hpp"\n#endif\n',
                )
                (root / "compile_commands.json").write_text(
                    json.dumps([entry, {**entry, "command": entry["command"] + " -DLEAK_HOST"}]),
                    encoding="utf-8",
                )
                errors = architecture_build.client_violations(manifest, root)
                self.assertTrue(any("reaches host" in error for error in errors), errors)
                entry = compiler_probe(root, source, "// Pure in both compositions.\n")
                (root / "compile_commands.json").write_text(
                    json.dumps([entry, entry]), encoding="utf-8"
                )
                replay = root / "architecture-client-replay.json"
                replay.write_text(
                    json.dumps(
                        {
                            "target": "replay",
                            "source": "fuzz/cli.cpp",
                            "links": "de_cli",
                        }
                    ),
                    encoding="utf-8",
                )
                self.assertEqual(architecture_build.client_violations(manifest, root), [])
                replay.unlink()
                record.unlink()
                self.assertTrue(architecture_build.client_violations(manifest, root))


class CMakeRegistrationTests(unittest.TestCase):
    """Final target state must detect late links independently of recorded lists."""

    def test_late_actual_link_is_rejected_by_real_configure(self) -> None:
        """An unchanged registration cannot conceal a later addition to the actual target."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "spec").mkdir()
            (root / "spec/architecture.json").write_bytes(architecture.MANIFEST.read_bytes())
            text = (
                "cmake_minimum_required(VERSION 4.4)\nproject(Probe LANGUAGES NONE)\n"
                f'include("{(ROOT / "cmake/ArchitectureChecks.cmake").as_posix()}")\n'
                "add_library(options INTERFACE)\nadd_library(DocEnhance::options ALIAS options)\n"
                "add_library(de_core STATIC IMPORTED)\nadd_library(de_host INTERFACE)\n"
                "set_property(TARGET de_core PROPERTY LINK_LIBRARIES DocEnhance::options)\n"
                "de_register_target(de_core LAYER core)\n"
            )
            project = root / "CMakeLists.txt"
            project.write_text(text, encoding="utf-8")
            accepted = subprocess.run(
                [
                    str(cmake),
                    "-G",
                    "Ninja",
                    "-S",
                    str(root),
                    "-B",
                    str(root / "accepted"),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(accepted.returncode, 0, accepted.stdout + accepted.stderr)
            project.write_text(
                text + "set_property(TARGET de_core APPEND PROPERTY LINK_LIBRARIES de_host)\n",
                encoding="utf-8",
            )
            refused = subprocess.run(
                [
                    str(cmake),
                    "-G",
                    "Ninja",
                    "-S",
                    str(root),
                    "-B",
                    str(root / "refused"),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(refused.returncode, 0)
            self.assertIn("actual links differ", refused.stdout + refused.stderr)
