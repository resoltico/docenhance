#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Quality gates: anti-god files, the suppression registry, and lint coverage of every file.

Nothing is grandfathered. Size limits have no waiver mechanism at all. Every in-source
suppression must name its rules and be registered with an explanation; registry entries that no
longer match a suppression fail too. Configuration-level relaxations are checked by config_gates.
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path, PurePosixPath
from typing import TYPE_CHECKING

import config_gates
import repo_hygiene
import suppressions

if TYPE_CHECKING:
    from collections.abc import Iterator

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "tests" / "exceptions" / "registry.json"
# Build products and caches, never first-party sources. Pruned wherever they occur.
SKIPPED_DIRS = frozenset(
    {".git", ".cache", "out", "dist", ".venv", "__pycache__", ".mypy_cache", ".ruff_cache"}
)
CXX_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl"})
BUILD_SUFFIXES = frozenset({".cmake", ".ps1", ".sh"})
# Physical lines, blank and comment lines included. No per-file exceptions exist.
LINE_LIMITS = {"production": 300, "test": 400, "build": 200}
TEST_ROOTS = frozenset({"tests", "fuzz"})
MIN_EXPLANATION_LENGTH = 20
FIRST_PARTY_CXX_ROOTS = ("src", "tests", "fuzz", "tools")
TARGET_PATTERN = re.compile(r"\badd_(?:library|executable)\(\s*([\w${}]+)([^)]*)\)")


def source_kind(path: Path) -> str | None:
    """Classify a file as "cxx", "python" or "build", or None when it is not code."""
    name = path.name.removesuffix(".in")
    suffix = PurePosixPath(name).suffix
    if suffix in CXX_SUFFIXES:
        return "cxx"
    if suffix == ".py":
        return "python"
    if name == "CMakeLists.txt" or suffix in BUILD_SUFFIXES:
        return "build"
    return None


def iter_files(root: Path = ROOT) -> Iterator[Path]:
    """Yield every file below root, pruning build products and caches."""
    for directory, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIPPED_DIRS)
        for filename in sorted(filenames):
            yield Path(directory) / filename


def code_files(root: Path = ROOT) -> Iterator[tuple[Path, str, str]]:
    """Yield (path, repository-relative POSIX path, kind) for every code file."""
    for path in iter_files(root):
        kind = source_kind(path)
        if kind is not None:
            yield path, path.relative_to(root).as_posix(), kind


def line_limit(rel: str, kind: str) -> tuple[str, int]:
    """Return the size category and physical-line limit for a code file."""
    parts = PurePosixPath(rel).parts
    if kind == "build":
        category = "build"
    elif parts[0] in TEST_ROOTS or parts[-1].startswith("test_"):
        category = "test"
    else:
        category = "production"
    return category, LINE_LIMITS[category]


def god_file_errors(root: Path = ROOT) -> list[str]:
    """Report every code file above its category's line limit."""
    errors = []
    for path, rel, kind in code_files(root):
        count = len(path.read_text(encoding="utf-8").splitlines())
        category, limit = line_limit(rel, kind)
        if count > limit:
            errors.append(f"God file: {rel} has {count} lines ({category} limit {limit}); split it")
    return errors


def load_registry(path: Path = REGISTRY) -> tuple[dict[str, str], list[str]]:
    """Return the registered suppression keys with explanations, and schema errors."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return {}, [f"Cannot read suppression registry {path.name}: {exc}"]
    if not isinstance(data, dict) or set(data) != {"exceptions"}:
        return {}, ['Suppression registry must be {"exceptions": {...}} and nothing else']
    registry, errors = {}, []
    for key, entry in data["exceptions"].items():
        explanation = entry.get("explanation", "") if isinstance(entry, dict) else ""
        if not isinstance(entry, dict) or set(entry) != {"explanation"}:
            errors.append(f'Registry entry {key} must contain exactly one "explanation"')
        elif not isinstance(explanation, str) or len(explanation.strip()) < MIN_EXPLANATION_LENGTH:
            errors.append(f"Registry entry {key} needs a real explanation")
        registry[key] = str(explanation)
    return registry, errors


def suppression_errors(root: Path = ROOT, registry_path: Path = REGISTRY) -> list[str]:
    """Require every suppression to be registered, and every registration to be used."""
    registry, errors = load_registry(registry_path)
    found: set[str] = set()
    for path, rel, kind in code_files(root):
        result = suppressions.scan(path, rel, kind)
        errors.extend(result.errors)
        for suppression in result.suppressions:
            found.add(suppression.key)
            if suppression.key not in registry:
                errors.append(
                    f"Unregistered suppression {suppression.key} ({rel}:{suppression.line}); "
                    "register it with a reason"
                )
    errors.extend(
        f"Stale suppression registry entry: {key}" for key in sorted(set(registry) - found)
    )
    return errors


def cmake_files(root: Path = ROOT) -> dict[Path, str]:
    """Map each CMake file to its text."""
    return {
        path: path.read_text(encoding="utf-8")
        for path, _, kind in code_files(root)
        if kind == "build" and path.suffix not in {".ps1", ".sh"}
    }


def build_coverage_errors(root: Path = ROOT) -> list[str]:
    """Every first-party translation unit must be compiled (and thus linted) by some target."""
    files = cmake_files(root)
    everything = "\n".join(files.values())
    errors = []
    for path, rel, kind in code_files(root):
        if kind != "cxx" or path.suffix not in {".c", ".cc", ".cpp", ".cxx"}:
            continue
        if PurePosixPath(rel).parts[0] not in FIRST_PARTY_CXX_ROOTS:
            continue
        referenced = f"${{PROJECT_SOURCE_DIR}}/{rel}" in everything or any(
            path.is_relative_to(cmake.parent) and path.relative_to(cmake.parent).as_posix() in text
            for cmake, text in files.items()
        )
        if not referenced:
            errors.append(f"{rel} is not compiled by any CMake target, so it is never linted")
    return errors


def target_option_errors(root: Path = ROOT) -> list[str]:
    """Every first-party target must receive warnings and clang-tidy via de_apply_options()."""
    errors = []
    for path, text in cmake_files(root).items():
        rel = path.relative_to(root).as_posix()
        if PurePosixPath(rel).parts[0] not in FIRST_PARTY_CXX_ROOTS:
            continue
        for name, rest in TARGET_PATTERN.findall(text):
            if re.search(r"\b(?:ALIAS|IMPORTED|INTERFACE)\b", rest):
                continue
            if f"de_apply_options({name})" not in text:
                errors.append(f"{rel}: target {name} lacks de_apply_options({name})")
    return errors


def repository_files(root: Path = ROOT) -> list[tuple[Path, str]]:
    """Every file in the tree, with its repository-relative POSIX path."""
    return [
        (path, path.relative_to(root).as_posix()) for path in iter_files(root) if path.is_file()
    ]


def check_gates(root: Path = ROOT) -> list[str]:
    """Run every gate and return all errors."""
    return [
        *repo_hygiene.check(root, repository_files(root)),
        *god_file_errors(root),
        *suppression_errors(root, root / "tests" / "exceptions" / "registry.json"),
        *build_coverage_errors(root),
        *target_option_errors(root),
        *config_gates.check(
            root,
            list(iter_files(root)),
            [path for path, _, kind in code_files(root) if kind == "python"],
        ),
    ]


if __name__ == "__main__":
    failures = check_gates()
    print("\n".join(failures) if failures else "PASS: size, suppression, coverage and config gates")
    sys.exit(bool(failures))
