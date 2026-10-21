# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One regular-file extent and immutable identity-bound quality evidence acquisition."""

from __future__ import annotations

from pathlib import Path
from typing import Any, TypedDict

from quality_attempt import MAX_RESPONSE_BYTES, CaptureAdmission, issue, note_issue
from quality_file_snapshot import AcquiredFile, _acquire_regular
from quality_json import read_json
from quality_provenance_contract import reference_shape


class FileIdentity(TypedDict):
    """The two actual observations returned by one hash-only regular-file acquisition."""

    sha256: str
    bytes: int


def confined(root: Path, name: str) -> Path:
    """Evidence identities stay confined and cannot redirect through links."""
    path = root / name
    if (
        Path(name).is_absolute()
        or ".." in Path(name).parts
        or not path.resolve().is_relative_to(root)
    ):
        message = "Evaluation path escapes its corpus root"
        raise ValueError(message)
    if any(part.is_symlink() for part in (path, *path.parents)):
        message = "Evaluation evidence must not redirect through symbolic links"
        raise ValueError(message)
    return path


def acquire_file(
    root: Path, reference: dict[str, Any], retain_limit: int | None = None
) -> AcquiredFile:
    """Return same-descriptor facts, including failures, from a confined regular file."""
    return _acquire_regular(confined(root, reference["path"]), reference, retain_limit)


def _require(acquired: AcquiredFile) -> FileIdentity:
    if acquired.failures:
        raise next(iter(acquired.failures.values()))
    if not acquired.identity_bound or acquired.bytes is None or acquired.sha256 is None:
        message = "Evidence identity is unavailable"
        raise ValueError(message)
    return {"sha256": acquired.sha256, "bytes": acquired.bytes}


def acquire_payload(root: Path, reference: dict[str, Any]) -> bytes:
    """Expected length, when supplied, is checked before any payload retention or read."""
    acquired = acquire_file(root, reference)
    _require(acquired)
    if acquired.payload is None:
        message = "Evidence bytes are unavailable"
        raise ValueError(message)
    return acquired.payload


def acquire_reference(root: Path, reference: dict[str, Any]) -> dict[str, Any]:
    """Metadata has its actual B-sized RAM owner, with no invented metadata ceiling."""
    return read_json(acquire_payload(root, reference))


def acquire_json_file(path: Path) -> dict[str, Any]:
    """Project-owned schema metadata uses the same finite descriptor snapshot too."""
    acquired = _acquire_regular(path, {}, None)
    _require(acquired)
    if acquired.payload is None:
        message = "Project metadata bytes are unavailable"
        raise ValueError(message)
    return read_json(acquired.payload)


def fingerprint(path: Path) -> FileIdentity:
    """Hash-only selected executable/source/asset acquisition never retains whole raw bytes."""
    acquired = _acquire_regular(path, {}, 0)
    return _require(acquired)


def acquire_original_stream(
    original: dict[str, Any], name: str, root: Path, admission: CaptureAdmission
) -> bytes | None:
    """Original oversized streams stay in place; only complete <=L bound bytes enter parsing."""
    reference = (original["declared"].get("execution") or {}).get(name) or {}
    stream = original[name]
    if not reference.get("path"):
        stream.update(read_state="unavailable", retention_state="unavailable")
        note_issue(
            original,
            "errors.raw_" + name,
            issue("metadata_unavailable", original["role_index"]),
            admission,
        )
        return None
    try:
        acquired = acquire_file(root, reference, MAX_RESPONSE_BYTES)
    except (OSError, ValueError, TypeError, KeyError, MemoryError) as error:
        stream.update(read_state="unavailable", retention_state="unavailable")
        note_issue(
            original,
            "errors.raw_" + name,
            issue("metadata_unavailable", original["role_index"], error),
            admission,
        )
        return None
    stream.update(
        path=reference["path"],
        read_state=acquired.read_state,
        observed_prefix_bytes=acquired.bytes,
        observed_extent=acquired.extent,
        retained_bytes=acquired.bytes if acquired.stable else None,
        sha256=acquired.sha256 if acquired.stable else None,
        closed=acquired.closed,
        retention_state="complete"
        if acquired.identity_bound and not acquired.failures
        else "unavailable",
    )
    for phase, failure in acquired.failures.items():
        code = "identity_mismatch" if phase == "hash" and isinstance(failure, ValueError) else phase
        if phase == "stat" and isinstance(failure, ValueError):
            code = "metadata_unavailable"
        note_issue(
            original,
            name + ".errors." + phase,
            issue(code, original["role_index"], failure),
            admission,
        )
    return acquired.payload if acquired.identity_bound else None


def identified_reference(root: Path, reference: dict[str, Any]) -> FileIdentity:
    """Match the actual hash-only source evidence file to its existing reference."""
    reference_shape(reference)
    actual = fingerprint(confined(root, reference["path"]))
    if actual["sha256"] != reference["sha256"]:
        message = "Reviewed provenance evidence bytes changed"
        raise ValueError(message)
    return actual


def _declared_identity(result: AcquiredFile, reference: dict[str, Any]) -> None:
    if "sha256" in reference and result.sha256 != reference["sha256"]:
        message = "Evidence payload differs from declared identity"
        raise ValueError(message)
