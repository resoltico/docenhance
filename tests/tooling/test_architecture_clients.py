# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Final direct client links remain distinct from native transitive usage requirements."""

from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import architecture
import architecture_build


class ClientRegistrationTests(unittest.TestCase):
    """Observe real final CMake properties, including links added after client registration."""

    def test_native_runtime_is_transitive_but_unreviewed_direct_links_are_refused(self) -> None:
        """Libm inherited from a codec is not direct; adding it or an unknown import is direct."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        manifest = architecture.load_manifest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            text = (
                "cmake_minimum_required(VERSION 4.4)\nproject(Probe LANGUAGES NONE)\n"
                f'file(READ "{architecture.MANIFEST.as_posix()}" DE_ARCHITECTURE_JSON)\n'
                f'include("{(ROOT / "cmake/ArchitectureClients.cmake").as_posix()}")\n'
                "add_library(DE::math INTERFACE IMPORTED)\n"
                "add_library(DE::unknown INTERFACE IMPORTED)\n"
                "add_library(DE::png INTERFACE IMPORTED)\n"
                "set_property(TARGET DE::png PROPERTY INTERFACE_LINK_LIBRARIES DE::math)\n"
                "add_library(de_pipeline INTERFACE)\n"
                "target_link_libraries(de_pipeline INTERFACE DE::png)\n"
                "add_library(probe INTERFACE)\n"
                "set_property(TARGET probe PROPERTY LINK_LIBRARIES de_pipeline)\n"
                'de_register_architecture_client(probe "${PROJECT_SOURCE_DIR}/fuzz/render.cpp")\n'
            )
            for extra in ("", "DE::math", "DE::unknown", "de_host"):
                with self.subTest(extra=extra):
                    project = root / "CMakeLists.txt"
                    project.write_text(
                        text
                        + (
                            f"set_property(TARGET probe APPEND PROPERTY LINK_LIBRARIES {extra})\n"
                            if extra
                            else ""
                        ),
                        encoding="utf-8",
                    )
                    build = root / (extra.replace("::", "-") or "healthy")
                    result = subprocess.run(
                        [str(cmake), "-G", "Ninja", "-S", str(root), "-B", str(build)],
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=15,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    record = json.loads((build / "architecture-client-probe.json").read_text())
                    self.assertEqual(
                        record["links"].split(";"), ["de_pipeline", *([extra] if extra else [])]
                    )
                    errors = architecture_build.client_link_errors(manifest, "pipeline", record)
                    self.assertEqual(bool(errors), bool(extra), errors)
                    if extra:
                        self.assertTrue(any(f"cannot link {extra}" in error for error in errors))

    def test_public_record_package_exports_usage_without_exporting_private_packages(self) -> None:
        """Record JSON reaches report consumers; private and unreviewed packages stay closed."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        source = (ROOT / "src/CMakeLists.txt").read_text(encoding="utf-8")
        marker = "function(de_library name)"
        self.assertEqual(source.count(marker), 1)
        body = marker + source.split(marker, 1)[1].split("endfunction()", 1)[0] + "endfunction()\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "spec").mkdir()
            (root / "spec/architecture.json").write_bytes(architecture.MANIFEST.read_bytes())
            for relative in (
                "probe.cpp",
                "include/docenhance/record/probe.hpp",
                "include/docenhance/report/probe.hpp",
                "include/docenhance/codec/probe.hpp",
                "json/nlohmann/json.hpp",
                "png/png.h",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("// CMake usage-requirement fixture\n", encoding="utf-8")
            text = (
                "cmake_minimum_required(VERSION 4.4)\nproject(ExportProbe LANGUAGES CXX)\n"
                f'list(APPEND CMAKE_MODULE_PATH "{(ROOT / "cmake").as_posix()}")\n'
                "include(ArchitectureChecks)\n"
                'set(DE_INCLUDE "${PROJECT_SOURCE_DIR}/include/docenhance")\n'
                "function(de_apply_options target)\nendfunction()\n"
                "add_library(de_core INTERFACE)\nadd_library(de_image INTERFACE)\n"
                "add_library(de_methods INTERFACE)\nadd_library(de_app INTERFACE)\n"
                "add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)\n"
                "set_property(TARGET nlohmann_json::nlohmann_json "
                'PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/json")\n'
                "add_library(DE::png INTERFACE IMPORTED)\n"
                "set_property(TARGET DE::png "
                'PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/png")\n'
                "add_library(DE::unreviewed INTERFACE IMPORTED)\n"
            ) + body
            # Discover the real C++ compiler once; every variant still reruns CMake policy.
            build = root / "usage"
            for public in ("nlohmann_json::nlohmann_json", "DE::unreviewed"):
                with self.subTest(public=public):
                    (root / "CMakeLists.txt").write_text(
                        text + "de_library(de_record LAYER record SOURCES probe.cpp "
                        'HEADERS "${DE_INCLUDE}/record/probe.hpp" '
                        "LINKS de_core de_image de_methods "
                        f"PUBLIC_LINKS {public})\n"
                        "de_library(de_report LAYER report SOURCES probe.cpp "
                        'HEADERS "${DE_INCLUDE}/report/probe.hpp" '
                        "LINKS de_app de_core de_image de_methods PUBLIC_LINKS de_record)\n"
                        "de_library(de_codec LAYER codec SOURCES probe.cpp "
                        'HEADERS "${DE_INCLUDE}/codec/probe.hpp" LINKS DE::png)\n'
                        "add_library(consumer STATIC probe.cpp)\n"
                        "target_link_libraries(consumer PRIVATE de_report)\n"
                        "add_library(private_consumer STATIC probe.cpp)\n"
                        "target_link_libraries(private_consumer PRIVATE de_codec)\n"
                        "get_property(actual TARGET de_record PROPERTY LINK_LIBRARIES)\n"
                        "get_property(declared TARGET de_record PROPERTY DE_ARCHITECTURE_LINKS)\n"
                        "get_property(report_actual TARGET de_report PROPERTY LINK_LIBRARIES)\n"
                        "get_property(report_declared TARGET de_report "
                        "PROPERTY DE_ARCHITECTURE_LINKS)\n"
                        'file(WRITE "${PROJECT_BINARY_DIR}/direct.txt" '
                        '"${actual}\\n${declared}\\n${report_actual}\\n${report_declared}")\n'
                        'file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/usage.txt" CONTENT '
                        '"$<JOIN:$<TARGET_PROPERTY:consumer,INCLUDE_DIRECTORIES>,;>\\n'
                        '$<JOIN:$<TARGET_PROPERTY:private_consumer,INCLUDE_DIRECTORIES>,;>")\n',
                        encoding="utf-8",
                    )
                    result = subprocess.run(
                        [str(cmake), "-G", "Ninja", "-S", str(root), "-B", str(build)],
                        capture_output=True,
                        text=True,
                        check=False,
                        # Cold hosted Windows compiler discovery exceeded 15 seconds.
                        timeout=30 if public == "nlohmann_json::nlohmann_json" else 15,
                    )
                    if public == "DE::unreviewed":
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(
                            "does not declare as a package",
                            " ".join((result.stdout + result.stderr).split()),
                        )
                        continue
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    expected = ["de_core", "de_image", "de_methods", public]
                    report_expected = ["de_app", "de_core", "de_image", "de_methods", "de_record"]
                    self.assertEqual(
                        [
                            line.split(";")
                            for line in (build / "direct.txt").read_text().splitlines()
                        ],
                        [expected, expected, report_expected, report_expected],
                    )
                    record_usage, private_usage = (build / "usage.txt").read_text().splitlines()
                    self.assertIn((root / "json").as_posix(), record_usage.split(";"))
                    self.assertNotIn((root / "png").as_posix(), private_usage.split(";"))
