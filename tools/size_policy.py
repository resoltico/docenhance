# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Protect reviewed function bounds from configuration-level disabling or relaxation."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING, Any

import yaml

if TYPE_CHECKING:
    from pathlib import Path

TIDY_BOUNDS = {
    "readability-function-cognitive-complexity.Threshold": "20",
    "readability-function-cognitive-complexity.IgnoreMacros": "true",
    "readability-function-size.LineThreshold": "80",
    "readability-function-size.StatementThreshold": "60",
    "readability-function-size.BranchThreshold": "15",
    "readability-function-size.ParameterThreshold": "4294967295",
    "readability-function-size.NestingThreshold": "4",
    "readability-function-size.VariableThreshold": "20",
}
TIDY_BOUNDS.update(
    {
        key.replace("readability-function-size.", "google-readability-function-size."): "5"
        if key.endswith("ParameterThreshold")
        else "4294967295"
        for key in list(TIDY_BOUNDS)
        if key.startswith("readability-function-size.")
    }
)
TIDY_CHECKS = (
    "readability-function-size",
    "readability-function-cognitive-complexity",
    "google-readability-function-size",
)
RUFF_BOUNDS = {
    "mccabe": {"max-complexity": 10},
    "pylint": {"max-args": 5, "max-branches": 12, "max-returns": 6, "max-statements": 50},
}
HEADER_FILTER = r".*[/\\](include[/\\]docenhance|src|tests|fuzz|tools)[/\\].*"
RUFF_CHECKS = ("C901", "PLR0911", "PLR0912", "PLR0913", "PLR0915")


def enabled(check: str, checks: str, *, inherited: bool) -> bool:
    """Evaluate clang-tidy's ordered enabling/disabling glob list for one check."""
    active = inherited
    for entry in filter(None, (part.strip() for part in checks.split(","))):
        if re.fullmatch(re.escape(entry.removeprefix("-")).replace(r"\*", ".*"), check):
            active = not entry.startswith("-")
    return active


def clang_tidy_errors(root: Path, path: Path) -> list[str]:
    """Root budgets stay reviewed; nested configurations cannot disable the guarded checks."""
    rel = path.relative_to(root).as_posix()
    try:
        config = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as error:
        return [f"{rel}: invalid clang-tidy configuration: {error}"]
    if not isinstance(config, dict) or not isinstance(config.get("Checks"), str):
        return [f"{rel}: clang-tidy configuration needs a Checks string"]
    is_root = path.parent == root
    errors = [
        f"{rel}: {check} must remain enabled"
        for check in TIDY_CHECKS
        if not enabled(check, config["Checks"], inherited=not is_root)
    ]
    if is_root:
        if config.get("HeaderFilterRegex") != HEADER_FILTER or config.get(
            "ExcludeHeaderFilterRegex"
        ):
            errors.append(f"{rel}: header filtering must preserve all first-party code")
        options = config.get("CheckOptions", {})
        if not isinstance(options, dict):
            return [*errors, f"{rel}: CheckOptions must be a mapping"]
        errors.extend(
            f"{rel}: {key} must remain {value}"
            for key, value in TIDY_BOUNDS.items()
            if str(options.get(key, "")).lower() != value
        )
    return errors


def ruff_errors(config: dict[str, Any]) -> list[str]:
    """Keep reviewed Python bounds and prohibit ignoring their checks, including prefixes."""
    lint = config.get("lint", {})
    errors = [
        f"ruff.toml: lint.{section}.{key} must remain {value}"
        for section, bounds in RUFF_BOUNDS.items()
        for key, value in bounds.items()
        if lint.get(section, {}).get(key) != value
    ]
    ignored = [*lint.get("ignore", []), *lint.get("extend-ignore", [])]
    for codes in lint.get("per-file-ignores", {}).values():
        ignored.extend(codes)
    for codes in lint.get("extend-per-file-ignores", {}).values():
        ignored.extend(codes)
    errors.extend(
        f"ruff.toml: {code} cannot ignore size or complexity checks"
        for code in sorted(set(ignored))
        if code == "ALL" or any(check.startswith(code) for check in RUFF_CHECKS)
    )
    return errors
