# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Only verified analysis executables persist after the temporary LLVM build."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import install_llvm
import llvm_cache


class LLVMCacheTests(unittest.TestCase):
    """Exercise cache payload ownership without compiling LLVM or fetching upstream bytes."""

    def test_only_self_contained_tools_survive_the_build(self) -> None:
        """Object files and source archives remain in the caller's temporary work directory."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, target = root / "work", root / "cache"
            work.mkdir()
            commands = []

            def invoke(command: list[str], **_: object) -> None:
                commands.append(command)
                if "--build" in command:
                    binary = work / "build/bin"
                    binary.mkdir(parents=True)
                    for name in ("clang-tidy", "clang-query"):
                        executable = binary / name
                        executable.write_bytes(name.encode())
                        executable.chmod(0o755)
                    (work / "build/large-object.o").write_bytes(b"discard")
                    headers = work / "build/lib/clang/23/include"
                    headers.mkdir(parents=True)
                    (headers / "stddef.h").write_bytes(b"builtin")

            def acquire(_: str, destination: Path) -> None:
                destination.write_bytes(b"locked archive")

            pin = json.loads((ROOT / "deps/tools.json").read_text())["clang_tidy"]["install"][
                "macos"
            ] | {
                "source_url": "https://example.invalid/llvm",
                "source_sha256": hashlib.sha256(b"locked archive").hexdigest(),
            }
            with (
                patch("install_llvm.download", side_effect=acquire),
                patch("install_llvm.tool", side_effect=lambda name: name),
                patch("install_llvm.subprocess.run", side_effect=invoke),
            ):
                install_llvm.build_source_tools(pin, work, target)
            self.assertEqual({path.name for path in target.iterdir()}, {"bin", "lib"})
            self.assertEqual(
                {path.name for path in (target / "bin").iterdir()}, {"clang-tidy", "clang-query"}
            )
            for name in ("clang-tidy", "clang-query"):
                self.assertEqual((target / "bin" / name).read_bytes(), name.encode())
                self.assertTrue((target / "bin" / name).stat().st_mode & 0o111)
            self.assertEqual((target / "lib/clang/23/include/stddef.h").read_bytes(), b"builtin")
            self.assertIn("clang-resource-headers", commands[2])
            configure = commands[1]
            self.assertIn("-DBUILD_SHARED_LIBS=OFF", configure)
            self.assertIn("-DLLVM_LINK_LLVM_DYLIB=OFF", configure)
            self.assertIn("-DCLANG_LINK_CLANG_DYLIB=OFF", configure)
            self.assertIn(str(work / "build"), configure)
            self.assertNotIn(str(target / "build"), configure)

    def test_unexpected_payload_or_symlink_receipt_cannot_be_restored(self) -> None:
        """Matching executable hashes cannot bless an unbounded or redirected cache payload."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "bin"
            binary.mkdir()
            headers = root / "lib/clang/23/include"
            headers.mkdir(parents=True)
            (headers / "stddef.h").write_bytes(b"builtin")
            identity = {"version": "23.1.3"}
            for name in ("clang-tidy", "clang-query"):
                (binary / name).write_bytes(name.encode())
            receipt = root / "receipt.json"
            receipt.write_text(json.dumps(llvm_cache.source_tool_receipt(root, identity)))
            llvm_cache.validate_source_tools(root, identity)
            (headers / "stddef.h").write_bytes(b"changed")
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity)
            (headers / "stddef.h").unlink()
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity)
            (headers / "stddef.h").write_bytes(b"builtin")
            (headers / "unexpected-empty-directory").mkdir()
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity)
            (headers / "unexpected-empty-directory").rmdir()
            if hasattr(os, "mkfifo"):
                os.mkfifo(headers / "unexpected-fifo")
                with self.assertRaisesRegex(llvm_cache.InstallError, "Unsupported"):
                    llvm_cache.validate_source_tools(root, identity)
                (headers / "unexpected-fifo").unlink()
            (root / "objects").mkdir()
            with self.assertRaises(llvm_cache.InstallError):
                llvm_cache.validate_source_tools(root, identity)
            (root / "objects").rmdir()
            original = root.parent / (root.name + "-receipt")
            receipt.rename(original)
            try:
                receipt.symlink_to(original)
                with self.assertRaises(llvm_cache.InstallError):
                    llvm_cache.validate_source_tools(root, identity)
            finally:
                original.unlink()

    def test_resource_limits_refuse_before_any_hashing(self) -> None:
        """Entry and logical-byte bounds reject excess input before reading payloads."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "one.h").write_bytes(b"1234")
            (root / "nested").mkdir()
            expected = {"nested": "directory", "one.h": hashlib.sha256(b"1234").hexdigest()}
            self.assertEqual(llvm_cache.resource_inventory(root), expected)
            for name, value, message in (
                ("MAX_RESOURCE_ENTRIES", 1, "entry"),
                ("MAX_RESOURCE_BYTES", 3, "byte"),
            ):
                with (
                    patch.object(llvm_cache, name, value),
                    patch("llvm_cache.os.open") as opened,
                    self.assertRaisesRegex(llvm_cache.InstallError, message),
                ):
                    llvm_cache.resource_inventory(root)
                opened.assert_not_called()
            (root / "link.h").symlink_to(root / "one.h")
            with self.assertRaisesRegex(llvm_cache.InstallError, "Unsupported"):
                llvm_cache.resource_inventory(root)

    def test_receipt_and_header_paths_are_bounded(self) -> None:
        """Oversize receipts and path names refuse independently of matching hashes."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            receipt = root / "receipt.json"
            receipt.write_bytes(b" " * 9)
            with (
                patch("llvm_cache.MAX_RECEIPT_BYTES", 8),
                self.assertRaisesRegex(llvm_cache.InstallError, "receipt exceeds"),
            ):
                llvm_cache.read_receipt(receipt)
            receipt.unlink()
            (root / "long.h").write_bytes(b"builtin")
            with (
                patch("llvm_cache.MAX_RESOURCE_PATH_BYTES", 3),
                patch("llvm_cache.os.open") as opened,
                self.assertRaisesRegex(llvm_cache.InstallError, "path exceeds"),
            ):
                llvm_cache.resource_inventory(root)
            opened.assert_not_called()

    def test_failed_build_leaves_no_persistent_cache(self) -> None:
        """Compilation failures cannot retain partial objects as a purported tool cache."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, target = root / "work", root / "cache"
            work.mkdir()
            pin = {
                "source_url": "https://example.invalid",
                "source_sha256": hashlib.sha256(b"archive").hexdigest(),
            }

            def invoke(command: list[str], **_: object) -> None:
                if "--build" in command:
                    raise subprocess.CalledProcessError(1, command)

            with (
                patch(
                    "install_llvm.download",
                    side_effect=lambda _, destination: destination.write_bytes(b"archive"),
                ),
                patch("install_llvm.tool", side_effect=lambda name: name),
                patch("install_llvm.subprocess.run", side_effect=invoke),
                self.assertRaises(subprocess.CalledProcessError),
            ):
                install_llvm.build_source_tools(pin, work, target)
            self.assertFalse(target.exists())
