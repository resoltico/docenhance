# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Execute the actual provider import on selected-file substitution counterexamples."""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import audit_build


class PocketfftBindingTests(unittest.TestCase):
    """A correct canonical installed copy cannot excuse a different selected header."""

    def test_selected_owned_header_bytes_and_file_symlink_are_admitted_truthfully(self) -> None:
        """Run the production import owner, without unrelated packages or numeric facades."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        with tempfile.TemporaryDirectory(prefix="pocketfft-binding-") as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "deps").mkdir(parents=True)
            prefix = root / "prefix"
            canonical = prefix / "include/pocketfft"
            canonical.mkdir(parents=True)
            header = canonical / "pocketfft_hdronly.h"
            header.write_bytes(b"// Reviewed admission fixture, never a numerical FFT.\n")
            digest = hashlib.sha256(header.read_bytes()).hexdigest()
            (source / "deps/features.json").write_text(
                json.dumps(
                    {
                        "dependencies": {
                            "pocketfft": {
                                "POCKETFFT_NO_MULTITHREADING": True,
                                "POCKETFFT_CACHE_SIZE": "0",
                                "DOCENHANCE_POCKETFFT_FALLIBLE_VECTORS": True,
                                "DOCENHANCE_POCKETFFT_PATCHED_HEADER_SHA256": digest,
                            }
                        },
                    }
                )
            )
            (source / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 4.4)
project(Provider NONE)
set(DE_DEPENDENCY_PREFIX "{prefix.as_posix()}")
list(APPEND CMAKE_MODULE_PATH "{ROOT.as_posix()}/cmake")
include(PocketFFT)
get_target_property(selected DE::pocketfft INTERFACE_INCLUDE_DIRECTORIES)
file(WRITE "${{PROJECT_BINARY_DIR}}/selected.txt" "${{selected}}")
""")
            result = self.configure(str(cmake), source, root / "canonical")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((root / "canonical/selected.txt").read_text(), canonical.as_posix())
            alternate = prefix / "alternate"
            alternate.mkdir()
            (alternate / header.name).write_bytes(b"// Unreviewed selected header.\n")
            result = self.configure(str(cmake), source, root / "wrong", alternate)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("selected header digest differs", result.stdout + result.stderr)
            self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), digest)
            foreign = root / "foreign.h"
            foreign.write_bytes(header.read_bytes())
            escaped = prefix / "escaped"
            escaped.mkdir()
            (escaped / header.name).symlink_to(foreign)
            result = self.configure(str(cmake), source, root / "symlink", escaped)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("outside the owned dependency prefix", result.stdout + result.stderr)

    def test_private_header_patch_refuses_changed_constructor_inventory(self) -> None:
        """Real adapter installation preserves upstream and binds every reviewed replacement."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake)
        constructors = (
            (
                "POCKETFFT_NOINLINE explicit cfftp(size_t length_)\n      : length(length_)",
                (
                    "POCKETFFT_NOINLINE explicit cfftp(size_t length_)\n"
                    "      : length(length_), fact(0)"
                ),
            ),
            (
                "POCKETFFT_NOINLINE explicit rfftp(size_t length_)\n      : length(length_)",
                (
                    "POCKETFFT_NOINLINE explicit rfftp(size_t length_)\n"
                    "      : length(length_), fact(0)"
                ),
            ),
            (
                (
                    "arr_info(shape_t shape_, stride_t stride_)\n"
                    "      : shp(std::move(shape_)), str(std::move(stride_)) {}"
                ),
                (
                    "arr_info(const shape_t &shape_, const stride_t &stride_)\n"
                    "      : shp(shape_), str(stride_) {}"
                ),
            ),
        )
        original = "\n".join(before for before, _ in constructors) + "\n"
        expected = "\n".join(after for _, after in constructors) + "\n"
        digest = hashlib.sha256(expected.encode()).hexdigest()
        cases: list[tuple[str, str, str | None, str, str, str]] = [
            ("healthy", original, None, digest, "ON", "")
        ]
        cases.extend(
            (f"missing-{index}", original.replace(before, "absent"), None, digest, "ON", "absent")
            for index, (before, _) in enumerate(constructors)
        )
        cases.extend(
            (f"repeated-{index}", original + before, None, digest, "ON", "repeated")
            for index, (before, _) in enumerate(constructors)
        )
        cases.extend(
            [
                ("upstream", original, "0" * 64, digest, "ON", "reviewed resource"),
                ("derived", original, None, "0" * 64, "ON", "reviewed output"),
                ("disabled", original, None, digest, "OFF", "reviewed pocketfft policy"),
            ]
        )
        with tempfile.TemporaryDirectory(prefix="pocketfft-private-") as temporary:
            root = Path(temporary)
            for name, content, upstream_hash, derived, enabled, refusal in cases:
                with self.subTest(name=name):
                    source, build, prefix = (
                        root / name / part for part in ("source", "build", "prefix")
                    )
                    source.mkdir(parents=True)
                    header = source / "pocketfft_hdronly.h"
                    header.write_text(content)
                    upstream_bytes = header.read_bytes()
                    (source / "LICENSE.md").write_text(
                        "Test constructor fixture, not an FFT provider.\n"
                    )
                    source_hash = upstream_hash or hashlib.sha256(header.read_bytes()).hexdigest()
                    command = [
                        str(cmake),
                        "-S",
                        str(ROOT / "cmake/dependencies/pocketfft"),
                        "-B",
                        str(build),
                        "-G",
                        "Ninja",
                        f"-DDE_UPSTREAM_SOURCE={source}",
                        f"-DCMAKE_INSTALL_PREFIX={prefix}",
                        "-DPOCKETFFT_NO_MULTITHREADING=ON",
                        "-DPOCKETFFT_CACHE_SIZE=0",
                        f"-DDOCENHANCE_POCKETFFT_FALLIBLE_VECTORS={enabled}",
                        f"-DDOCENHANCE_POCKETFFT_HEADER_SHA256={source_hash}",
                        f"-DDOCENHANCE_POCKETFFT_PATCHED_HEADER_SHA256={derived}",
                    ]
                    result = subprocess.run(
                        command, capture_output=True, text=True, check=False, timeout=15
                    )
                    self.assertEqual(header.read_bytes(), upstream_bytes)
                    if refusal:
                        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn(refusal, result.stdout + result.stderr)
                        self.assertFalse(
                            (prefix / "include/pocketfft/pocketfft_hdronly.h").exists()
                        )
                        continue
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    install = subprocess.run(
                        [str(cmake), "--install", str(build)],
                        capture_output=True,
                        text=True,
                        check=False,
                        timeout=15,
                    )
                    self.assertEqual(install.returncode, 0, install.stdout + install.stderr)
                    owned = build / "owned-source/pocketfft_hdronly.h"
                    installed = prefix / "include/pocketfft/pocketfft_hdronly.h"
                    self.assertEqual(owned.read_bytes(), expected.encode())
                    self.assertEqual(installed.read_bytes(), owned.read_bytes())
                    self.assertEqual(header.read_bytes(), upstream_bytes)

    def test_installed_pocketfft_header_has_independent_identity(self) -> None:
        """Changed upstream or two agreeing private copies cannot replace reviewed bytes."""
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
            header.write_bytes(b"reviewed upstream fixture")
            owned = binary / "owned-source/pocketfft_hdronly.h"
            owned.parent.mkdir()
            owned.write_bytes(b"reviewed private fixture")
            installed.write_bytes(b"different")
            self.assertTrue(audit_build.pocketfft_header_failures(binary))
            upstream = hashlib.sha256(header.read_bytes()).hexdigest()
            expected = hashlib.sha256(owned.read_bytes()).hexdigest()
            with patch("audit_build.POCKETFFT_UPSTREAM_HEADER_SHA256", upstream):
                self.assertTrue(audit_build.pocketfft_header_failures(binary))
                installed.write_bytes(owned.read_bytes())
                self.assertTrue(audit_build.pocketfft_header_failures(binary))
                with patch("audit_build.POCKETFFT_HEADER_SHA256", expected):
                    self.assertFalse(audit_build.pocketfft_header_failures(binary))
                    header.write_bytes(b"changed upstream")
                    self.assertTrue(audit_build.pocketfft_header_failures(binary))
                    header.write_bytes(b"reviewed upstream fixture")
                    owned.write_bytes(b"both copies changed")
                    installed.write_bytes(owned.read_bytes())
                    self.assertTrue(audit_build.pocketfft_header_failures(binary))

    @staticmethod
    def configure(
        cmake: str, source: Path, build: Path, selected: Path | None = None
    ) -> subprocess.CompletedProcess[str]:
        """Use the real cache override which the production find_path continues to admit."""
        command = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja"]
        if selected is not None:
            command.append(f"-DDE_POCKETFFT_INCLUDE={selected.as_posix()}")
        return subprocess.run(command, capture_output=True, text=True, check=False, timeout=90)
