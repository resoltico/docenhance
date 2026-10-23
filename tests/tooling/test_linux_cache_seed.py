# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real tiny archive/receipt fixtures prove selective, exclusive offline Linux seeding."""

from __future__ import annotations

import json
import os
import stat
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import prepare_tool_imports

import test_dependency_integrity as fixtures

import dep_verify
import linux_cache_seed as seed

prepare_tool_imports()


class LinuxCacheSeedTests(unittest.TestCase):
    """The source and receipt become ready together without copying archive payloads."""

    def cache_pair(self, root: Path) -> tuple[Path, Path, dep_verify.Dependency]:
        """An existing original-archive mount is represented by one pre-existing hard link."""
        host, cache = root / "host", root / "cache"
        host.mkdir()
        cache.mkdir()
        dep = fixtures.DependencyIntegrityTests().archive(host)
        (cache / "archives").mkdir()
        archive = next((host / "archives").iterdir())
        os.link(archive, cache / "archives" / archive.name)
        return host, cache, dep

    def test_only_verified_selected_sources_and_receipts_are_seeded(self) -> None:
        """Retired sources and archives are neither duplicated nor deleted; reuse is read-only."""
        with tempfile.TemporaryDirectory() as raw:
            host, cache, dep = self.cache_pair(Path(raw))
            retired = host / "sources/retired"
            retired.mkdir()
            (retired / "original").write_bytes(b"preserve")
            before = seed.source_epoch((cache / "archives/fixture-1.tar.gz").stat())
            self.assertTrue(seed.seed([dep], host, cache))
            self.assertEqual({path.name for path in (cache / "sources").iterdir()}, {"fixture"})
            self.assertEqual(dep_verify.verify(dep, cache), dep_verify.verify(dep, host))
            self.assertEqual(
                seed.source_epoch((cache / "archives/fixture-1.tar.gz").stat()), before
            )
            original = seed.source_epoch((cache / "sources/fixture/LICENSE").stat())
            self.assertFalse(seed.seed([dep], host, cache))
            self.assertEqual(
                seed.source_epoch((cache / "sources/fixture/LICENSE").stat()), original
            )
            self.assertEqual((retired / "original").read_bytes(), b"preserve")
            self.assertFalse(list(cache.glob(".seed-*")))
            self.assertFalse((cache / "acquisition.active").exists())

    def test_partial_or_foreign_cache_is_never_overwritten(self) -> None:
        """An incomplete source directory is a refusal, never a repair or seed destination."""
        with tempfile.TemporaryDirectory() as raw:
            host, cache, dep = self.cache_pair(Path(raw))
            foreign = cache / "sources/fixture"
            foreign.mkdir(parents=True)
            (foreign / "sentinel").write_bytes(b"foreign")
            with self.assertRaises(dep_verify.DependencyError):
                seed.seed([dep], host, cache)
            self.assertEqual((foreign / "sentinel").read_bytes(), b"foreign")
            self.assertFalse((cache / "receipts").exists())

    def test_joint_source_and_receipt_tampering_cannot_override_original_archive(self) -> None:
        """The existing verifier binds the selected source to the unchanged original artifact."""
        with tempfile.TemporaryDirectory() as raw:
            host, cache, dep = self.cache_pair(Path(raw))
            source = host / "sources/fixture"
            (source / "LICENSE").write_bytes(b"unreviewed")
            (host / "receipts/fixture.json").write_text(
                json.dumps(dep_verify.receipt_for(dep, source))
            )
            with self.assertRaisesRegex(dep_verify.DependencyError, "locked archive"):
                seed.seed([dep], host, cache)
            self.assertFalse((cache / "sources").exists())
            self.assertFalse(list(cache.glob(".seed-*")))

    def test_active_writer_and_redirected_cache_are_not_adopted(self) -> None:
        """Both the host writer and a foreign source directory keep their existing ownership."""
        with tempfile.TemporaryDirectory() as raw:
            host, cache, dep = self.cache_pair(Path(raw))
            (host / "acquisition.active").write_text("another writer")
            with self.assertRaisesRegex(dep_verify.DependencyError, "active"):
                seed.seed([dep], host, cache)
            (host / "acquisition.active").unlink()
            (cache / "sources").symlink_to(host / "sources")
            with self.assertRaisesRegex(dep_verify.DependencyError, "redirect"):
                seed.seed([dep], host, cache)
            self.assertTrue((cache / "sources").is_symlink())

    def test_atomic_ready_receipt_cannot_replace_a_foreign_collision(self) -> None:
        """The final hard link fails exclusively and leaves the other owner's bytes intact."""
        with tempfile.TemporaryDirectory() as raw:
            host, cache, dep = self.cache_pair(Path(raw))
            publish = seed.publish_entry

            def collide(value: dep_verify.Dependency, stage: Path, destination: Path) -> None:
                (destination / "receipts/fixture.json").write_bytes(b"foreign receipt")
                publish(value, stage, destination)

            with (
                patch.object(seed, "publish_entry", side_effect=collide),
                self.assertRaises(OSError),
            ):
                seed.seed([dep], host, cache)
            self.assertEqual((cache / "receipts/fixture.json").read_bytes(), b"foreign receipt")
            with self.assertRaises(ValueError):
                seed.seed([dep], host, cache)

    def test_a_changed_copy_plan_cannot_read_an_unbounded_growing_source(self) -> None:
        """The admitted extent is checked before copying rather than following growth to EOF."""
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            source.mkdir()
            original = source / "value"
            original.write_bytes(b"old")
            copy = seed.source_copy(seed.source_plan(source))
            original.write_bytes(b"changed")
            with self.assertRaisesRegex(dep_verify.DependencyError, "changed before"):
                copy(str(original), str(root / "new"))
            self.assertFalse((root / "new").exists())

    def test_membership_refusal_does_not_depend_on_directory_timestamp_refresh(self) -> None:
        """A late child still refuses when directory stat epochs appear unchanged."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "original").write_text("kept")
            admitted = seed.source_plan(source)
            epoch = seed.source_epoch

            def unchanged_directory(info: os.stat_result) -> tuple[int, ...]:
                return (0,) if stat.S_ISDIR(info.st_mode) else epoch(info)

            def admit_then_grow(_source: Path) -> dict[str, os.stat_result]:
                (source / "late").mkdir()
                return admitted

            with (
                patch.object(seed, "source_epoch", side_effect=unchanged_directory),
                patch.object(seed, "source_plan", side_effect=admit_then_grow),
                self.assertRaisesRegex(dep_verify.DependencyError, "changed during seeding"),
            ):
                seed.copy_source(source, root / "target")
            self.assertFalse((root / "target/late").exists())

    def test_directory_growth_after_admission_is_refused_and_not_copied(self) -> None:
        """An added empty directory cannot bypass the pre-admitted membership bound."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (source / "original").write_text("kept", encoding="utf-8")
            target = root / "target"
            admitted = seed.source_plan(source)

            def admit_then_grow(_source: Path) -> dict[str, os.stat_result]:
                (source / "late").mkdir()
                return admitted

            with (
                patch("linux_cache_seed.source_plan", side_effect=admit_then_grow),
                self.assertRaisesRegex(dep_verify.DependencyError, "changed during seeding"),
            ):
                seed.copy_source(source, target)
            self.assertFalse((target / "late").exists())
