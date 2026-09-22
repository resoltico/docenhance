#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Reconcile one changelog-bound source release without overwriting remote state."""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol, cast

from changelog import require

if TYPE_CHECKING:
    from pathlib import Path

Record = dict[str, object]


def record(value: object) -> Record:
    """Validate one GitHub JSON object."""
    require("Expected a GitHub JSON object", condition=isinstance(value, dict))
    result = cast("Record", value)
    require(
        "Invalid JSON object keys",
        condition=all(isinstance(key, str) for key in result),
    )
    return result


def identifier(value: Record) -> int:
    """Return one positive GitHub release identifier."""
    item = value.get("id")
    require("Invalid GitHub release identifier", condition=type(item) is int and item > 0)
    return cast("int", item)


@dataclass(frozen=True, slots=True)
class Artifact:
    """Bind one verified source asset to its byte identity."""

    path: Path
    digest: str
    size: int

    @classmethod
    def inspect(cls, path: Path) -> Artifact:
        """Inspect one regular, non-symlinked asset."""
        require(
            "Artifact is not a regular file",
            condition=path.is_file() and not path.is_symlink(),
        )
        with path.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
            size = os.fstat(source.fileno()).st_size
        return cls(path, "sha256:" + digest, size)


@dataclass(frozen=True, slots=True)
class Release:
    """Bind an immutable publication to one tag, body, and asset set."""

    tag: str
    commit: str
    body: str
    artifacts: tuple[Artifact, ...]

    @property
    def title(self) -> str:
        """Return the deterministic public title."""
        return "DocEnhance " + self.tag.removeprefix("v")


class GitHub(Protocol):
    """Expose the minimal release-reconciliation transport."""

    def tag_commit(self, tag: str) -> str:
        """Return the remote tag target commit."""

    def releases(self) -> list[Record]:
        """Return all existing releases."""

    def release(self, release_id: int) -> Record:
        """Return one release by numeric identity."""

    def assets(self, release_id: int) -> list[Record]:
        """Return all assets attached to one release."""

    def create(self, desired: Release) -> Record:
        """Create one draft matching the desired release."""

    def upload(self, tag: str, artifact: Artifact) -> None:
        """Upload one pre-verified asset."""

    def publish(self, release_id: int) -> None:
        """Publish a complete verified draft."""


def _assert_tag(api: GitHub, desired: Release) -> None:
    require(
        "Remote tag target mismatch",
        condition=api.tag_commit(desired.tag) == desired.commit,
    )


def _metadata(actual: Record, desired: Release) -> bool:
    """Validate metadata and return whether the exact release is a draft."""
    for key, value in {
        "tag_name": desired.tag,
        "name": desired.title,
        "body": desired.body,
        "prerelease": False,
    }.items():
        require(
            f"Release metadata mismatch: {key}",
            condition=type(actual.get(key)) is type(value) and actual.get(key) == value,
        )
    draft = actual.get("draft")
    require("Invalid release draft flag", condition=type(draft) is bool)
    if draft:
        require(
            "Draft target mismatch",
            condition=actual.get("target_commitish") == desired.commit,
        )
    else:
        published = actual.get("published_at")
        require(
            "Invalid publication date",
            condition=isinstance(published, str) and bool(published),
        )
    return cast("bool", draft)


def _missing_assets(actual: list[Record], desired: Release) -> list[Artifact]:
    """Validate remote assets and return only missing verified local assets."""
    expected = {item.path.name: item for item in desired.artifacts}
    require("No verified release assets supplied", condition=bool(expected))
    require("Duplicate local asset names", condition=len(expected) == len(desired.artifacts))
    seen: set[str] = set()
    for item in actual:
        name = item.get("name")
        require("Invalid remote asset name", condition=isinstance(name, str))
        require(
            "Unexpected or duplicate remote asset",
            condition=name in expected and name not in seen,
        )
        wanted = expected[cast("str", name)]
        require(f"Incomplete remote asset: {name}", condition=item.get("state") == "uploaded")
        size = item.get("size")
        require(
            f"Invalid remote asset size: {name}",
            condition=type(size) is int and size >= 0,
        )
        require(f"Remote asset size mismatch: {name}", condition=size == wanted.size)
        require(
            f"Remote asset digest mismatch: {name}",
            condition=item.get("digest") == wanted.digest,
        )
        seen.add(cast("str", name))
    return [item for name, item in expected.items() if name not in seen]


def _state(api: GitHub, release_id: int, desired: Release) -> tuple[bool, list[Artifact]]:
    """Read and verify the complete current remote release state."""
    actual = api.release(release_id)
    require("Release identity changed", condition=identifier(actual) == release_id)
    draft = _metadata(actual, desired)
    missing = _missing_assets(api.assets(release_id), desired)
    require("Published release is missing expected assets", condition=draft or not missing)
    return draft, missing


def publish_release(api: GitHub, desired: Release) -> int:
    """Publish or verify the changelog-bound release without remote mutation."""
    _missing_assets([], desired)
    for artifact in desired.artifacts:
        require("Local asset changed", condition=Artifact.inspect(artifact.path) == artifact)
    _assert_tag(api, desired)
    matches = [item for item in api.releases() if item.get("tag_name") == desired.tag]
    require("Multiple releases refer to the same tag", condition=len(matches) <= 1)
    if matches:
        release_id = identifier(matches[0])
    else:
        created = api.create(desired)
        require("Release creation did not return a draft", condition=_metadata(created, desired))
        release_id = identifier(created)
    draft, missing = _state(api, release_id, desired)
    for artifact in missing:
        require("Release was published by another writer", condition=draft)
        _assert_tag(api, desired)
        draft, missing = _state(api, release_id, desired)
        if artifact not in missing:
            continue
        require("Local asset changed", condition=Artifact.inspect(artifact.path) == artifact)
        api.upload(desired.tag, artifact)
        draft, missing = _state(api, release_id, desired)
    if draft:
        require("Draft is not complete for publication", condition=not missing)
        _assert_tag(api, desired)
        api.publish(release_id)
    final_draft, remaining = _state(api, release_id, desired)
    require(
        "Publication read-back did not match",
        condition=not final_draft and not remaining,
    )
    _assert_tag(api, desired)
    return release_id
