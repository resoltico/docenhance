# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Tests for dependency pinning, verification, extraction and the generated contract."""

from __future__ import annotations

import hashlib
import io
import json
import tarfile
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import audit_build
import check_all
import check_gates
import deps
import generate_spec
import package_source

EXECUTABLE_MODE = 0o755
LOCKED_DEPENDENCY_COUNT = 11
MIN_CLANG_TIDY_MAJOR = 23


class LockTests(unittest.TestCase):
    """The committed lock is pinned; verification rejects any drift."""

    def test_committed_lock_is_fully_pinned(self) -> None:
        """Every dependency is locked and matches the feature policy."""
        lock = deps.load_lock(ROOT / "deps/lock.json")
        self.assertEqual(len(lock["dependencies"]), LOCKED_DEPENDENCY_COUNT)
        self.assertEqual(
            {d["name"] for d in lock["dependencies"]},
            set(json.loads((ROOT / "deps/features.json").read_text())["dependencies"]),
        )

    def test_bad_object_and_duplicate_are_rejected(self) -> None:
        """Unpinned, duplicate and non-HTTPS entries are rejected."""
        for mutation in ["unpinned", "duplicate", "http"]:
            lock = json.loads((ROOT / "deps/lock.json").read_text())
            if mutation == "unpinned":
                lock["dependencies"][0]["object"] = "latest"
            if mutation == "duplicate":
                lock["dependencies"].append(lock["dependencies"][0])
            if mutation == "http":
                lock["dependencies"][0]["repository"] = "http://example.invalid/source"
            with tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "lock.json"
                path.write_text(json.dumps(lock))
                with self.assertRaises(ValueError):
                    deps.load_lock(path)

    def test_pinned_git_and_receipt_reject_mutation(self) -> None:
        """A modified checkout no longer matches its receipt."""
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            source = cache / "sources" / "fixture"
            source.mkdir(parents=True)
            deps.run("git", "init", str(source))
            deps.run("git", "config", "user.email", "fixture@example.invalid", cwd=source)
            deps.run("git", "config", "user.name", "Fixture", cwd=source)
            (source / "LICENSE").write_text("test fixture license\n")
            (source / "data").write_text("original\n")
            deps.run("git", "add", ".", cwd=source)
            deps.run("git", "-c", "commit.gpgsign=false", "commit", "-m", "fixture", cwd=source)
            deps.run(
                "git", "-c", "tag.gpgsign=false", "tag", "-a", "v1", "-m", "fixture", cwd=source
            )
            obj = deps.run("git", "rev-parse", "refs/tags/v1", cwd=source)
            dep = {
                "name": "fixture",
                "transport": "git",
                "ref": "refs/tags/v1",
                "object": obj,
                "object_type": "tag",
                "license_files": ["LICENSE"],
            }
            receipt = deps.receipt_for(dep, source)
            (cache / "receipts").mkdir()
            (cache / "receipts" / "fixture.json").write_text(json.dumps(receipt))
            self.assertEqual(deps.verify(dep, cache), receipt)
            (source / "data").write_text("modified\n")
            with self.assertRaises(ValueError):
                deps.verify(dep, cache)

    def test_archive_digest_and_source_inventory(self) -> None:
        """File digests and the source inventory agree."""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bytes"
            path.write_bytes(b"abc")
            self.assertEqual(deps.digest_file(path), hashlib.sha256(b"abc").hexdigest())
            self.assertEqual(
                deps.inventory(Path(temp)), {"bytes": hashlib.sha256(b"abc").hexdigest()}
            )


class LocalCheckTests(unittest.TestCase):
    """The authoritative local check list stays runnable."""

    def test_every_check_exists(self) -> None:
        """Each entry names a script in tools/ or a module that can be imported."""
        self.assertTrue(check_all.CHECKS)
        for name, arguments in check_all.CHECKS:
            if arguments[0].endswith(".py"):
                self.assertTrue((ROOT / arguments[0]).is_file(), name)
            else:
                self.assertEqual(arguments[0], "-m", name)


