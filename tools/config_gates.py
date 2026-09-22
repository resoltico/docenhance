# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Configuration-level suppression gates.

Linters can be silenced from configuration as easily as from source. These checks keep every
such relaxation visible: each disabled clang-tidy check and each ignored Ruff code carries a
written reason in its file, nested configs cannot override the root ones, mypy stays strict
with no escape hatches, and no preset can turn warnings-as-errors or clang-tidy off.
"""

from __future__ import annotations

import configparser
import json
import re
import tomllib
from typing import TYPE_CHECKING, Any

import check_reference_suite

if TYPE_CHECKING:
    from pathlib import Path

ARTIFACT_EXCLUDES = frozenset({".cache", "out", "dist"})
MYPY_FORBIDDEN = frozenset(
    {
        "ignore_errors",
        "disable_error_code",
        "ignore_missing_imports",
        "follow_imports",
        "follow_untyped_imports",
        "exclude",
        "allow_untyped_defs",
        "allow_untyped_calls",
        "allow_incomplete_defs",
        "allow_any_generics",
        "allow_subclassing_any",
        "allow_untyped_decorators",
        "allow_untyped_globals",
        "implicit_reexport",
        "implicit_optional",
    }
)
ROOT_ONLY_CONFIGS = (
    "ruff.toml",
    ".ruff.toml",
    "pyproject.toml",
    "setup.cfg",
    ".mypy.ini",
    "mypy.ini",
)
GUARDED_CACHE_VARIABLES = ("DE_ENABLE_CLANG_TIDY", "DE_WARNINGS_AS_ERRORS")


def tidy_checks(text: str) -> list[str]:
    """Return the entries of a .clang-tidy Checks value (folded block or inline string)."""
    match = re.search(r"^Checks:[ \t]*(?P<inline>.*)$", text, re.MULTILINE)
    if match is None:
        return []
    value = match["inline"].strip()
    if value in {">", "|", ">-", "|-"}:
        block = []
        for line in text[match.end() :].splitlines()[1:]:
            if line and not line[0].isspace():
                break
            block.append(line)
        value = " ".join(block)
    return [entry for entry in re.split(r"[,\s]+", value.strip("'\"")) if entry]


def clang_tidy_errors(root: Path, path: Path) -> list[str]:
    """A disabled check needs a written reason; nested configs may only disable, never relax."""
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(root).as_posix()
    comments = "\n".join(line for line in text.splitlines() if line.lstrip().startswith("#"))
    checks = tidy_checks(text)
    is_root = path.parent == root
    # The root may start from "-*" to opt in explicitly; every other "-" entry needs a reason.
    disabled = [e for i, e in enumerate(checks) if e.startswith("-") and not (is_root and i == 0)]
    errors = [
        f"{rel}: {entry} is disabled without a documented reason in the file"
        for entry in disabled
        if entry == "-*" or entry.removeprefix("-") not in comments
    ]
    if not re.search(r"^WarningsAsErrors:\s*'\*'\s*$", text, re.MULTILINE) and is_root:
        errors.append(f"{rel}: the root configuration must set WarningsAsErrors: '*'")
    if not is_root:
        if not re.search(r"^InheritParentConfig:\s*true\s*$", text, re.MULTILINE):
            errors.append(f"{rel}: nested configurations must inherit the root configuration")
        errors.extend(
            f"{rel}: nested configurations may not set {key}"
            for key in ("WarningsAsErrors", "CheckOptions", "HeaderFilterRegex", "SystemHeaders")
            if re.search(rf"^{key}:", text, re.MULTILINE)
        )
    return errors


def ruff_errors(root: Path) -> list[str]:
    """Ruff selects ALL; each ignored code has a same-line reason; nothing else is excluded."""
    path = root / "ruff.toml"
    if not path.is_file():
        return ["ruff.toml is missing"]
    text = path.read_text(encoding="utf-8")
    config: dict[str, Any] = tomllib.loads(text)
    lint = config.get("lint", {})
    errors = []
    if lint.get("select") != ["ALL"]:
        errors.append('ruff.toml: lint.select must be exactly ["ALL"]')
    errors.extend(
        f"ruff.toml: {key} is not allowed; only artifact directories are excluded"
        for key in ("exclude", "force-exclude", "respect-gitignore")
        if key in config or key in lint
    )
    if set(config.get("extend-exclude", [])) != ARTIFACT_EXCLUDES:
        errors.append(f"ruff.toml: extend-exclude must be exactly {sorted(ARTIFACT_EXCLUDES)}")
    ignored = [*lint.get("ignore", []), *lint.get("extend-ignore", [])]
    for codes in lint.get("per-file-ignores", {}).values():
        ignored.extend(codes)
    errors.extend(
        f"ruff.toml: ignored {code} needs a same-line reason comment"
        for code in ignored
        if code == "ALL" or not re.search(rf'"{re.escape(code)}",?\s*#\s*\S', text)
    )
    return errors


def mypy_errors(root: Path, python_files: list[Path]) -> list[str]:
    """Require strict mypy over every Python file, with no per-module or global escape hatch."""
    parser = configparser.ConfigParser()
    if not parser.read(root / "mypy.ini", encoding="utf-8"):
        return ["mypy.ini is missing"]
    errors = []
    if parser.sections() != ["mypy"]:
        errors.append("mypy.ini: only the [mypy] section is allowed (no per-module overrides)")
    section = parser["mypy"] if parser.has_section("mypy") else {}
    if str(section.get("strict", "")).strip() != "True":
        errors.append("mypy.ini: strict = True is required")
    errors.extend(
        f"mypy.ini: {key} is not allowed" for key in sorted(MYPY_FORBIDDEN & set(section))
    )
    targets = [root / t.strip() for t in str(section.get("files", "")).split(",") if t.strip()]
    errors.extend(
        f"{path.relative_to(root).as_posix()} is outside mypy.ini files"
        for path in python_files
        if not any(path.is_relative_to(target) for target in targets)
    )
    return errors


def nested_config_errors(root: Path, files: list[Path]) -> list[str]:
    """Tool configurations live at the root only, so a subdirectory cannot relax them."""
    errors = []
    for path in files:
        rel = path.relative_to(root).as_posix()
        nested = path.parent != root
        if path.name in ROOT_ONLY_CONFIGS and (
            nested or path.name not in {"ruff.toml", "mypy.ini"}
        ):
            errors.append(f"{rel}: lint configuration must be the root ruff.toml and mypy.ini only")
        if path.name == ".clang-format":
            text = path.read_text(encoding="utf-8")
            if nested or re.search(
                r"^\s*DisableFormat:\s*true", text, re.MULTILINE | re.IGNORECASE
            ):
                errors.append(f"{rel}: formatting may not be disabled or overridden")
    return errors


def preset_errors(root: Path) -> list[str]:
    """No shared preset may disable clang-tidy or warnings-as-errors; the base enables both."""
    errors = []
    for path in [root / "CMakePresets.json", *sorted((root / "cmake" / "presets").glob("*.json"))]:
        presets = json.loads(path.read_text(encoding="utf-8")).get("configurePresets", [])
        for preset in presets:
            variables = preset.get("cacheVariables", {})
            for name in GUARDED_CACHE_VARIABLES:
                value = variables.get(name, True)
                if name in variables and value is not True:
                    errors.append(f"{path.name}: preset {preset['name']} sets {name} to {value!r}")
                elif preset.get("name") == "base" and name not in variables:
                    errors.append(f"{path.name}: the base preset must set {name} to true")
    return errors


def warning_sync_errors(root: Path) -> list[str]:
    """The reference-suite flags and the CMake warning set must be the same list."""
    text = (root / "cmake" / "ProjectOptions.cmake").read_text(encoding="utf-8")
    match = re.search(r"set\(DE_STRICT_WARNINGS([^)]*)\)", text)
    cmake_flags = match.group(1).split() if match else []
    if cmake_flags != check_reference_suite.STRICT_WARNINGS:
        return ["DE_STRICT_WARNINGS and check_reference_suite.STRICT_WARNINGS differ"]
    return []


def check(root: Path, files: list[Path], python_files: list[Path]) -> list[str]:
    """Run every configuration gate over the repository's files."""
    errors = [*nested_config_errors(root, files), *ruff_errors(root), *preset_errors(root)]
    for path in files:
        if path.name == ".clang-tidy":
            errors.extend(clang_tidy_errors(root, path))
    errors.extend(mypy_errors(root, python_files))
    errors.extend(warning_sync_errors(root))
    return errors
