# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Source-authored controls for existing native text decoders, without running their producers."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import prepare_tool_imports

import audit_build
import compile_db
import package_inspection
from architecture import ArchitectureError

prepare_tool_imports()


class NativeCaptureParsers(unittest.TestCase):
    """Real decoder text retains its platform and malformed-input refusal domains."""

    def test_cache_text_and_existing_file_owner_have_identical_values(self) -> None:
        """Cache text and existing file owner have identical values."""
        text = "# comment\n// another\nA:BOOL=OFF\nB:STRING=x=y\nignored\nA:BOOL=ON\n"
        expected = {"A": "ON", "B": "x=y"}
        self.assertEqual(audit_build.cache_values(text), expected)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "CMakeCache.txt"
            path.write_text(text, encoding="utf-8")
            self.assertEqual(audit_build.read_cache(path), expected)
        self.assertEqual(
            audit_build.cache_values(text.replace("A:BOOL=ON", "A:BOOL=OFF"))["A"], "OFF"
        )

    def test_import_text_decoders_preserve_platform_domains_without_effects(self) -> None:
        """Import text decoders preserve platform domains without effects."""
        darwin = "docenhance:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
        linux = "libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x1234)\n"
        windows = "    KERNEL32.dll\n    api-ms-win-core-synch-l1-2-0.dll\n"
        with (
            patch("package_inspection.subprocess.run") as process,
            patch("package_inspection.windows_api_host") as loader,
        ):
            self.assertEqual(
                package_inspection.native_import_values("Darwin", darwin),
                ["/usr/lib/libSystem.B.dylib"],
            )
            self.assertEqual(
                package_inspection.native_import_values("Linux", linux),
                ["/lib/x86_64-linux-gnu/libc.so.6"],
            )
            self.assertEqual(
                package_inspection.native_import_values("Windows", windows),
                ["KERNEL32.dll", "api-ms-win-core-synch-l1-2-0.dll"],
            )
            for system, text in (
                ("Darwin", darwin.replace("/usr/lib/", "/selected/")),
                ("Linux", linux.replace("libc.so.6 (", "libunadmitted_fixture.so.1 (")),
                ("Linux", linux + "libm.so.6 => not found\n"),
                ("Windows", windows + "    VCRUNTIME140.dll\n"),
            ):
                with self.subTest(system=system, text=text), self.assertRaises(ValueError):
                    package_inspection.native_import_values(system, text)
            process.assert_not_called()
            loader.assert_not_called()

    def test_existing_windows_lexical_policies_have_one_shared_owner(self) -> None:
        """Existing windows lexical policies have one shared owner."""
        self.assertEqual(package_inspection.native_import_command("Darwin"), ("otool", ["-L"]))
        self.assertEqual(package_inspection.native_import_command("Linux"), ("ldd", []))
        self.assertEqual(
            package_inspection.native_import_command("Windows"), ("dumpbin", ["/DEPENDENTS"])
        )
        self.assertTrue(package_inspection.windows_dynamic_crt("api-ms-win-crt-runtime-l1-1-0.dll"))
        self.assertFalse(package_inspection.windows_dynamic_crt("KERNEL32.dll"))
        self.assertTrue(package_inspection.windows_api_contract("api-ms-win-core-synch-l1-2-0.dll"))
        self.assertFalse(package_inspection.windows_api_contract("unadmitted_fixture.dll"))

    def test_header_text_preserves_spelling_order_spaces_dollars_and_pch(self) -> None:
        """Header text preserves spelling order spaces dollars and pch."""
        text = (
            ". generated/version.hpp\n.. include/a b$.hpp\n"
            ". generated/version.hpp\nwarning: retained\n"
        )
        with patch("compile_db.run_compiler") as process:
            self.assertEqual(
                compile_db.header_trace_values(text),
                (None, ["generated/version.hpp", "include/a b$.hpp", "generated/version.hpp"]),
            )
            self.assertEqual(
                compile_db.header_trace_values("! cache/a b$.pch\n" + text)[0], "cache/a b$.pch"
            )
            process.assert_not_called()

    def test_live_header_resolution_and_missing_file_refusal_remain_in_existing_owner(self) -> None:
        """Live header resolution and missing file refusal remain in existing owner."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "a b$.hpp"
            path.write_text("// header\n", encoding="utf-8")
            self.assertEqual(
                compile_db.header_trace(". a b$.hpp\n", directory, "control.cpp"),
                [(f"{directory}/a b$.hpp", str(path.resolve()))],
            )
            with self.assertRaises(ArchitectureError):
                compile_db.header_trace(". absent.hpp\n", directory, "control.cpp")
            with self.assertRaises(ArchitectureError):
                compile_db.header_trace("! a b$.pch\n", directory, "control.cpp")


if __name__ == "__main__":
    unittest.main()
