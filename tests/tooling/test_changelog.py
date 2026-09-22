# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Tests for the changelog-bound source release contract."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import changelog
import publish_source_release
import release_publication


class ChangelogTests(unittest.TestCase):
    """Release prose has one validated Markdown source."""

    def test_current_release_extracts_exact_markdown(self) -> None:
        """The current stable section is returned verbatim without its heading."""
        text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn(
            "`process` now performs B03 fixed-threshold binarization",
            changelog.extract_release(text, "0.2.0"),
        )

    def test_rejects_noncurrent_or_undated_release(self) -> None:
        """An older or invalid section cannot become release prose by selection alone."""
        text = "## [Unreleased]\n\n## [0.2.0] - 2026-09-22\n\n- Current.\n\n## [0.1.0]\n\n- Old.\n"
        with self.assertRaises(changelog.ChangelogError):
            changelog.extract_release(text, "0.1.0")

    def test_source_assets_require_the_exact_checksum_manifest(self) -> None:
        """A source release never publishes a mismatched archive checksum."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            archive = directory / "docenhance-0.2.0-source.tar.gz"
            archive.write_bytes(b"source bytes")
            checksum = archive.with_suffix(archive.suffix + ".sha256")
            checksum.write_text("bad  docenhance-0.2.0-source.tar.gz\n", encoding="utf-8")
            with self.assertRaises(changelog.ReleaseError):
                publish_source_release.assets_directory(directory, "0.2.0")


class PublicationTests(unittest.TestCase):
    """Release reconciliation accepts only the exact verified publication."""

    def test_publishes_then_readback_verifies_without_rewriting(self) -> None:
        """A rerun verifies the release and performs no second remote write."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            paths = (directory / "archive.tar.gz", directory / "archive.tar.gz.sha256")
            for index, path in enumerate(paths):
                path.write_bytes(f"source asset {index}".encode())
            desired = release_publication.Release(
                "v0.2.0",
                "a" * 40,
                "- Canonical changelog prose.\n",
                tuple(release_publication.Artifact.inspect(path) for path in paths),
            )
            api = _FakeGitHub(desired)
            self.assertEqual(release_publication.publish_release(api, desired), 1)
            self.assertEqual(api.writes, ["create", "upload", "upload", "publish"])
            self.assertEqual(release_publication.publish_release(api, desired), 1)
            self.assertEqual(api.writes, ["create", "upload", "upload", "publish"])


class _FakeGitHub:
    """In-memory GitHub release state used to test no-overwrite reconciliation."""

    def __init__(self, desired: release_publication.Release) -> None:
        """Start with one desired tag target and no remote release."""
        self.desired = desired
        self.entries: list[dict[str, object]] = []
        self.files: list[dict[str, object]] = []
        self.writes: list[str] = []

    def tag_commit(self, _tag: str) -> str:
        """Return the only expected tag target."""
        return self.desired.commit

    def releases(self) -> list[dict[str, object]]:
        """Return every remote release."""
        return self.entries.copy()

    def release(self, _release_id: int) -> dict[str, object]:
        """Return the single released record."""
        return self.entries[0].copy()

    def assets(self, _release_id: int) -> list[dict[str, object]]:
        """Return all attached assets."""
        return self.files.copy()

    def create(self, desired: release_publication.Release) -> dict[str, object]:
        """Create the exact requested draft."""
        self.entries.append(
            {
                "id": 1,
                "tag_name": desired.tag,
                "name": desired.title,
                "body": desired.body,
                "prerelease": False,
                "draft": True,
                "target_commitish": desired.commit,
                "published_at": None,
            }
        )
        self.writes.append("create")
        return self.entries[0].copy()

    def upload(self, _tag: str, artifact: release_publication.Artifact) -> None:
        """Add one exact verified asset."""
        self.files.append(
            {
                "name": artifact.path.name,
                "state": "uploaded",
                "size": artifact.size,
                "digest": artifact.digest,
            }
        )
        self.writes.append("upload")

    def publish(self, _release_id: int) -> None:
        """Publish the complete draft."""
        self.entries[0].update({"draft": False, "published_at": "2026-09-22T00:00:00Z"})
        self.writes.append("publish")
