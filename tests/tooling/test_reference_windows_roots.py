# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Observed MSVC simulation grants only the selected compiler and SDK include owners."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import check_reference_suite as suite
import reference_closure as closure
from architecture import ArchitectureError


class ReferenceWindowsRootsTests(unittest.TestCase):
    """Empty implicit lists need real compiler facts, never ambient include permissions."""

    def test_selected_msvc_roots_and_non_msvc_refusal(self) -> None:
        """Only exact observed MSVC simulation admits the independently verified owners."""
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            vc, sdk = base / "vc", base / "sdk"
            include = vc / "include"
            include.mkdir(parents=True)
            compiler = vc / "bin/Hostx64/x64/cl.exe"
            compiler.parent.mkdir(parents=True)
            compiler.write_bytes(b"compiler provenance fixture")
            sdk_include = sdk / "Include/10.0.26100.0/ucrt"
            sdk_include.mkdir(parents=True)
            environment = {
                "VCTOOLSINSTALLDIR": str(vc),
                "WINDOWSSDKDIR": str(sdk),
                "WINDOWSSDKVERSION": "10.0.26100.0/",
                "INCLUDE": str(base / "arbitrary"),
            }
            with (
                patch("reference_closure.sys.platform", "win32"),
                patch.dict("reference_closure.os.environ", environment),
                patch("reference_closure.shutil.which", return_value=str(compiler)),
            ):
                self.assertEqual(
                    closure.system_roots([], simulate_id="MSVC"),
                    sorted([include.resolve(), sdk_include.resolve()]),
                )
                for value in ("", "GNU", "unknown", "MSVC "):
                    with self.subTest(value=value):
                        self.assertEqual(closure.system_roots([], simulate_id=value), [])
                self.assertEqual(closure.system_roots([str(base)], simulate_id="GNU"), [])
                with (
                    patch("reference_closure.shutil.which", return_value=str(base / "cl.exe")),
                    self.assertRaisesRegex(ArchitectureError, "does not own"),
                ):
                    closure.system_roots([], simulate_id="MSVC")
                with (
                    patch.dict("reference_closure.os.environ", {"WINDOWSSDKVERSION": "unowned"}),
                    self.assertRaisesRegex(ArchitectureError, "provenance"),
                ):
                    closure.system_roots([], simulate_id="MSVC")

    def test_generated_fact_is_same_cmake_compiler_observation(self) -> None:
        """The private fact is produced by the configured compiler, with no user override."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "compiler-id.txt").write_text("GNU")
            with (
                patch("check_reference_suite.shutil.which", return_value="cmake"),
                patch("check_reference_suite.subprocess.run") as run,
            ):
                suite.generate_build_facts(directory, "selected-clang++", "4.4")
            cmake = (directory / "configuration/CMakeLists.txt").read_text()
            self.assertIn(
                'file(WRITE "${DE_REFERENCE_SIMULATE_ID}" "${CMAKE_CXX_SIMULATE_ID}")',
                cmake,
            )
            command = run.call_args.args[0]
            self.assertIn("-DCMAKE_CXX_COMPILER:FILEPATH=selected-clang++", command)
            self.assertIn(
                f"-DDE_REFERENCE_SIMULATE_ID:FILEPATH={directory / 'compiler-simulate-id.txt'}",
                command,
            )

    def test_build_reads_exact_fact_and_keeps_owned_header_roots(self) -> None:
        """Missing or unknown facts cannot silently become MSVC observations."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "toolchain-roots.txt").write_text("")
            fact = directory / "compiler-simulate-id.txt"
            for value in (None, "MSVC", "MSVC\n", "GNU"):
                if value is None:
                    fact.unlink(missing_ok=True)
                else:
                    fact.write_text(value)
                with (
                    self.subTest(value=value),
                    patch.object(suite, "validate_source_inventory"),
                    patch.object(suite, "check_environment"),
                    patch.object(suite, "system_roots", return_value=[]) as roots,
                    patch.object(suite, "ordered_map", return_value=[]),
                    patch("check_reference_suite.subprocess.run") as run,
                ):
                    suite.build_reference("selected-clang++", [], [], directory, 1)
                    roots.assert_called_once_with([""], simulate_id=value or "")
                    self.assertEqual(run.call_count, 1)

    def test_private_header_refusal_precedes_allowed_root(self) -> None:
        """Even an explicitly allowed root cannot authorize project-private inputs."""
        private = (ROOT / "out/private.hpp").resolve()
        with (
            patch.object(closure, "header_trace", return_value=[(str(private), str(private))]),
            self.assertRaisesRegex(ArchitectureError, "native/package"),
        ):
            closure.observe_headers("", ROOT, ROOT / "tests/reference.cpp", [ROOT])

    def test_selected_clang_resource_query_adds_only_the_observed_include_owner(self) -> None:
        """Both Clang driver families expose their own resource directory once, with a clock."""
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            resource = base / "selected/lib/clang/20"
            (resource / "include").mkdir(parents=True)
            for identity in ("Clang", "AppleClang"):
                directory = base / identity
                directory.mkdir()
                (directory / "compiler-id.txt").write_text(identity)
                roots = directory / "toolchain-roots.txt"
                roots.write_text("existing-observed-root")
                replies = [
                    subprocess.CompletedProcess([], 0, "", ""),
                    subprocess.CompletedProcess([], 0, str(resource) + "\n", ""),
                ]
                with (
                    self.subTest(identity=identity),
                    patch("check_reference_suite.shutil.which", return_value="cmake"),
                    patch("check_reference_suite.subprocess.run", side_effect=replies) as run,
                ):
                    suite.generate_build_facts(directory, "selected-clang++", "4.4")
                self.assertEqual(run.call_count, 2)
                self.assertEqual(run.call_args.args[0], ["selected-clang++", "-print-resource-dir"])
                self.assertEqual(run.call_args.kwargs["timeout"], 15)
                self.assertTrue(run.call_args.kwargs["check"])
                self.assertEqual(
                    roots.read_text(),
                    "existing-observed-root;" + str(resource.resolve() / "include"),
                )

    def test_malformed_missing_or_broad_clang_resource_cannot_append_roots(self) -> None:
        """Only a real narrow resource directory returned by this driver can add authority."""
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            broad = base / "broad"
            (broad / "include").mkdir(parents=True)
            values = (
                "",
                "relative\n",
                str(broad) + "\n",
                str(base / "missing") + "\n",
                "/a\n/b\n",
                "/a;/b\n",
            )
            for index, value in enumerate(values):
                directory = base / str(index)
                directory.mkdir()
                (directory / "compiler-id.txt").write_text("Clang")
                roots = directory / "toolchain-roots.txt"
                roots.write_text("unchanged")
                replies = [
                    subprocess.CompletedProcess([], 0, "", ""),
                    subprocess.CompletedProcess([], 0, value, ""),
                ]
                with (
                    self.subTest(value=value),
                    patch("check_reference_suite.shutil.which", return_value="cmake"),
                    patch("check_reference_suite.subprocess.run", side_effect=replies),
                    self.assertRaisesRegex(RuntimeError, "selected Clang resource"),
                ):
                    suite.generate_build_facts(directory, "selected-clang++", "4.4")
                self.assertEqual(roots.read_text(), "unchanged")

    def test_clang_query_failure_or_timeout_has_no_ambient_fallback(self) -> None:
        """The exact compiler observation error survives without granting any new roots."""
        errors = (
            subprocess.CalledProcessError(1, ["selected-clang++", "-print-resource-dir"]),
            subprocess.TimeoutExpired(["selected-clang++", "-print-resource-dir"], 15),
        )
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            for index, error in enumerate(errors):
                directory = base / str(index)
                directory.mkdir()
                (directory / "compiler-id.txt").write_text("Clang")
                roots = directory / "toolchain-roots.txt"
                roots.write_text("unchanged")
                with (
                    self.subTest(error=type(error)),
                    patch("check_reference_suite.shutil.which", return_value="cmake"),
                    patch("check_reference_suite.subprocess.run", side_effect=[None, error]),
                    self.assertRaises(type(error)),
                ):
                    suite.generate_build_facts(directory, "selected-clang++", "4.4")
                self.assertEqual(roots.read_text(), "unchanged")
