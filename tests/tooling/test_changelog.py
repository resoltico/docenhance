# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Tests for the changelog-bound source release contract."""

from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path

from tools_path import ROOT

import changelog
import publish_source_release
import release_publication


class ChangelogTests(unittest.TestCase):
    """Release prose has one validated Markdown source."""

    def test_latest_dated_release_extracts_exact_markdown(self) -> None:
        """The latest dated section is independent of an upcoming build version."""
        text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        heading = re.search(r"^## \[([^]]+)\] - ", text, re.MULTILINE)
        self.assertIsNotNone(heading)
        if heading is None:
            self.fail("No dated release section exists")
        version = heading[1]
        lines = text.splitlines()
        start = next(
            index for index, line in enumerate(lines) if line.startswith(f"## [{version}] - ")
        )
        end = next(
            (index for index in range(start + 1, len(lines)) if lines[index].startswith("## [")),
            len(lines),
        )
        expected = "\n".join(lines[start + 1 : end]).strip("\n") + "\n"
        self.assertEqual(changelog.extract_release(text, version), expected)

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

    def test_quality_refusal_preserves_prewrite_and_partial_draft_distinctions(self) -> None:
        """Admission failure writes nothing; later refusal leaves a complete unpublished draft."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.tar.gz"
            path.write_bytes(b"source bytes")
            desired = release_publication.Release(
                "v0.2.0",
                "a" * 40,
                "- Canonical outcome.\n",
                (release_publication.Artifact.inspect(path),),
            )
            for cutoff, writes in ((1, []), (3, ["create", "upload"])):
                with self.subTest(cutoff=cutoff):
                    api = _FakeGitHub(desired)
                    api.refuse_on_quality_check = cutoff
                    with self.assertRaisesRegex(changelog.ReleaseError, "Full CI"):
                        release_publication.publish_release(api, desired)
                    self.assertEqual(api.writes, writes)
                    if api.entries:
                        self.assertTrue(api.entries[0]["draft"] is True)


class _FakeGitHub:
    """In-memory GitHub release state used to test no-overwrite reconciliation."""

    def __init__(self, desired: release_publication.Release) -> None:
        """Start with one desired tag target and no remote release."""
        self.desired = desired
        self.entries: list[dict[str, object]] = []
        self.files: list[dict[str, object]] = []
        self.writes: list[str] = []
        self.quality_checks = 0
        self.refuse_on_quality_check = 0

    def tag_commit(self, _tag: str) -> str:
        """Return the only expected tag target."""
        return self.desired.commit

    def verify_quality(self, commit: str) -> None:
        """Refuse selected admission points while recording the exact requested commit."""
        if commit != self.desired.commit:
            message = "CI commit mismatch"
            raise changelog.ReleaseError(message)
        self.quality_checks += 1
        if self.quality_checks == self.refuse_on_quality_check:
            message = "Full CI is not successful"
            raise changelog.ReleaseError(message)

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
