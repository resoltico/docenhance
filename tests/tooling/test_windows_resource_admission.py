# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Producer admission controls use actual header-only distinctions and CL static-CRT semantics."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest.mock import patch

from tools_path import prepare_tool_imports

from test_windows_resource_proof import map_text

import windows_resource_artifacts as artifacts
import windows_resource_execution as execution
import windows_resource_producer as producer
import windows_resource_proof as proof
import windows_resource_records as records

prepare_tool_imports()


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

    def test_debug_junit_exact_two_cases_and_truthful_suite_counts(self) -> None:
        """A third skipped case or adverse suite counter cannot hide behind two healthy rows."""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "ctest.xml"
            cases = "".join(
                '<testcase name="' + name + '" status="run"/>' for name in records.CASES
            )
            good = (
                '<testsuite tests="2" failures="0" errors="0" skipped="0" disabled="0">'
                + cases
                + "</testsuite>"
            )
            path.write_text(good)
            execution.exact_debug_junit(path)
            path.write_text(good.replace(' errors="0"', ""))
            execution.exact_debug_junit(path)
            for bad in (
                good.replace(
                    "</testsuite>",
                    '<testcase name="extra" status="notrun"><skipped/></testcase></testsuite>',
                ),
                good.replace('failures="0"', 'failures="1"'),
                good.replace('errors="0"', 'errors="1"'),
                good.replace('tests="2"', 'tests="3"'),
            ):
                path.write_text(bad)
                with self.subTest(xml=bad), self.assertRaises(ValueError):
                    execution.exact_debug_junit(path)

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
                records.native_records(directory, wrong)
            with self.assertRaises(ValueError):
                proof.retained_owner({"path": str(directory), "identity": wrong})
