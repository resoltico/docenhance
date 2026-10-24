# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Immutable archive evidence, independent Git configuration and exclusive cache writers."""

from __future__ import annotations

import hashlib
import io
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT, prepare_tool_imports

import audit_build
import cache_lock
import dep_acquire
import dep_verify
import install_llvm
import llvm_cache

prepare_tool_imports()


class DependencyIntegrityTests(unittest.TestCase):
    """Challenge the authoritative artifact rather than comparing two mutable local copies."""

    def test_windows_zlib_names_resolve_only_configuration_private_libraries(self) -> None:
        """Real CMake imports Debug zsd and Release zs without adopting a foreign archive."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        source = (ROOT / "cmake/Dependencies.cmake").read_text(encoding="utf-8")
        marker = "function(de_import_static target)"
        self.assertEqual(source.count(marker), 1)
        helper = (
            marker + source.split(marker, 1)[1].split("endfunction()", 1)[0] + "endfunction()\n"
        )
        self.assertEqual(source.count("\nif(WIN32)\n"), 1)
        names = "if(WIN32)\n" + source.split("\nif(WIN32)\n", 1)[1].split("\nendif()", 1)[0]
        names += "\nendif()\n"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            foreign = root / "foreign"
            (foreign / "lib").mkdir(parents=True)
            (foreign / "lib/zsd.lib").write_bytes(b"foreign archive fixture")
            for name, libraries, selected, override in (
                ("debug", ("zsd.lib",), "zsd.lib", False),
                ("release", ("zs.lib",), "zs.lib", False),
                ("debug-first", ("zs.lib", "zsd.lib"), "zsd.lib", False),
                ("ambient-only", (), None, False),
                ("foreign-override", (), None, True),
            ):
                with self.subTest(name=name):
                    project = root / name
                    prefix = project / "prefix"
                    (prefix / "lib").mkdir(parents=True)
                    (prefix / "include").mkdir()
                    (prefix / "include/zlib.h").write_text("// owned header\n")
                    for library in libraries:
                        (prefix / "lib" / library).write_bytes(b"owned archive fixture")
                    (project / "CMakeLists.txt").write_text(
                        "cmake_minimum_required(VERSION 4.4)\nproject(Probe LANGUAGES NONE)\n"
                        'set(WIN32 TRUE)\nset(CMAKE_FIND_LIBRARY_PREFIXES "")\n'
                        'set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")\n'
                        f'set(DE_DEPENDENCY_PREFIX "{prefix.as_posix()}")\n'
                        f'include("{(ROOT / "cmake/DependencyPaths.cmake").as_posix()}")\n'
                        + helper
                        + names
                        + "de_import_static(DE::zlib NAMES ${de_zlib_names} HEADER zlib.h)\n"
                        "get_target_property(actual DE::zlib IMPORTED_LOCATION)\n"
                        'file(WRITE "${CMAKE_BINARY_DIR}/selected.txt" "${actual}")\n',
                        encoding="utf-8",
                    )
                    build = project / "build"
                    command = [str(cmake), "-G", "Ninja", "-S", str(project), "-B", str(build)]
                    command.append(f"-DCMAKE_PREFIX_PATH={foreign.as_posix()}")
                    build_type = "Release" if name == "release" else "Debug"
                    command.append(f"-DCMAKE_BUILD_TYPE={build_type}")
                    if override:
                        command.append(f"-Dde_library={foreign.as_posix()}/lib/zsd.lib")
                    result = subprocess.run(
                        command, capture_output=True, text=True, check=False, timeout=15
                    )
                    if selected is None:
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertFalse((build / "selected.txt").exists())
                    else:
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertEqual(
                            (build / "selected.txt").read_text(),
                            (prefix / "lib" / selected).as_posix(),
                        )

    def archive(self, cache: Path) -> dep_verify.Dependency:
        """One real source archive, extracted and inventoried through production code."""
        path = cache / "archives/fixture-1.tar.gz"
        path.parent.mkdir()
        with tarfile.open(path, "w:gz") as stream:
            member = tarfile.TarInfo("fixture/LICENSE")
            data = b"reviewed bytes\n"
            member.size = len(data)
            stream.addfile(member, io.BytesIO(data))
        source = cache / "sources/fixture"
        source.mkdir(parents=True)
        dep: dep_verify.Dependency = {
            "name": "fixture",
            "version": "1",
            "transport": "archive",
            "digest_algorithm": "sha256",
            "digest": hashlib.sha256(path.read_bytes()).hexdigest(),
            "license_files": ["LICENSE"],
        }
        dep_acquire.unpack_archive(path, source)
        (cache / "receipts").mkdir()
        (cache / "receipts/fixture.json").write_text(
            json.dumps(dep_verify.receipt_for(dep, source))
        )
        return dep

    def test_source_and_receipt_cannot_jointly_override_locked_archive(self) -> None:
        """The old receipt comparison admitted this mutation; archive comparison must refuse it."""
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            dep = self.archive(cache)
            dep_verify.verify(dep, cache)
            source = cache / "sources/fixture"
            (source / "LICENSE").write_text("changed bytes\n")
            (cache / "receipts/fixture.json").write_text(
                json.dumps(dep_verify.receipt_for(dep, source))
            )
            with self.assertRaisesRegex(dep_verify.DependencyError, "locked archive"):
                dep_verify.verify(dep, cache)

    def test_missing_or_changed_archive_is_not_a_receipt_only_success(self) -> None:
        """A retained source tree cannot replace the mandatory original acquisition evidence."""
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            dep = self.archive(cache)
            path = cache / "archives/fixture-1.tar.gz"
            path.write_bytes(b"different")
            with self.assertRaisesRegex(dep_verify.DependencyError, "locked archive"):
                dep_verify.verify(dep, cache)
            path.unlink()
            with self.assertRaisesRegex(dep_verify.DependencyError, "locked archive"):
                dep_verify.verify(dep, cache)

    def test_duplicate_archive_members_are_refused(self) -> None:
        """Extraction cannot silently choose which same-name source member wins."""
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "duplicate.tar"
            with tarfile.open(archive, "w") as stream:
                for data in (b"one", b"two"):
                    member = tarfile.TarInfo("source/file")
                    member.size = len(data)
                    stream.addfile(member, io.BytesIO(data))
            with self.assertRaises(tarfile.FilterError):
                dep_verify.safe_extract(archive, Path(temporary) / "output")

    def test_nested_git_named_source_files_are_inventoried(self) -> None:
        """Only the actual top-level Git metadata is excluded, not arbitrary nested directories."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nested = root / "code/.git/input"
            nested.parent.mkdir(parents=True)
            nested.write_bytes(b"source")
            self.assertIn("code/.git/input", dep_verify.inventory(root))

    def test_git_environment_and_global_aliases_do_not_redirect_commands(self) -> None:
        """Real Git ignores inherited work-tree and config injection."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repository"
            dep_verify.run("git", "init", str(repository))
            global_config = root / "global"
            global_config.write_text("[alias]\n poisoned = !echo unreviewed-command\n")
            with patch.dict(
                os.environ,
                {
                    "GIT_DIR": str(root / "missing"),
                    "GIT_CONFIG_GLOBAL": str(global_config),
                    "GIT_CONFIG_COUNT": "1",
                    "GIT_CONFIG_KEY_0": "core.bare",
                    "GIT_CONFIG_VALUE_0": "true",
                },
            ):
                self.assertEqual(
                    dep_verify.run("git", "rev-parse", "--is-bare-repository", cwd=repository),
                    "false",
                )
                with self.assertRaises(dep_verify.CommandError):
                    dep_verify.run("git", "poisoned", cwd=repository)

    def test_cache_writer_cannot_adopt_or_delete_another_claim(self) -> None:
        """Exclusive claims survive another writer's failure and refund the owner on exceptions."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "active"
            with cache_lock.exclusive_cache(path):
                with self.assertRaises(RuntimeError), cache_lock.exclusive_cache(path):
                    self.fail("Second writer was admitted")
                self.assertTrue(path.exists())
            self.assertFalse(path.exists())
            with self.assertRaises(ValueError), cache_lock.exclusive_cache(path):
                message = "controlled failure"
                raise ValueError(message)
            self.assertFalse(path.exists())

    def test_llvm_cache_requires_both_tools_and_matching_receipt(self) -> None:
        """One executable or a stale/tampered query cannot manufacture cache readiness."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "bin"
            tools.mkdir(parents=True)
            headers = root / "lib/clang/23/include"
            headers.mkdir(parents=True)
            (headers / "stddef.h").write_bytes(b"builtin")
            identity = {
                "version": "23.1.3",
                "source_sha256": "reviewed",
                "host_os_version": "15.7",
                "deployment_target": "15.0",
            }
            (tools / "clang-tidy").write_bytes(b"tidy")
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity)
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity | {"host_os_version": "26.0"})
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity | {"deployment_target": "14.0"})
            (tools / "clang-query").write_bytes(b"query")
            (root / "receipt.json").write_text(
                json.dumps(llvm_cache.source_tool_receipt(root, identity))
            )
            llvm_cache.validate_source_tools(root, identity)
            (tools / "clang-query").write_bytes(b"changed")
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity)

    def test_installed_major_is_preferred_over_runner_default(self) -> None:
        """The CI host can retain generic LLVM 18 alongside the newly installed LLVM 23."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ("clang-tidy", "clang-query"):
                (directory / name).write_bytes(b"runner default")
                desired = directory / (name + "-23")
                desired.write_bytes(b"reviewed major")
                self.assertEqual(install_llvm.resolve_analysis_tool(directory, name, "23"), desired)
                desired.unlink()
                self.assertEqual(
                    install_llvm.resolve_analysis_tool(directory, name, "23"),
                    directory / name,
                )
                (directory / name).unlink()
                with self.assertRaises(llvm_cache.InstallError):
                    install_llvm.resolve_analysis_tool(directory, name, "23")

    def test_binary_path_feature_is_checked_and_missing_plan_cache_fails(self) -> None:
        """Expanded path settings and optional-looking missing caches are mandatory evidence."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            settings: dict[str, bool | str] = {"FIXTURE_PATH": "<BINARY>/downloads"}
            binary = root / "deps/fixture"
            bad = audit_build.feature_failures(
                "fixture",
                settings,
                {"BUILD_SHARED_LIBS": "OFF", "FIXTURE_PATH": str(root / "outside")},
                binary,
            )
            self.assertTrue(bad)
            good = audit_build.feature_failures(
                "fixture",
                settings,
                {
                    "BUILD_SHARED_LIBS": "OFF",
                    "FIXTURE_PATH": binary.as_posix() + "/downloads",
                },
                binary,
            )
            self.assertFalse(good)
            (root / "dependency-plan.json").write_text('["catch2"]')
            (root / "build-identity.json").write_text('{"CMAKE_SYSTEM_NAME":"Linux"}')
            self.assertTrue(audit_build.audit(root))

    def test_catch_language_cache_matches_bound_consumer_standard(self) -> None:
        """A real dependency cache must retain the bound standard without fallback/extensions."""
        standard = str(
            json.loads((audit_build.ROOT / "deps/tools.json").read_text())["cxx"]["standard"]
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "deps/catch2"
            binary.mkdir(parents=True)
            (root / "dependency-plan.json").write_text('["catch2"]')
            (root / "build-identity.json").write_text(
                json.dumps({"CMAKE_SYSTEM_NAME": "Linux", "DE_CXX_STANDARD": standard})
            )
            healthy = {
                "BUILD_SHARED_LIBS": "OFF",
                "CATCH_BUILD_TESTING": "OFF",
                "CATCH_INSTALL_DOCS": "OFF",
                "CATCH_INSTALL_EXTRAS": "ON",
                "CMAKE_CXX_STANDARD": standard,
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_CXX_EXTENSIONS": "OFF",
            }
            # This fixture isolates language/cache admission; native hardening has its own controls.
            with patch("audit_build.hardening_errors", return_value=[]):
                (binary / "CMakeCache.txt").write_text(
                    "".join(f"{key}:STRING={value}\n" for key, value in healthy.items())
                )
                self.assertEqual(audit_build.audit(root), [])
                for key, wrong in (
                    ("CMAKE_CXX_STANDARD", "14"),
                    ("CMAKE_CXX_STANDARD_REQUIRED", "OFF"),
                    ("CMAKE_CXX_EXTENSIONS", "ON"),
                ):
                    for missing in (False, True):
                        with self.subTest(key=key, missing=missing):
                            values = dict(healthy)
                            if missing:
                                values.pop(key)
                            else:
                                values[key] = wrong
                            (binary / "CMakeCache.txt").write_text(
                                "".join(
                                    f"{name}:STRING={value}\n" for name, value in values.items()
                                )
                            )
                            errors = audit_build.audit(root)
                            diagnostic = (
                                f"catch2: missing declared feature {key}"
                                if missing
                                else f"catch2: unexpected {key}={wrong}"
                            )
                            self.assertTrue(
                                any(error.startswith(diagnostic) for error in errors),
                                errors,
                            )

    def test_installed_json_header_must_match_private_build_input(self) -> None:
        """A feature cache flag cannot substitute for the actual installed header bytes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "deps/json"
            cache = {"BUILD_SHARED_LIBS": "OFF"}
            self.assertTrue(audit_build.feature_failures("json", {}, cache, binary))
            owned = binary / "owned-source/single_include/nlohmann/json.hpp"
            installed = root / "prefix/include/nlohmann/json.hpp"
            owned.parent.mkdir(parents=True)
            installed.parent.mkdir(parents=True)
            owned.write_bytes(b"reviewed private header")
            installed.write_bytes(b"unmodified upstream header")
            self.assertTrue(audit_build.feature_failures("json", {}, cache, binary))
            installed.write_bytes(owned.read_bytes())
            # Two mutable copies agreeing is not independent identity evidence.
            self.assertTrue(audit_build.feature_failures("json", {}, cache, binary))
            expected = hashlib.sha256(owned.read_bytes()).hexdigest()
            with patch("audit_build.JSON_BOUNDED_HEADER_SHA256", expected):
                self.assertFalse(audit_build.feature_failures("json", {}, cache, binary))
                owned.write_bytes(b"both copies altered")
                installed.write_bytes(owned.read_bytes())
                self.assertTrue(audit_build.feature_failures("json", {}, cache, binary))

    def test_installed_pocketfft_header_has_independent_identity(self) -> None:
        """Two agreeing mutable copies cannot replace the reviewed provider header."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "deps/pocketfft"
            binary.mkdir(parents=True)
            source = root / "source"
            source.mkdir()
            (binary / "CMakeCache.txt").write_text(f"DE_UPSTREAM_SOURCE:PATH={source}\n")
            installed = root / "prefix/include/pocketfft/pocketfft_hdronly.h"
            self.assertTrue(audit_build.pocketfft_header_failures(binary))
            installed.parent.mkdir(parents=True)
            header = source / "pocketfft_hdronly.h"
            header.write_bytes(b"reviewed provider fixture")
            installed.write_bytes(b"different")
            self.assertTrue(audit_build.pocketfft_header_failures(binary))
            installed.write_bytes(header.read_bytes())
            self.assertTrue(audit_build.pocketfft_header_failures(binary))
            expected = hashlib.sha256(header.read_bytes()).hexdigest()
            with patch("audit_build.POCKETFFT_HEADER_SHA256", expected):
                self.assertFalse(audit_build.pocketfft_header_failures(binary))
                header.write_bytes(b"both copies changed")
                installed.write_bytes(header.read_bytes())
                self.assertTrue(audit_build.pocketfft_header_failures(binary))


if __name__ == "__main__":
    unittest.main()
