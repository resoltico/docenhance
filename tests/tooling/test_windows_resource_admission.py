# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Producer admission controls use actual header-only distinctions and CL static-CRT semantics."""

from __future__ import annotations

import gzip
import hashlib
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import Any, cast
from unittest.mock import patch

from tools_path import prepare_tool_imports

from restoration_resource_fixture import RESOURCE_EXECUTABLES, resource_fixture
from test_windows_resource_proof import map_text

import native_journal as journal
import restoration_resource_records as resource
import windows_resource_artifacts as artifacts
import windows_resource_execution as execution
import windows_resource_producer as producer
import windows_resource_proof as proof
import windows_resource_records as records

prepare_tool_imports()


def windows_stat(info: os.stat_result, **changes: int) -> os.stat_result:
    """Model CPython Windows path/fd epochs while real tiny files supply the bytes."""
    values = {
        name: getattr(info, name)
        for name in ("st_mode", "st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
    }
    values["st_birthtime_ns"] = 17
    values.update(changes)
    return cast("os.stat_result", SimpleNamespace(**values))


class WindowsResourceAdmissionTests(unittest.TestCase):
    """Synthetic admission evidence does not qualify an actual Windows compiler or runtime."""

    def test_intrinsic_debug_and_contradictory_defines_undefines(self) -> None:
        """MTd without explicit D_DEBUG is normal; contradictory CRT options refuse."""
        producer.static_crt_options(["cl.exe", "/MTd"], debug=True)
        producer.static_crt_options(["cl.exe", "-MT"], debug=False)
        for args, debug in (
            (["/MD"], False),
            (["/MDd"], True),
            (["/MTd", "/U_DEBUG"], True),
            (["/MTd", "/D_DLL=0"], True),
            (["/MT", "/D", "_DEBUG"], False),
            (["/MT", "/U_MT"], False),
        ):
            with self.subTest(args=args), self.assertRaises(ValueError):
                producer.static_crt_options(args, debug=debug)

    def test_only_six_compiled_dependency_caches_require_crt(self) -> None:
        """Header-only json/picosha2/pocketfft have no runtime cache key."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in (
                "zlib",
                "jpeg",
                "png",
                "tiff",
                "lcms",
                "catch2",
                "json",
                "picosha2",
                "pocketfft",
            ):
                path = root / "deps" / name / "CMakeCache.txt"
                path.parent.mkdir(parents=True)
                path.write_text(
                    ""
                    if name in {"json", "picosha2", "pocketfft"}
                    else "CMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreadedDebug\n"
                )
            result: dict[str, Any] = {}
            producer.dependency_crt(root, "MultiThreadedDebug", result)
            self.assertEqual(set(result), {"zlib", "jpeg", "png", "tiff", "lcms", "catch2"})
            (root / "deps/catch2/CMakeCache.txt").write_text(
                "CMAKE_MSVC_RUNTIME_LIBRARY:STRING=MultiThreadedDLL\n"
            )
            partial: dict[str, Any] = {}
            with self.assertRaises(ValueError):
                producer.dependency_crt(root, "MultiThreadedDebug", partial)
            self.assertEqual(set(partial), {"zlib", "jpeg", "png", "tiff", "lcms"})

    def test_debug_junit_exact_discovered_cases_and_truthful_suite_counts(self) -> None:
        """An extra skipped case or adverse suite count cannot hide behind healthy resources."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ctest.xml"
            names = resource.resource_cases(resource_fixture()[0], RESOURCE_EXECUTABLES)
            cases = "".join(
                '<testcase name="' + name + '" status="run"/>' for name in sorted(names)
            )
            good = (
                f'<testsuite tests="{len(names)}" failures="0" errors="0" skipped="0" disabled="0">'
                + cases
                + "</testsuite>"
            )
            path.write_text(good)
            execution.exact_debug_junit(path, names)
            path.write_text(good.replace(' errors="0"', ""))
            execution.exact_debug_junit(path, names)
            for bad in (
                good.replace(
                    "</testsuite>",
                    '<testcase name="extra" status="notrun"><skipped/></testcase></testsuite>',
                ),
                good.replace('failures="0"', 'failures="1"'),
                good.replace('errors="0"', 'errors="1"'),
                good.replace(f'tests="{len(names)}"', f'tests="{len(names) + 1}"'),
            ):
                path.write_text(bad)
                with self.subTest(xml=bad), self.assertRaises(ValueError):
                    execution.exact_debug_junit(path, names)

    def test_map_failure_preserves_first_acquired_record_and_second_archive(self) -> None:
        """Later route failure keeps verified preceding facts and newly retained bytes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "app/tests"
            directory.mkdir(parents=True)
            for index, target in enumerate(artifacts.TARGETS):
                (directory / (target + ".map")).write_text(map_text() if index == 0 else "bad map")
            acquired: list[dict[str, Any]] = []
            with self.assertRaises(ValueError):
                records.capture_maps(root, root, acquired, debug=False)
            self.assertEqual(len(acquired), 2)
            self.assertEqual(len(acquired[0]["routes"]), 4)
            self.assertNotIn("routes", acquired[1])
            self.assertTrue(Path(acquired[1]["gzip"]["path"]).is_file())

    def test_later_executable_failure_retains_first_actual_producer_identities(self) -> None:
        """Partial producer facts are stored before a later acquisition can fail."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            app.mkdir()
            prefix = str((root / "prefix").resolve())
            config = {
                "CMAKE_SYSTEM_NAME": "Windows",
                "CMAKE_CXX_COMPILER_ID": "MSVC",
                "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDebug",
                "CMAKE_BUILD_TYPE": "Debug",
                prefix: prefix,
            }
            for path in (root / "build-identity.json", app / "build-identity.json"):
                path.write_text(json.dumps(config))
            entries = []
            for index, target in enumerate(artifacts.TARGETS):
                path = (
                    app
                    / "tests/CMakeFiles"
                    / (target + ".dir")
                    / "support/allocation_interpose.cpp.obj"
                )
                path.parent.mkdir(parents=True)
                path.write_bytes(b"synthetic object identity only")
                if index == 0:
                    (app / "tests" / (target + ".exe")).write_bytes(b"synthetic exe identity only")
                entries.append(
                    {
                        "file": "allocation_interpose.cpp",
                        "command": "cl.exe /MTd /Fo" + target + ".dir/observer.obj",
                    }
                )
            (app / "compile_commands.json").write_text(json.dumps(entries))
            acquired: dict[str, Any] = {}
            with self.assertRaises(ValueError):
                producer.compilation(root, acquired, debug=True)
            self.assertEqual(len(acquired["targets"]), 2)
            self.assertIn("object", acquired["targets"][0])
            self.assertIn("executable", acquired["targets"][0])
            self.assertIn("object", acquired["targets"][1])
            self.assertNotIn("executable", acquired["targets"][1])

    def test_map_oversize_refuses_before_identity_hash(self) -> None:
        """The regular-file size admission precedes expensive content acquisition."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "large.map"
            path.write_bytes(b"xx")
            with (
                patch.object(artifacts, "MAP_BYTES", 1),
                patch.object(artifacts, "identity") as identity,
                self.assertRaises(ValueError),
            ):
                artifacts.archive_map(path, path.parent)
            identity.assert_not_called()

    def test_selected_directory_identity_is_required_during_read_and_completion(self) -> None:
        """Another directory at the same path cannot become the previously selected owner."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            info = directory.lstat()
            wrong = [info.st_dev, info.st_ino + 1]
            with self.assertRaises(ValueError):
                records.native_records(
                    directory, wrong, resource_fixture()[0], RESOURCE_EXECUTABLES
                )
            with self.assertRaises(ValueError):
                proof.retained_owner({"path": str(directory), "identity": wrong})

    def test_windows_path_birth_time_and_fd_change_time_are_comparable(self) -> None:
        """A stable real file accepts unequal API ctimes without dropping fd stability."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "source.map"
            path.write_bytes(b"fixed native proof")
            actual_fstat = os.fstat

            def path_epoch(name: Path) -> journal.Journal:
                return journal.journal_identity(windows_stat(name.lstat(), st_ctime_ns=17))

            def fd_epoch(descriptor: int) -> os.stat_result:
                return windows_stat(actual_fstat(descriptor), st_ctime_ns=29)

            with (
                patch.object(sys, "platform", "win32"),
                patch.object(artifacts, "identify_journal", side_effect=path_epoch),
                patch.object(os, "fstat", side_effect=fd_epoch),
            ):
                observed = artifacts.identity(path)
                raw, archived = artifacts.archive_map(path, path.parent)
            self.assertEqual(observed["bytes"], len(b"fixed native proof"))
            self.assertEqual(observed["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(raw, b"fixed native proof")
            self.assertEqual(gzip.decompress(Path(archived["gzip"]["path"]).read_bytes()), raw)

    def test_windows_identity_rejects_inode_size_and_missing_birth_time(self) -> None:
        """The cross-API repair keeps file ownership, byte count and required creation time."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "source.map"
            path.write_bytes(b"fixed")
            native = path.stat()
            with patch.object(sys, "platform", "win32"):
                epoch = journal.journal_identity(windows_stat(native))
                for field in ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_birthtime_ns"):
                    original = getattr(windows_stat(native), field)
                    changed = windows_stat(native, **{field: original + 1})
                    with (
                        self.subTest(field=field),
                        patch.object(artifacts, "identify_journal", return_value=epoch),
                        patch.object(os, "fstat", return_value=changed),
                        self.assertRaisesRegex(ValueError, "changed while opening"),
                    ):
                        artifacts.identity(path)
                missing = windows_stat(native)
                delattr(missing, "st_birthtime_ns")
                with self.assertRaisesRegex(journal.OutputError, "birth time"):
                    journal.journal_identity(missing)
                with self.assertRaisesRegex(journal.OutputError, "regular file"):
                    journal.journal_identity(windows_stat(native, st_mode=stat.S_IFLNK))

    def test_windows_descriptor_change_time_is_retained_for_hash_and_map(self) -> None:
        """Changing only native fd ChangeTime refuses both owners despite stable birth time."""
        read_identity = artifacts.identity
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "source.map"
            path.write_bytes(b"fixed")
            native = path.stat()
            source = {"path": str(path), "bytes": 5, "sha256": hashlib.sha256(b"fixed").hexdigest()}
            with patch.object(sys, "platform", "win32"):
                epoch = journal.journal_identity(windows_stat(native))
                for operation in ("identity", "map"):
                    with (
                        self.subTest(operation=operation),
                        patch.object(artifacts, "identity", return_value=source),
                        patch.object(artifacts, "identify_journal", return_value=epoch),
                        patch.object(
                            os,
                            "fstat",
                            side_effect=[
                                windows_stat(native, st_ctime_ns=29),
                                windows_stat(native, st_ctime_ns=30),
                            ],
                        ),
                        self.assertRaisesRegex(ValueError, "changed"),
                    ):
                        if operation == "identity":
                            read_identity(path)
                        else:
                            artifacts.archive_map(path, root)
                    self.assertFalse((root / "source.map.gz").exists())
