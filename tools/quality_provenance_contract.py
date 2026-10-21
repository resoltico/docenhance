# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Closed existing build receipt and provenance scalar/reference shapes."""

from __future__ import annotations

import ntpath
from typing import Any

BUILD_KEYS = {"application_version", "platform", "compiler", "dependency_lock_sha256"}
SHA256_HEX_LENGTH = 64
WINDOWS_DRIVE_LENGTH = 2
PROVENANCE_KEYS = {"source_commit", "executable_sha256", "build", "capture_scope"}
CAPTURE_KEYS = {"native_producer", "interpreter", "child_environment"}


def exact(value: object, keys: set[str]) -> dict[str, Any]:
    """Require exactly the concrete members owned by this evidence section."""
    if not isinstance(value, dict) or set(value) != keys:
        message = "Quality provenance differs from its closed owning section"
        raise ValueError(message)
    return value


def sha256_text(value: object) -> None:
    """Require lowercase SHA256 identity text without repairing its bytes."""
    if (
        not isinstance(value, str)
        or len(value) != SHA256_HEX_LENGTH
        or any(c not in "0123456789abcdef" for c in value)
    ):
        message = "Provenance identity is not a SHA256"
        raise ValueError(message)


def identity_shape(value: object) -> None:
    """Require the existing positive hash/extent identity domain."""
    value = exact(value, {"sha256", "bytes"})
    sha256_text(value["sha256"])
    if type(value["bytes"]) is not int or not 0 < value["bytes"] < 2**63:
        message = "Provenance identity needs a positive supported byte extent"
        raise ValueError(message)


def reference_shape(value: object) -> None:
    """Reuse the existing closed path/SHA reference, without a parallel reference vocabulary."""
    value = exact(value, {"path", "sha256"})
    sha256_text(value["sha256"])
    if not isinstance(value["path"], str) or not value["path"]:
        message = "Provenance reference path is unavailable"
        raise ValueError(message)


def build_facts(value: object) -> None:
    """Require the native wire's four actual build fields."""
    value = exact(value, BUILD_KEYS)
    if any(not isinstance(item, str) or not item for item in value.values()):
        message = "Quality provenance requires actual nonempty native BuildFacts"
        raise ValueError(message)
    sha256_text(value["dependency_lock_sha256"])


def environment_shape(environment: object) -> None:
    """Offline shape admits the two explicit platform forms, never ambient inheritance."""
    if not isinstance(environment, dict) or set(environment) not in (set(), {"SystemRoot"}):
        message = "Capture environment has unsupported keys"
        raise ValueError(message)
    if environment:
        directory = environment["SystemRoot"]
        if (
            not isinstance(directory, str)
            or not directory
            or "\0" in directory
            or not ntpath.isabs(directory)
            or len(ntpath.splitdrive(directory)[0]) != WINDOWS_DRIVE_LENGTH
        ):
            message = "Windows capture environment requires an absolute selected SystemRoot"
            raise ValueError(message)
        directory.encode("utf-8", "strict")


def provenance_shape(receipt: object) -> None:
    """Past receipt shape checks do not identify its former selected process or runtime."""
    receipt = exact(receipt, PROVENANCE_KEYS)
    build_facts(receipt["build"])
    sha256_text(receipt["executable_sha256"])
    if not isinstance(receipt["source_commit"], str) or not receipt["source_commit"]:
        message = "Source epoch identity is unavailable"
        raise ValueError(message)
    scope = receipt["capture_scope"]
    exact(scope, CAPTURE_KEYS)
    reference_shape(scope["native_producer"])
    reference_shape(scope["interpreter"])
    environment_shape(scope["child_environment"])


def capture_references(receipt: dict[str, Any]) -> list[dict[str, str]]:
    """Return the two existing capture references from the validated receipt."""
    provenance_shape(receipt)
    scope = receipt["capture_scope"]
    return [scope["native_producer"], scope["interpreter"]]
