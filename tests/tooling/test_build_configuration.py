# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Real CMake negative controls for configuration ownership and package-provider confinement."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

CONFIGURE_TIMEOUT = 90


class BuildConfigurationTests(unittest.TestCase):
    """Run production policy modules with actual native compiler/cache state."""

    @override
    def setUp(self) -> None:
        """Each test owns fresh sources and build state; no dependency is downloaded."""
        self.temporary = tempfile.TemporaryDirectory(prefix="docenhance-config-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.environment = os.environ.copy()
        for name in ("CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CC", "CXX"):
            self.environment.pop(name, None)
        self.cmake = shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "Real configuration tests require CMake")

    def configure(self, *arguments: str, build: str = "build") -> subprocess.CompletedProcess[str]:
        """Preserve the full diagnostic on failure and require the real Ninja generator."""
        return subprocess.run(
            [
                str(self.cmake),
                "-S",
                str(self.source),
                "-B",
                str(self.root / build),
                "-G",
                "Ninja",
                *arguments,
            ],
            env=self.environment,
            capture_output=True,
            text=True,
            check=False,
            timeout=CONFIGURE_TIMEOUT,
        )

    def policy_project(self) -> None:
        """Use production policy and build binding without downloading native dependencies."""
        text = f"""cmake_minimum_required(VERSION 4.4)
include("{ROOT.as_posix()}/cmake/BuildPolicy.cmake")
project(Configuration C CXX)
set(PROJECT_SOURCE_DIR "{ROOT.as_posix()}")
list(APPEND CMAKE_MODULE_PATH "{ROOT.as_posix()}/cmake")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(Options)
include(CompilerPolicy)
de_validate_native_configuration()
include(DependencyPlan)
include(BuildIdentity)
de_commit_build_identity()
"""
        (self.source / "CMakeLists.txt").write_text(text)

    def assert_refused(self, result: subprocess.CompletedProcess[str], diagnostic: str) -> None:
        """A rejection is meaningful only when the intended production guard caused it."""
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(diagnostic, result.stdout + result.stderr)

    def test_json_destruction_recipe_requires_reviewed_source(self) -> None:
        """The real dependency recipe refuses disabled correction, drift and absent cleanup."""
        recipe = ROOT / "cmake/dependencies/json/CMakeLists.txt"
        (self.source / "CMakeLists.txt").write_text(recipe.read_text())
        upstream = self.root / "upstream"
        header = upstream / "single_include/nlohmann/json.hpp"
        header.parent.mkdir(parents=True)
        header.write_bytes(b"unreviewed header")
        settings = (
            f"-DDE_UPSTREAM_SOURCE={upstream}",
            "-DJSON_MultipleHeaders=OFF",
            "-DJSON_BuildTests=OFF",
        )
        self.assert_refused(
            self.configure(*settings, "-DDOCENHANCE_JSON_BOUNDED_DESTRUCTION=OFF"),
            "reviewed bounded JSON destruction configuration",
        )
        self.assert_refused(
            self.configure(
                *settings,
                "-DDOCENHANCE_JSON_BOUNDED_DESTRUCTION=ON",
                "-DDOCENHANCE_JSON_HEADER_SHA256=" + "0" * 64,
            ),
            "Review bounded JSON destruction",
        )
        self.assert_refused(
            self.configure(
                *settings,
                "-DDOCENHANCE_JSON_BOUNDED_DESTRUCTION=ON",
                "-DDOCENHANCE_JSON_HEADER_SHA256="
                + hashlib.sha256(header.read_bytes()).hexdigest(),
            ),
            "reviewed JSON cleanup sequence is absent",
        )

    def test_configuration_binding_refuses_reuse_and_shared_prefix(self) -> None:
        """Identical reuse succeeds; changing the build kind or sharing another prefix fails."""
        self.policy_project()
        arguments = ("-DCMAKE_BUILD_TYPE=Debug", "-DDE_TOOLCHAIN=platform")
        first = self.configure(*arguments)
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        second = self.configure(*arguments)
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        changed = self.configure("-DCMAKE_BUILD_TYPE=Release", "-DDE_TOOLCHAIN=platform")
        self.assert_refused(changed, "Build inputs changed")
        foreign = self.configure(
            *arguments, f"-DDE_DEPENDENCY_PREFIX={self.root / 'shared'}", build="foreign"
        )
        self.assert_refused(foreign, "private prefix")

    def test_application_child_uses_only_its_bound_parent_prefix(self) -> None:
        """A populated owned prefix is valid for the fresh child, never for a different owner."""
        self.policy_project()
        arguments = ("-DCMAKE_BUILD_TYPE=Debug", "-DDE_TOOLCHAIN=platform")
        first = self.configure(*arguments)
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        parent = self.root / "build"
        (parent / "prefix/include").mkdir(parents=True)
        child = self.configure(
            *arguments,
            "-DDE_SUPERBUILD=OFF",
            f"-DDE_SUPERBUILD_BINARY={parent}",
            f"-DDE_DEPENDENCY_PREFIX={parent / 'prefix'}",
            build="build/app",
        )
        self.assertEqual(child.returncode, 0, child.stdout + child.stderr)
        bad = self.configure(
            *arguments,
            "-DDE_SUPERBUILD=OFF",
            f"-DDE_SUPERBUILD_BINARY={parent}",
            f"-DDE_DEPENDENCY_PREFIX={parent / 'prefix'}",
            build="different",
        )
        self.assert_refused(bad, "owner's app directory")

    def test_unbound_cache_is_rejected_without_migration(self) -> None:
        """Deleting the binding from a populated cache cannot silently create a new identity."""
        self.policy_project()
        arguments = ("-DCMAKE_BUILD_TYPE=Debug", "-DDE_TOOLCHAIN=platform")
        first = self.configure(*arguments)
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        (self.root / "build/build-identity.json").unlink()
        self.assert_refused(self.configure(*arguments), "no configuration binding")

    def test_ambient_flags_and_second_test_switch_are_refused(self) -> None:
        """Flags must not disappear between parent/children or disable test registration."""
        self.policy_project()
        self.environment["CXXFLAGS"] = "-ffast-math"
        self.assert_refused(self.configure(), "CXXFLAGS is unsupported")
        self.environment.pop("CXXFLAGS")
        self.environment["CPATH"] = str(self.root)
        self.assert_refused(self.configure(), "CPATH is unsupported")
        self.environment.pop("CPATH")
        self.assert_refused(
            self.configure("-DBUILD_TESTING=OFF", build="testing"), "BUILD_TESTING is not"
        )
        self.assert_refused(
            self.configure(
                "-DDE_TOOLCHAIN=pinned",
                build="compiler",
            ),
            "analysis or platform",
        )

    def test_python_validator_check_survives_optimized_execution(self) -> None:
        """The real root rejects the wrong validator before preparing dependency builds."""
        distribution = self.root / "python/jsonschema-0.0.dist-info"
        distribution.mkdir(parents=True)
        (distribution / "METADATA").write_text("Name: jsonschema\nVersion: 0.0\n")
        environment = self.environment.copy()
        environment.update(PYTHONPATH=str(distribution.parent), PYTHONOPTIMIZE="1")
        result = subprocess.run(
            [
                str(self.cmake),
                "-S",
                str(ROOT),
                "-B",
                str(self.root / "root-build"),
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug",
                "-DDE_TOOLCHAIN=platform",
                f"-DPython3_EXECUTABLE={sys.executable}",
            ],
            env=environment,
            capture_output=True,
            text=True,
            check=False,
            timeout=CONFIGURE_TIMEOUT,
        )
        self.assert_refused(result, "selected Python test environment")
        self.assertFalse((self.root / "root-build/deps").exists())

    def test_nondefault_compile_flags_and_quality_disable_are_refused(self) -> None:
        """Defaults are admitted; explicit unsafe compile flags and unverified builds are not."""
        self.policy_project()
        arguments = ("-DCMAKE_BUILD_TYPE=Debug", "-DDE_TOOLCHAIN=platform")
        self.assert_refused(
            self.configure(*arguments, "-DCMAKE_CXX_FLAGS=-ffast-math"), "native defaults"
        )
        self.assert_refused(
            self.configure(*arguments, "-DDE_ENABLE_CLANG_TIDY=OFF", build="tidy"),
            "quality contract",
        )

    def test_foreign_cached_package_is_rejected_before_execution(self) -> None:
        """Reject cached foreign package code before find_package can execute it."""
        prefix = self.root / "prefix"
        owned = prefix / "package"
        foreign = self.root / "foreign"
        marker = self.root / "executed"
        for path in (owned, foreign):
            path.mkdir(parents=True)
            (path / "FixtureConfigVersion.cmake").write_text(
                "set(PACKAGE_VERSION 1.0)\n"
                "set(PACKAGE_VERSION_COMPATIBLE TRUE)\nset(PACKAGE_VERSION_EXACT TRUE)\n"
            )
            (path / "FixtureConfig.cmake").write_text(
                f'file(WRITE "{marker.as_posix()}" "loaded")\n'
            )
        (self.source / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 4.4)
