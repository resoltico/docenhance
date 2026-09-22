#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Validate changelog prose or publish the current tag's verified source assets."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path
from typing import cast

from changelog import VERSION, ReleaseError, extract_release, require
from github_release_api import COMMIT, GitHubAPI, run
from project_version import project_version
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
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    require(
        "Source release checksum does not match its archive",
        condition=checksum.read_text(encoding="utf-8") == f"{digest}  {archive.name}\n",
    )
    return archive, checksum


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
        and VERSION.fullmatch(tag[1:]) is not None,
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
    version = project_version(ROOT)
    require("Tag/project version mismatch", condition=tag == f"v{version}")
    body = extract_release(_git("show", f"{commit}:CHANGELOG.md"), version)
    artifacts = tuple(Artifact.inspect(path) for path in assets_directory(directory, version))
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
