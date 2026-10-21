# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Offline relationships of the two acquired capture-provenance bodies."""

from __future__ import annotations

from pathlib import PureWindowsPath
from typing import Any

from jsonschema import ValidationError

from quality_native_body import validate_native_body
from quality_provenance_contract import capture_references
from quality_references import referenced_order
from quality_runtime_evidence import runtime_shape


def native_binding(body: dict[str, Any], receipt: dict[str, Any]) -> None:
    """Correlate an independently shape-checked native body to its acquired receipt."""
    if (
        body["build"] != receipt["build"]
        or body["source_epoch"]["value"]["head"] != receipt["source_commit"]
        or body["executable"]["sha256"] != receipt["executable_sha256"]
    ):
        message = "Captured native body differs from its selected build receipt"
        raise ValueError(message)
    system = body["configuration"]["value"]["CMAKE_SYSTEM_NAME"]
    environment = receipt["capture_scope"]["child_environment"]
    if system == "Windows":
        if (
            set(environment) != {"SystemRoot"}
            or PureWindowsPath(body["imports"]["system_directory"])
            != PureWindowsPath(environment["SystemRoot"]) / "System32"
        ):
            message = "Captured Windows native environment correspondence differs"
            raise ValueError(message)
    elif environment:
        message = "Captured POSIX native producer requires the frozen empty environment"
        raise ValueError(message)


def platform_binding(native: dict[str, Any], runtime: dict[str, Any]) -> None:
    """Compare recorded platforms only; this identifies no current process or implementation."""
    system = native["configuration"]["value"]["CMAKE_SYSTEM_NAME"]
    if {"Linux": "linux", "Darwin": "darwin", "Windows": "win32"}[system] != runtime["observed"][
        "platform"
    ]:
        message = "Captured native and interpreter platforms differ"
        raise ValueError(message)


def snapshot_order(plan: dict[str, Any], entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Append only references derived from the already acquired closed build receipt."""
    primary = referenced_order(plan)
    try:
        nested = capture_references(entries[1]["value"])
    except (ValueError, TypeError, IndexError):
        nested = []
    return primary + nested


def snapshot_failures(plan: dict[str, Any], entries: list[dict[str, Any]]) -> list[int]:
    """Return actual component indices that lack valid captured bodies, without live reads."""
    expected = snapshot_order(plan, entries)
    first = len(referenced_order(plan))
    if len(expected) == first:
        return []
    failures = []
    for index, check in zip(
        range(first, len(expected)), (validate_native_body, runtime_shape), strict=True
    ):
        if index >= len(entries) or entries[index]["reference"] != expected[index]:
            failures.append(index)
            continue
        try:
            check(entries[index]["value"])
            if index == first:
                native_binding(entries[index]["value"], entries[1]["value"])
        except (ValueError, TypeError, KeyError, ValidationError):
            failures.append(index)
    if not failures:
        try:
            platform_binding(entries[first]["value"], entries[first + 1]["value"])
        except (ValueError, TypeError, KeyError):
            failures.append(first + 1)
    return failures