project(Provider NONE)
set(DE_DEPENDENCY_PREFIX "{prefix.as_posix()}")
include("{ROOT.as_posix()}/cmake/DependencyPaths.cmake")
de_validate_package_directory(Fixture)
find_package(Fixture 1.0 EXACT CONFIG REQUIRED PATHS "{owned.as_posix()}" NO_DEFAULT_PATH)
""")
        self.assert_refused(self.configure(f"-DFixture_DIR={foreign}"), "outside the owned")
        self.assertFalse(marker.exists())
        admitted = self.configure(f"-DFixture_DIR={owned}", build="owned")
        self.assertEqual(admitted.returncode, 0, admitted.stdout + admitted.stderr)
        self.assertTrue(marker.is_file())

    def test_probe_selection_is_the_recorded_dependency_plan(self) -> None:
        """Without the probe, compile production codecs but no TIFF/Leptonica."""
        self.policy_project()
        result = self.configure(
            "-DCMAKE_BUILD_TYPE=Release", "-DDE_TOOLCHAIN=platform", "-DDE_BUILD_TOOLS=OFF"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        plan = json.loads((self.root / "build/dependency-plan.json").read_text())
        self.assertNotIn("tiff", plan)
        self.assertNotIn("leptonica", plan)
        self.assertIn("jpeg", plan)
        self.assertIn("opencv", plan)
        self.assertIn("catch2", plan)
