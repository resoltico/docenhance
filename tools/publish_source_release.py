#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Validate changelog prose or publish the current tag's verified source assets."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
import tempfile
from pathlib import Path
from typing import cast

from changelog import ReleaseError, extract_release, require
from github_release_api import COMMIT, GitHubAPI, run
from package_source import make_archive
from project_version import SEMVER, project_version, version_from_text
from release_publication import Artifact, Release, publish_release

ROOT = Path(__file__).resolve().parents[1]


def assets_directory(directory: Path, version: str) -> tuple[Path, Path]:
    """Return precisely the archive and checksum allowed in a source release."""
    archive = directory / f"docenhance-{version}-source.tar.gz"
    checksum = archive.with_suffix(archive.suffix + ".sha256")
    actual = {path.name for path in directory.iterdir()} if directory.is_dir() else set()
    expected = {archive.name, checksum.name}
    require(f"Unexpected source release assets: {sorted(actual)}", condition=actual == expected)
    require(
        "Source release assets must be regular files",
        condition=(
            archive.is_file()
            and not archive.is_symlink()
            and checksum.is_file()
            and not checksum.is_symlink()
        ),
    )
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    expected_checksum = f"{digest}  {archive.name}\n".encode()
    require(
        "Source release checksum has an invalid length",
        condition=checksum.stat().st_size == len(expected_checksum),
    )
    with checksum.open("rb") as stream:
        supplied_checksum = stream.read(len(expected_checksum) + 1)
    require(
        "Source release checksum does not match its archive",
        condition=supplied_checksum == expected_checksum,
    )
    return archive, checksum


def same_bytes(supplied: Path, expected: Path) -> bool:
    """Refuse links and length mismatches before comparing bounded chunks."""
    if (
        supplied.is_symlink()
        or not supplied.is_file()
        or supplied.stat().st_size != expected.stat().st_size
    ):
        return False
    with supplied.open("rb") as actual, expected.open("rb") as reference:
        while block := reference.read(64 * 1024):
            if actual.read(len(block)) != block:
                return False
        return actual.read(1) == b""


def _git(*arguments: str) -> str:
    """Run Git in the source checkout with the safe publication transport."""
    git = shutil.which("git")
    require("git is required for release publication", condition=git is not None)
    return run([cast("str", git), "-C", str(ROOT), *arguments]).strip()


def _context() -> tuple[str, str, str]:
    """Validate the exact clean tag-push context."""
    require(
        "Publication requires a tag push",
        condition=os.environ.get("GITHUB_EVENT_NAME") == "push",
    )
    require(
        "Publication requires a tag ref",
        condition=os.environ.get("GITHUB_REF_TYPE") == "tag",
    )
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    tag = os.environ.get("GITHUB_REF_NAME")
    commit = os.environ.get("GITHUB_SHA")
    require("Invalid repository", condition=bool(repository))
    require(
        "Invalid tag",
        condition=isinstance(tag, str)
        and tag.startswith("v")
        and SEMVER.fullmatch(tag[1:]) is not None,
    )
    require(
        "Invalid event commit",
        condition=isinstance(commit, str) and COMMIT.fullmatch(commit) is not None,
    )
    require(
        "Inconsistent event ref",
        condition=os.environ.get("GITHUB_REF") == f"refs/tags/{tag}",
    )
    require("Checkout/event commit mismatch", condition=_git("rev-parse", "HEAD") == commit)
    require(
        "Tracked files changed",
        condition=not _git("status", "--porcelain", "--untracked-files=no"),
    )
    return repository, cast("str", tag), cast("str", commit)


def publish(directory: Path) -> str:
    """Publish the exact tagged source assets and exact tagged changelog body."""
    repository, tag, commit = _context()
    version = version_from_text(_git("show", f"{commit}:CMakeLists.txt"))
    require("Tag/project version mismatch", condition=tag == f"v{version}")
    body = extract_release(_git("show", f"{commit}:CHANGELOG.md"), version)
    archive = directory / f"docenhance-{version}-source.tar.gz"
    checksum = archive.with_suffix(archive.suffix + ".sha256")
    with tempfile.TemporaryDirectory(prefix="docenhance-release-") as temporary:
        expected = make_archive(Path(temporary), epoch=0, revision=commit)
        expected_artifacts = []
        for supplied, regenerated in (
            (archive, expected),
            (checksum, expected.with_suffix(expected.suffix + ".sha256")),
        ):
            require(
                "Source release asset differs from selected commit",
                condition=same_bytes(supplied, regenerated),
            )
            expected_artifacts.append(Artifact.inspect(regenerated))
        artifacts = tuple(Artifact.inspect(path) for path in assets_directory(directory, version))
        require(
            "Source release asset changed after source agreement",
            condition=all(
                actual.digest == expected.digest and actual.size == expected.size
                for actual, expected in zip(artifacts, expected_artifacts, strict=True)
            ),
        )
    publish_release(GitHubAPI(repository), Release(tag, commit, body, artifacts))
    return f"https://github.com/{repository}/releases/tag/{tag}"


def main(argv: list[str] | None = None) -> int:
    """Print validated notes, or publish and re-verify a source release."""
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--assets-directory", type=Path)
    arguments = parser.parse_args(argv)
    try:
        if arguments.check:
            version = project_version(ROOT)
            changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
            sys.stdout.write(extract_release(changelog, version))
        else:
            print(publish(arguments.assets_directory))
    except (OSError, ReleaseError, ValueError) as error:
        print(f"Release failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
