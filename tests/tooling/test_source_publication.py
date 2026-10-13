# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real committed trees and corrupt assets exercise pre-effect release admission."""

from __future__ import annotations

import hashlib
import io
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path
from typing import TYPE_CHECKING, Any, override
from unittest.mock import patch

from tools_path import ROOT

import package_source
import publish_source_release
from changelog import ReleaseError
from dep_verify import command_environment
from github_release_api import run

if TYPE_CHECKING:
    from typing import IO


class SourcePublicationTests(unittest.TestCase):
    """No network call is used; actual Git and archive owners supply admission facts."""

    @override
    def setUp(self) -> None:
        """Keep fixture commits, supplied artifacts and private regeneration independent."""
        self.assertTrue((ROOT / "tools/package_source.py").is_file())
        self.temporary = tempfile.TemporaryDirectory(prefix="docenhance-source-publication-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.root = self.directory / "repo"
        self.root.mkdir()
        self.git("init")
        (self.root / "CMakeLists.txt").write_text(
            "project(docenhance VERSION 0.2.0 LANGUAGES CXX)\n"
        )
        (self.root / "CHANGELOG.md").write_text(
            "## [Unreleased]\n\n## [0.2.0] - 2026-10-13\n\n- Frozen prose.\n"
        )
        self.content = {
            "unusual \u0101 name.dat": b"\x00\xff\n",
            "fuzz/regressions/sample": b"retained\x00",
        }
        for name, data in self.content.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.commit = self.commit_tree("selected")
        self.assets = self.directory / "assets"
        self.addCleanup(patch.stopall)
        patch("package_source.ROOT", self.root).start()
        patch("publish_source_release.ROOT", self.root).start()
        self.archive = package_source.make_archive(self.assets, revision=self.commit)
        self.checksum = self.archive.with_suffix(self.archive.suffix + ".sha256")
        patch.dict(
            os.environ,
            {
                "GITHUB_EVENT_NAME": "push",
                "GITHUB_REF_TYPE": "tag",
                "GITHUB_REPOSITORY": "fixture/project",
                "GITHUB_REF_NAME": "v0.2.0",
                "GITHUB_REF": "refs/tags/v0.2.0",
                "GITHUB_SHA": self.commit,
            },
        ).start()

    def git(self, *arguments: str) -> str:
        """Use joined real Git without user signing/configuration or network."""
        result = subprocess.run(
            [
                str(shutil.which("git")),
                "-C",
                str(self.root),
                "-c",
                "commit.gpgsign=false",
                "-c",
                "user.name=Fixture",
                "-c",
                "user.email=fixture@example.invalid",
                "-c",
                "maintenance.autoDetach=false",
                "-c",
                "gc.autoDetach=false",
                *arguments,
            ],
            capture_output=True,
            check=True,
            text=True,
            env=command_environment(),
            timeout=60,
        )
        return result.stdout.strip()

    def commit_tree(self, message: str) -> str:
        """Commit fixture bytes and return their immutable identity."""
        self.git("add", ".")
        self.git("commit", "-m", message)
        return self.git("rev-parse", "HEAD")

    def checksum_archive(self) -> None:
        """Give even deliberately forged assets an internally matching checksum."""
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.checksum.write_text(f"{digest}  {self.archive.name}\n")

    def assert_preflight_refused(self) -> None:
        """A refusal must happen before constructing any remote transport."""
        with (
            patch("publish_source_release.GitHubAPI") as api,
            patch("publish_source_release.publish_release") as publish,
        ):
            with self.assertRaises(ReleaseError):
                publish_source_release.publish(self.assets)
            api.assert_not_called()
            publish.assert_not_called()

    def test_selected_archive_preserves_unusual_names_and_regression_bytes(self) -> None:
        """Exact selected bytes pass, and untracked files cannot enter regeneration."""
        (self.root / "untracked").write_bytes(b"excluded")
        with tarfile.open(self.archive) as archive:
            for name, data in self.content.items():
                stream = archive.extractfile("docenhance/" + name)
                if stream is None:
                    self.fail("Committed regular member has no readable stream")
                self.assertEqual(stream.read(), data)
        with (
            patch("publish_source_release.GitHubAPI") as api,
            patch("publish_source_release.publish_release") as publish,
        ):
            self.assertEqual(
                publish_source_release.publish(self.assets),
                "https://github.com/fixture/project/releases/tag/v0.2.0",
            )
            api.assert_called_once_with("fixture/project")
            desired = publish.call_args.args[1]
            self.assertEqual(desired.commit, self.commit)
            self.assertEqual(desired.body, "- Frozen prose.\n")
            self.assertEqual(len(desired.artifacts), 2)

    def test_ambient_head_movement_does_not_change_admitted_commit(self) -> None:
        """HEAD can move after context admission without rebinding archive or prose."""
        actual_run = run

        def move_head(command: list[str]) -> str:
            admitted = actual_run(command)
            if command[-2:] != ["show", f"{self.commit}:CMakeLists.txt"]:
                return admitted
            (self.root / "CHANGELOG.md").write_text(
                "## [Unreleased]\n\n## [0.2.0] - 2026-10-13\n\n- Changed prose.\n"
            )
            self.commit_tree("later same version")
            return admitted

        with (
            patch("publish_source_release.run", side_effect=move_head),
            patch("publish_source_release.GitHubAPI"),
            patch("publish_source_release.publish_release") as publish,
        ):
            publish_source_release.publish(self.assets)
            self.assertNotEqual(self.git("rev-parse", "HEAD"), self.commit)
            self.assertEqual(publish.call_args.args[1].commit, self.commit)
            self.assertEqual(publish.call_args.args[1].body, "- Frozen prose.\n")

    def test_wrong_same_version_commit_is_refused(self) -> None:
        """Version equality and a valid checksum cannot authenticate a different tree."""
        (self.root / "other").write_bytes(b"different tree")
        later = self.commit_tree("other source")
        wrong = package_source.make_archive(self.directory / "wrong", revision=later)
        self.archive.write_bytes(wrong.read_bytes())
        self.checksum_archive()
        self.git("reset", "--hard", self.commit)
        self.assert_preflight_refused()

    def test_arbitrary_and_malformed_archives_are_refused_before_transport(self) -> None:
        """Entry additions, deletions, aliases and forged manifests cannot self-certify."""
        cases: tuple[tuple[str, list[tuple[str, bytes, bytes]]], ...] = (
            ("changed", [("docenhance/CMakeLists.txt", b"changed", tarfile.REGTYPE)]),
            ("missing", []),
            ("extra", [("docenhance/extra", b"extra", tarfile.REGTYPE)]),
            (
                "duplicate",
                [
                    ("docenhance/a", b"one", tarfile.REGTYPE),
                    ("docenhance/a", b"two", tarfile.REGTYPE),
                ],
            ),
            ("link", [("docenhance/link", b"", tarfile.SYMTYPE)]),
            ("traversal", [("../escape", b"escape", tarfile.REGTYPE)]),
            ("manifest", [("docenhance/SOURCE_MANIFEST.sha256", b"forged", tarfile.REGTYPE)]),
        )
        original = self.archive.read_bytes()
        for name, entries in cases:
            with self.subTest(name=name):
                buffer = io.BytesIO()
                with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
                    for path, data, kind in entries:
                        info = tarfile.TarInfo(path)
                        info.type = kind
                        info.size = len(data)
                        if kind == tarfile.SYMTYPE:
                            info.linkname = "outside"
                        archive.addfile(info, io.BytesIO(data))
                self.archive.write_bytes(buffer.getvalue())
                self.checksum_archive()
                self.assert_preflight_refused()
        self.archive.write_bytes(b"arbitrary bytes".ljust(len(original), b"!"))
        self.checksum_archive()
        self.assert_preflight_refused()

    def test_length_mismatch_is_refused_without_opening_supplied_bytes(self) -> None:
        """Oversized untrusted assets never require a whole-payload read or hash."""
        expected = self.directory / "expected"
        expected.write_bytes(b"small")
        self.archive.write_bytes(b"large" * 1000)
        actual_open = Path.open

        def guarded_open(path: Path, mode: str = "r") -> IO[Any]:
            self.assertNotEqual(path, self.archive, "Length mismatch opened supplied archive")
            return actual_open(path, mode)

        with patch.object(Path, "open", guarded_open):
            self.assertFalse(publish_source_release.same_bytes(self.archive, expected))
        self.assert_preflight_refused()

    def test_missing_extra_and_linked_assets_are_refused(self) -> None:
        """Closed inventory and regular-file admission hold before remote construction."""
        checksum = self.checksum.read_bytes()
        self.checksum.unlink()
        self.assert_preflight_refused()
        self.checksum.write_bytes(checksum)
        extra = self.assets / "extra"
        extra.write_bytes(b"unexpected")
        self.assert_preflight_refused()
        extra.unlink()
        original = self.archive.read_bytes()
        saved = self.directory / "saved.tar.gz"
        saved.write_bytes(original)
        self.archive.unlink()
        self.archive.symlink_to(saved)
        self.assert_preflight_refused()

    def test_dirty_tracked_source_is_refused_without_remote_effect(self) -> None:
        """Committed-source isolation does not weaken clean tag-push admission."""
        (self.root / "CMakeLists.txt").write_text("dirty worktree")
        self.assert_preflight_refused()

    def test_supplied_asset_mutation_after_comparison_is_refused(self) -> None:
        """The effect plan binds expected identities, not a replacement's own checksum."""
        same_bytes = publish_source_release.same_bytes

        def mutate(supplied: Path, expected: Path) -> bool:
            agrees = same_bytes(supplied, expected)
            if supplied == self.checksum:
                self.archive.write_bytes(b"replacement")
                self.checksum_archive()
            return agrees

        with patch("publish_source_release.same_bytes", side_effect=mutate):
            self.assert_preflight_refused()
