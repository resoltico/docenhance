# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real CMake/CPack boundaries for owner outputs and exact archive selection."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from typing import override

from tools_path import ROOT

import package_selection

CONFIGURE_TIMEOUT = 90


class BuildLayoutTests(unittest.TestCase):
    """Run production ownership/package modules against a native fixture without downloads."""

    @override
    def setUp(self) -> None:
        """Own private prefixes and a native program; preserve repository outputs."""
        temporary = tempfile.TemporaryDirectory(prefix="docenhance-layout-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / "source"
        self.source.mkdir()
        self.environment = os.environ.copy()
        for name in ("CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS"):
            self.environment.pop(name, None)
        self.environment["VIRTUAL_ENV"] = str(Path(sys.prefix))
        self.cmake = shutil.which("cmake")
        self.assertIsNotNone(self.cmake, "Layout verification requires real CMake")
        (self.source / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (self.source / "CMakeLists.txt").write_text(
            f"""cmake_minimum_required(VERSION 4.4)
include("{ROOT.as_posix()}/cmake/BuildPolicy.cmake")
project(Layout VERSION 1.2.3 LANGUAGES C CXX)
set(PROJECT_SOURCE_DIR "{ROOT.as_posix()}")
list(APPEND CMAKE_MODULE_PATH "{ROOT.as_posix()}/cmake")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(Options)
include(CompilerPolicy)
de_validate_native_configuration()
include(DependencyPlan)
include(BuildIdentity)
add_executable(docenhance "{self.source.as_posix()}/main.cpp")
set_target_properties(docenhance PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${{DE_BUILD_OWNER}}/bin")
file(WRITE "${{PROJECT_BINARY_DIR}}/payload.txt" "${{PAYLOAD}}")
install(FILES "${{PROJECT_BINARY_DIR}}/payload.txt" DESTINATION .)
include(PackageMetadata)
include(CPack)
de_commit_build_identity()
""",
            encoding="utf-8",
        )

    def run_command(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        """Keep diagnostics and actual subprocess failure visible."""
        return subprocess.run(
            list(arguments),
            env=self.environment,
            capture_output=True,
            text=True,
            check=False,
            timeout=CONFIGURE_TIMEOUT,
        )

    def require_success(self, result: subprocess.CompletedProcess[str]) -> None:
        """A zero exit is required, including native compiler and CPack execution."""
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def configure(self, build: Path, payload: str, *, owner: Path | None = None) -> None:
        """Configure the same real ownership boundary on both sides of the app child."""
        arguments = [
            str(self.cmake),
            "-S",
            str(self.source),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DDE_TOOLCHAIN=platform",
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DPAYLOAD={payload}",
            "-DDE_SUPERBUILD=TRUE",
        ]
        if owner is not None:
            arguments += [
                "-DDE_SUPERBUILD=OFF",
                f"-DDE_SUPERBUILD_BINARY={owner}",
                f"-DDE_DEPENDENCY_PREFIX={owner / 'prefix'}",
            ]
        self.require_success(self.run_command(*arguments))

    def package(self, owner: Path, payload: str) -> Path:
        """Generate identical outer/inner CPack contracts and run the real external packager."""
        self.configure(owner, payload)
        self.configure(owner / "app", payload, owner=owner)
        self.require_success(self.run_command(str(self.cmake), "--build", str(owner)))
        cpack = shutil.which("cpack")
        self.assertIsNotNone(cpack)
        self.require_success(
            self.run_command(str(cpack), "--config", str(owner / "CPackConfig.cmake"))
        )
        return package_selection.current_package(owner)

    def test_private_packages_preserve_same_name_payloads_and_exact_selection(self) -> None:
        """Retained source/old-version archives cannot redirect current package selection."""
        first = self.root / "configuration-one"
        second = self.root / "configuration with spaces"
        archive_one = self.package(first, "first")
        archive_two = self.package(second, "second")
        self.assertEqual(archive_one.name, archive_two.name)
        self.assertNotEqual(archive_one.read_bytes(), archive_two.read_bytes())
        self.assertEqual(archive_one.parent, first / "packages")
        self.assertEqual(archive_two.parent, second / "packages")
        self.assertTrue(archive_one.with_suffix(".gz.sha256").is_file())
        executable = "docenhance.exe" if os.name == "nt" else "docenhance"
        self.assertTrue((first / "bin" / executable).is_file())
        self.assertFalse((first / "app/bin").exists())
        self.assertEqual(
            package_selection.cpack_values(first / "CPackConfig.cmake"),
            package_selection.cpack_values(first / "app/CPackConfig.cmake"),
        )
        for name in ("docenhance-0.1.0-native.tar.gz", "docenhance-source.tar.gz"):
            (archive_one.parent / name).write_bytes(b"retained artifact")
        self.assertEqual(package_selection.current_package(first), archive_one)
        with tarfile.open(archive_one) as archive:
            member = archive.extractfile(archive_one.name.removesuffix(".tar.gz") + "/payload.txt")
            self.assertIsNotNone(member)
            if member is not None:
                self.assertEqual(member.read(), b"first")
        self.configure(first, "first")
        self.assertEqual(package_selection.current_package(first), archive_one)
        archive_one.unlink()
        with self.assertRaisesRegex(ValueError, "Missing current native archive"):
            package_selection.current_package(first)
        self.assertEqual(
            (archive_one.parent / "docenhance-source.tar.gz").read_bytes(), b"retained artifact"
        )

    def test_current_archive_still_requires_actual_package_inspection(self) -> None:
        """Correct archive selection still requires the actual runtime binary."""
        owner = self.root / "owner"
        self.package(owner, "deliberately incomplete package")
        result = self.run_command(
            sys.executable, str(ROOT / "tools/package_smoke.py"), "--build", str(owner)
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exactly one runtime executable", result.stdout + result.stderr)

    def test_metadata_and_retired_owner_admission_fail_without_fallback(self) -> None:
        """Missing or contradictory metadata cannot select another directory or archive."""
        owner = self.root / "owner"
        self.package(owner, "bytes")
        with self.assertRaisesRegex(ValueError, "not its app child"):
            package_selection.current_package(owner / "app")
        config = owner / "CPackConfig.cmake"
        config.unlink()
        with self.assertRaisesRegex(ValueError, "Missing current native package metadata"):
            package_selection.current_package(owner)
        for arguments in (
            ["retained.tar.gz", "--build", str(owner)],
            ["--build", str(owner / "app")],
        ):
            result = self.run_command(
                sys.executable, str(ROOT / "tools/package_smoke.py"), *arguments
            )
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.configure(owner, "bytes")
        inner = owner / "app/CPackConfig.cmake"
        inner.write_text(inner.read_text() + '\nset(CPACK_PACKAGE_FILE_NAME "wrong-current")\n')
        with self.assertRaisesRegex(ValueError, "must agree"):
            package_selection.current_package(owner)
