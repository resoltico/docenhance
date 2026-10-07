#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Quality gates: anti-god files, the suppression registry, and source configuration.

Nothing is grandfathered. Size limits have no waiver mechanism at all. Every in-source
suppression must name its rules and be registered with an explanation; registry entries that no
longer match a suppression fail too. Configuration-level relaxations are checked by config_gates.
"""

from __future__ import annotations

import json
import os
import re
import sys
from collections import Counter
from pathlib import Path, PurePosixPath
from typing import TYPE_CHECKING

import config_gates
import config_suppressions
import package_source
import repo_hygiene
import suppressions

if TYPE_CHECKING:
    from collections.abc import Iterator

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "tests" / "exceptions" / "registry.json"
CXX_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl"})
BUILD_SUFFIXES = frozenset({".cmake", ".ps1", ".sh"})
# Physical lines, blank and comment lines included. No per-file exceptions exist.
LINE_LIMITS = {"production": 300, "test": 400, "build": 200}
TEST_ROOTS = frozenset({"tests", "fuzz"})
MIN_EXPLANATION_LENGTH = 20


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


def nested_checkout(directory: Path) -> bool:
    """Whether this directory is a repository of its own: a clone or a linked worktree."""
    return (directory / ".git").exists()


def iter_files(root: Path = ROOT) -> Iterator[Path]:
    """Yield every file of this checkout below root.

    Build products and caches are pruned by name. A directory that carries its own .git is a
    separate checkout — a nested clone, or a worktree placed inside the tree — and its files
    belong to that checkout, not this one.
    """
    for directory, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            name
            for name in dirnames
            if name not in package_source.EXCLUDED
            and not (Path(directory) == root and name in package_source.CHECKOUT_ARTIFACTS)
            and not nested_checkout(Path(directory) / name)
        )
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
    elif parts[0] in TEST_ROOTS:
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


def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    """Reject duplicate JSON keys at every registry depth instead of overwriting evidence."""
    result = {}
    for key, value in pairs:
        if key in result:
            msg = f"Duplicate suppression registry key: {key}"
            raise ValueError(msg)
        result[key] = value
    return result


def load_registry(path: Path = REGISTRY) -> tuple[dict[str, int], list[str]]:
    """Admit exact bindings with a declared site count; explanations require human review."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (OSError, ValueError) as exc:
        return {}, [f"Cannot read suppression registry {path.name}: {exc}"]
    if (
        not isinstance(data, dict)
        or set(data) != {"exceptions"}
        or not isinstance(data["exceptions"], dict)
    ):
        return {}, ['Suppression registry must be {"exceptions": {...}} and nothing else']
    registry, errors = {}, []
    for key, entry in data["exceptions"].items():
        if not re.fullmatch(r"[^:\\\n]+:[a-z-]+/[^@\n]+@[0-9a-f]{64}", key):
            errors.append(f"Registry entry {key} has an invalid exact SHA256 binding")
        elif any(
            part in {"", ".", ".."} for part in key.split(":", 1)[0].split("/")
        ) or key.startswith("/"):
            errors.append(f"Registry entry {key} must name a checkout-relative path")
        if not isinstance(entry, dict) or set(entry) != {"explanation", "occurrences"}:
            errors.append(f'Registry entry {key} requires only "explanation" and "occurrences"')
            continue
        explanation, count = entry["explanation"], entry["occurrences"]
        if not isinstance(explanation, str) or len(explanation.strip()) < MIN_EXPLANATION_LENGTH:
            errors.append(f"Registry entry {key} needs an explanation for review")
        if type(count) is not int or count < 1:
            errors.append(f"Registry entry {key} requires a positive integer occurrence count")
        else:
            registry[key] = count
    return registry, errors


def suppression_errors(root: Path = ROOT, registry_path: Path = REGISTRY) -> list[str]:
    """Every source/config exception binds exact text and exactly its declared occurrences."""
    registry, errors = load_registry(registry_path)
    found: Counter[str] = Counter()
    for path in iter_files(root):
        rel = path.relative_to(root).as_posix()
        kind = source_kind(path)
        if path.name in {".clang-tidy", "ruff.toml", ".ruff.toml"}:
            result = config_suppressions.scan(path, rel)
        elif kind is not None:
            result = suppressions.scan(path, rel, kind)
        else:
            continue
        errors.extend(result.errors)
        for suppression in result.suppressions:
            found[suppression.key] += 1
            if suppression.key not in registry:
                errors.append(
                    f"Unregistered suppression {suppression.key} ({rel}:{suppression.line}); "
                    "register it with a reason"
                )
    errors.extend(
        f"Stale suppression registry entry: {key}" for key in sorted(set(registry) - set(found))
    )
    errors.extend(
        f"Suppression occurrence count differs: {key} declares {count}, found {found[key]}"
        for key, count in registry.items()
        if key in found and found[key] != count
    )
    return errors


# Catch2 builds these from an out-of-range ResultDisposition::Flags combination, which clang-tidy's
# analyzer rejects with libc++ and MSVC but not libstdc++; write CHECK(!expr) instead.
NEGATED_ASSERTION = re.compile(r"\b(?:CHECK|REQUIRE)_FALSE\s*\(")


def negated_assertion_errors(root: Path = ROOT) -> list[str]:
    """Reject Catch2 negated-assertion macros in first-party C++ tests."""
    errors = []
    for path, rel, kind in code_files(root):
        if kind != "cxx" or PurePosixPath(rel).parts[0] not in TEST_ROOTS:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if NEGATED_ASSERTION.search(line):
                errors.append(
                    f"{rel}:{number}: use CHECK(!expr)/REQUIRE(!expr); Catch2's _FALSE macros "
                    "fail clang-tidy EnumCastOutOfRange on libc++ and MSVC"
                )
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
        *negated_assertion_errors(root),
        *config_gates.check(
            root,
            list(iter_files(root)),
            [path for path, _, kind in code_files(root) if kind == "python"],
        ),
    ]


if __name__ == "__main__":
    failures = check_gates()
    print("\n".join(failures) if failures else "PASS: size, suppression and config gates")
    sys.exit(bool(failures))
