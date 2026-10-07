# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Central registry observations for effective named configuration exceptions and their scopes."""

from __future__ import annotations

import re
import tomllib
from typing import TYPE_CHECKING

import yaml

from suppressions import ScanResult, Suppression
from workflow_config import workflow

if TYPE_CHECKING:
    from pathlib import Path

RUFF_RULE = re.compile(r"[A-Z]+[0-9]{3,4}")


def scan_tidy(path: Path, rel: str) -> ScanResult:
    """Read actual YAML mappings; duplicate and merge keys cannot hide effective disabled checks."""
    config = workflow(path)
    text = path.read_text(encoding="utf-8")
    checks = config.get("Checks", "")
    if not isinstance(checks, str):
        return ScanResult([], [f"{rel}: Checks must be a string"])
    disabled = [entry.strip() for entry in checks.split(",") if entry.strip().startswith("-")]
    # The root's initial clear-and-select baseline is admission of the enabled check families.
    if rel == ".clang-tidy" and checks.lstrip().startswith("-*,"):
        disabled.remove("-*")
    admitted = (
        {
            "Checks",
            "WarningsAsErrors",
            "HeaderFilterRegex",
            "SystemHeaders",
            "FormatStyle",
            "CheckOptions",
        }
        if rel == ".clang-tidy"
        else {"Checks", "InheritParentConfig"}
    )
    errors = [
        f"{rel}: unreviewed clang-tidy configuration field: {name}"
        for name in config
        if name not in admitted
    ]
    found = []
    for entry in disabled:
        rule = entry.removeprefix("-")
        if any(character in rule for character in "*?[]"):
            errors.append(f"{rel}: wildcard clang-tidy configuration exception is forbidden")
        else:
            found.append(Suppression(rel, 1, f"clang-tidy/{rule}", text))
    options = config.get("CheckOptions", {})
    if not isinstance(options, dict):
        errors.append(f"{rel}: CheckOptions must be a mapping")
    else:
        for name, value in options.items():
            if not isinstance(name, str) or not isinstance(value, str) or not name.strip():
                errors.append(f"{rel}: CheckOptions must use named scalar settings")
            else:
                found.append(Suppression(rel, 1, f"clang-tidy-option/{name}", text))
    return ScanResult(found, errors)


def scan_ruff(path: Path, rel: str) -> ScanResult:
    """Each global or per-file ignored exact code binds its full effective configuration scope."""
    text = path.read_text(encoding="utf-8")
    config = tomllib.loads(text)
    lint = config.get("lint", {})
    found: list[Suppression] = []
    top_level = {
        "target-version",
        "line-length",
        "src",
        "exclude",
        "extend-exclude",
        "respect-gitignore",
        "format",
        "lint",
    }
    errors = [
        f"{rel}: unreviewed Ruff configuration field: {name}"
        for name in config
        if name not in top_level
    ]
    formatting = config.get("format", {})
    errors.extend(
        f"{rel}: unreviewed Ruff format field: {name}"
        for name in formatting
        if name not in {"quote-style", "docstring-code-format"}
    )
    if formatting and (
        formatting.get("quote-style") != "double"
        or formatting.get("docstring-code-format") is not True
    ):
        errors.append(f"{rel}: reviewed Ruff formatting settings must remain enabled")
    admitted = {
        "select",
        "ignore",
        "extend-ignore",
        "per-file-ignores",
        "extend-per-file-ignores",
        "isort",
        "flake8-tidy-imports",
        "mccabe",
        "pylint",
    }
    errors.extend(
        f"{rel}: unreviewed lint configuration field: {name}"
        for name in lint
        if name not in admitted
    )
    options = {
        "isort": {"section-order", "sections"},
        "flake8-tidy-imports": {"ban-relative-imports"},
        "mccabe": {"max-complexity"},
        "pylint": {"max-args", "max-branches", "max-returns", "max-statements"},
    }
    for section, keys in options.items():
        errors.extend(
            f"{rel}: unreviewed lint configuration field: {section}.{name}"
            for name in lint.get(section, {})
            if name not in keys
        )
    if config.get("format", {}).get("exclude"):
        errors.append(f"{rel}: format.exclude cannot hide admitted Python sources")
    scopes = [(name, lint.get(name, [])) for name in ("ignore", "extend-ignore")]
    for name in ("per-file-ignores", "extend-per-file-ignores"):
        scopes.extend((f"{name}/{pattern}", codes) for pattern, codes in lint.get(name, {}).items())
    for scope, codes in scopes:
        if not isinstance(codes, list) or not all(isinstance(code, str) for code in codes):
            errors.append(f"{rel}: ignored rules must be a list of exact codes")
            continue
        for code in codes:
            if not RUFF_RULE.fullmatch(code):
                errors.append(f"{rel}: Ruff exception must name an exact rule, not {code}")
            else:
                found.append(Suppression(rel, 1, f"ruff/{code}", f"{scope}\n{text}"))
    return ScanResult(found, errors)


def scan(path: Path, rel: str) -> ScanResult:
    """Configuration parsing failure is an admission failure, never an empty passing inventory."""
    try:
        return scan_tidy(path, rel) if path.name == ".clang-tidy" else scan_ruff(path, rel)
    except (ValueError, TypeError, KeyError, AttributeError, yaml.YAMLError) as error:
        return ScanResult([], [f"{rel}: invalid suppression configuration: {error}"])
