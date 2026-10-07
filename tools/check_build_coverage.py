#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Prove complete native translation-unit coverage from CMake's compilation database."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from check_gates import code_files
from ninja_lint_coverage import entry_path, lint_errors

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = frozenset({"src", "tests", "fuzz", "tools"})
TRANSLATION_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx"})


def source_coverage_errors(root: Path, build: Path) -> list[str]:
    """Reject a missing compiler entry for any first-party translation unit."""
    expected = {
        path.resolve(): rel
        for path, rel, kind in code_files(root)
        if kind == "cxx"
        and path.suffix in TRANSLATION_SUFFIXES
        and Path(rel).parts[0] in SOURCE_ROOTS
    }
    database = build / "compile_commands.json"
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        return [f"Cannot read compiler coverage at {database}: {exc}"]
    if not isinstance(entries, list) or not entries:
        return [f"Compiler coverage database is not a nonempty array: {database}"]
    observed = set()
    for entry in entries:
        if not isinstance(entry, dict) or not all(
            isinstance(entry.get(key), str) and entry[key]
            for key in ("file", "directory", "command")
        ):
            return [f"Invalid compiler coverage entry at {database}"]
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        observed.add(source.resolve())
    return [
        f"{rel} has no real compiler entry in {database}"
        for path, rel in sorted(expected.items(), key=lambda item: item[1])
        if path not in observed
    ]


def coverage_errors(root: Path, build: Path) -> list[str]:
    """Prove source inventory and each actual first-party compiler/linter instance."""
    failures = source_coverage_errors(root, build)
    if failures:
        return failures
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    selected = []
    for entry in entries:
        source = entry_path(entry, "file")
        if not source.is_relative_to(root.resolve()):
            continue
        rel = source.relative_to(root.resolve())
        if rel.parts[0] not in SOURCE_ROOTS:
            continue
        if source.suffix not in TRANSLATION_SUFFIXES:
            failures.append(f"{rel}: compiled TU extension has no admitted source-file gates")
        elif not isinstance(entry.get("output"), str) or not entry["output"]:
            failures.append(f"{rel}: compiler instance lacks output identity")
        else:
            selected.append(entry)
    return failures + lint_errors(build, selected)


def main() -> int:
    """Check the admitted configured application tree."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    failures = coverage_errors(ROOT, args.build.resolve())
    print(
        "\n".join(failures) if failures else "PASS: every first-party TU has a real compiler entry"
    )
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