class ArchiveTests(unittest.TestCase):
    """The source archive records the modes the linters expect."""

    def test_mode_follows_shebang(self) -> None:
        """Files with a shebang are executable; library modules and data are not."""
        self.assertEqual(package_source.file_mode(b"#!/usr/bin/env python3\n"), EXECUTABLE_MODE)
        self.assertEqual(package_source.file_mode(b"# SPDX-License-Identifier: MIT\n"), 0o644)

    def test_repository_modes_match_the_archive(self) -> None:
        """Checked-out files already carry the modes the archive would record."""
        for path, _, _ in check_gates.code_files(ROOT):
            data = path.read_bytes()
            executable = bool(path.stat().st_mode & 0o111)
            self.assertEqual(executable, package_source.file_mode(data) == EXECUTABLE_MODE, path)

    def test_machine_metadata_is_not_a_source_file(self) -> None:
        """OS and editor droppings never enter a published source archive."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "keep.txt").write_text("source\n")
            for name in (".DS_Store", "Thumbs.db", "desktop.ini"):
                (root / name).write_text("metadata\n")
            self.assertEqual(
                [relative.as_posix() for _, relative in package_source.source_files(root)],
                ["keep.txt"],
            )


class ExtractionTests(unittest.TestCase):
    """Archive extraction rejects traversal and escaping links."""

    def test_traversal_is_rejected(self) -> None:
        """Members escaping the target directory are refused."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "attack.tar"
            with tarfile.open(archive, "w") as tf:
                info = tarfile.TarInfo("../escaped")
                info.size = 1
                tf.addfile(info, io.BytesIO(b"x"))
            with self.assertRaises(tarfile.FilterError):
                deps.safe_extract(archive, root / "extract")
            self.assertFalse((root / "escaped").exists())

    def test_escaping_symlink_is_rejected(self) -> None:
        """Symlinks pointing outside the target are refused."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "attack.tar"
            with tarfile.open(archive, "w") as tf:
                info = tarfile.TarInfo("source/link")
                info.type = tarfile.SYMTYPE
                info.linkname = "../../outside"
                tf.addfile(info)
            with self.assertRaises(tarfile.FilterError):
                deps.safe_extract(archive, root / "extract")

    def test_valid_archive_extracts(self) -> None:
        """A well-formed archive extracts its contents."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / "valid.tar"
            with tarfile.open(archive, "w") as tf:
                info = tarfile.TarInfo("source/readme")
                info.size = 3
                tf.addfile(info, io.BytesIO(b"abc"))
            deps.safe_extract(archive, root / "extract")
            self.assertEqual((root / "extract/source/readme").read_bytes(), b"abc")


class ContractTests(unittest.TestCase):
    """Generated files, layering and lint policy match their sources."""

    def test_generated_contract_matches(self) -> None:
        """Generated files equal the generator output."""
        for path, text in generate_spec.outputs().items():
            self.assertEqual(path.read_text(encoding="utf-8"), text)

    def test_clang_tidy_is_strict(self) -> None:
        """clang-tidy stays zero-tolerance and pinned."""
        tidy = (ROOT / ".clang-tidy").read_text(encoding="utf-8")
        self.assertIn("WarningsAsErrors: '*'", tidy)
        self.assertNotIn("-misc-include-cleaner", tidy)
        self.assertIn("bugprone-*", tidy)
        tools = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))
        self.assertGreaterEqual(
            int(str(tools["clang_tidy"]["minimum"]).split(".")[0]), MIN_CLANG_TIDY_MAJOR
        )

    def test_cache_reader(self) -> None:
        """CMakeCache.txt entries parse, including values with "="."""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "CMakeCache.txt"
            path.write_text("// comment\n# comment\nA:BOOL=OFF\nB:STRING=x=y\n")
            self.assertEqual(audit_build.read_cache(path), {"A": "OFF", "B": "x=y"})


if __name__ == "__main__":
    unittest.main()
