#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Offline structural and contract checks. Does not claim a native build succeeded."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from typing import Any

from check_all import CHECKS
from check_gates import code_files, iter_files
from deps import ROOT, load_lock
from project_version import VersionError, project_version

PRESET_SCHEMA = 12
MIN_CLANG_TOOLS_MAJOR = 23
CXX_STANDARD = 23
SPDX_WINDOW = 300
# Machine-readable ownership on every first-party file (REUSE/SPDX).
COPYRIGHT = "SPDX-FileCopyrightText: 2026 Ervins Strauhmanis"
LICENSE_TAG = "SPDX-License-Identifier: MIT"
REQUIRED_TIDY_SETS = ("bugprone-*", "clang-analyzer-*", "cppcoreguidelines-*", "misc-*")
PINNED_LINTERS = ("clang_tidy", "clang_format", "ruff", "mypy")
# GitHub runs the full native quality workflow and a lighter source-archive preflight.
SOURCE_WORKFLOW = ".github/workflows/source.yml"
REQUIRED_SOURCE_COMMANDS = (
    "python tools/check_project.py",
    "python tools/check_gates.py",
    "python tools/check_format.py",
    "python -m ruff format --check",
    "python -m ruff check --output-format github",
    "python -m mypy",
    "python -m unittest discover -s tests/tooling -v",
    "python tools/package_source.py",
)
REQUIRED_CI_COMMANDS = ("cmake --workflow --preset release", "cmake --workflow --preset fuzz")
SANITIZED_TEST_PRESETS = ("sanitize", "tsan", "fuzz", "fuzz-afl")


def read_json(relative: str) -> dict[str, Any]:
    """Parse a repository JSON file whose top level is an object."""
    data: dict[str, Any] = json.loads((ROOT / relative).read_text(encoding="utf-8"))
    return data


def json_errors() -> list[str]:
    """Every JSON file outside build products must parse."""
    errors = []
    for path in iter_files(ROOT):
        if path.suffix != ".json":
            continue
        try:
            json.loads(path.read_text(encoding="utf-8"))
        except (ValueError, OSError) as exc:
            errors.append(f"{path.relative_to(ROOT)}: {exc}")
    return errors


def lock_errors() -> list[str]:
    """The lock is fully pinned and its dependency set matches the feature policy."""
    try:
        lock = load_lock(ROOT / "deps/lock.json")
        features = read_json("deps/features.json")["dependencies"]
    except (KeyError, ValueError) as exc:
        return [str(exc)]
    if set(features) != {d["name"] for d in lock["dependencies"]}:
        return ["Feature and lock dependency sets differ"]
    return []


def metadata_errors() -> list[str]:
    """The declared version and preset schema, and an SPDX header on every code file."""
    errors = []
    try:
        project_version()
    except (OSError, VersionError) as exc:
        errors.append(str(exc))
    if read_json("CMakePresets.json")["version"] != PRESET_SCHEMA:
        errors.append("Expected CMake 4.4 preset schema 12")
    for path, rel, _ in code_files(ROOT):
        head = path.read_text(encoding="utf-8")[:SPDX_WINDOW]
        errors.extend(
            f"Missing {tag.split(':')[0]} header: {rel}"
            for tag in (LICENSE_TAG, COPYRIGHT)
            if tag not in head
        )
    return errors


def link_errors() -> list[str]:
    """Concrete repository-relative Markdown links, excluding URLs and heading anchors."""
    errors = []
    for path in [*ROOT.glob("*.md"), *(ROOT / "docs").rglob("*.md")]:
        for link in re.findall(r"\]\(([^)\s]+)\)", path.read_text(encoding="utf-8")):
            target = link.split("#", 1)[0]
            external = "://" in link or link.startswith(("#", "mailto:"))
            if not external and target and not (path.parent / target).exists():
                errors.append(f"Broken local link: {path.relative_to(ROOT)} -> {link}")
    return errors


def toolchain_errors() -> list[str]:
    """clang-tidy keeps its required check sets; every linter is pinned in deps/tools.json."""
    errors = []
    tidy = (ROOT / ".clang-tidy").read_text(encoding="utf-8")
    if "WarningsAsErrors: '*'" not in tidy:
        errors.append("clang-tidy must treat every enabled diagnostic as an error")
    errors.extend(
        f"clang-tidy is missing required check set: {required}"
        for required in REQUIRED_TIDY_SETS
        if required not in tidy
    )
    if "-misc-include-cleaner" in tidy:
        errors.append("clang-tidy must keep misc-include-cleaner enabled")
    tools = read_json("deps/tools.json")
    errors.extend(
        f"deps/tools.json must pin {name} 23 or newer"
        for name in ("clang_tidy", "clang_format")
        if int(str(tools.get(name, {}).get("minimum", "0")).split(".")[0]) < MIN_CLANG_TOOLS_MAJOR
    )
    errors.extend(
        f"deps/tools.json must pin {name}"
        for name in PINNED_LINTERS
        if not tools.get(name, {}).get("version")
    )
    if tools.get("cxx", {}).get("standard") != CXX_STANDARD:
        errors.append("deps/tools.json C++ standard must match cxx_std_23")
    return errors


def wiring_errors() -> list[str]:
    """The hooks and CI retain the full gate; source packaging has a lighter preflight."""
    hooks = (ROOT / ".pre-commit-config.yaml").read_text(encoding="utf-8")
    source = (ROOT / SOURCE_WORKFLOW).read_text(encoding="utf-8")
    ci = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    errors = []
    if "tools/check_all.py" not in hooks:
        errors.append("pre-commit does not run tools/check_all.py")
    errors.extend(
        f"Source workflow does not run {command}"
        for command in REQUIRED_SOURCE_COMMANDS
        if command not in source
    )
    for name, arguments in CHECKS:
        command = " ".join(arguments).replace("-m ", "")
        if command not in ci:
            errors.append(f"CI does not run the {name} check ({command})")
    errors.extend(
        f"CI does not run {command}" for command in REQUIRED_CI_COMMANDS if command not in ci
    )
    return errors


def fuzz_errors() -> list[str]:
    """Harnesses, settings, engine targets, replay tests, corpora and sanitizer options agree."""
    settings = read_json("fuzz/targets.json")
    configured = set(settings["targets"])
    harnesses = {path.stem for path in (ROOT / "fuzz").glob("*.cpp")}
    fuzz_cmake = (ROOT / "fuzz/CMakeLists.txt").read_text(encoding="utf-8")
    tests_cmake = (ROOT / "tests/CMakeLists.txt").read_text(encoding="utf-8")
    built = set(re.findall(r"^de_fuzz_target\((\w+)", fuzz_cmake, flags=re.MULTILINE))
    replayed = set(re.findall(r"^de_fuzz_replay\((\w+)", tests_cmake, flags=re.MULTILINE))
    errors = []
    if not configured == harnesses == built == replayed:
        errors.append(
            f"Fuzz targets differ: targets.json {sorted(configured)}, "
            f"harnesses {sorted(harnesses)}, engine targets {sorted(built)}, "
            f"replay tests {sorted(replayed)}"
        )
    for target in sorted(configured):
        for kind in ("corpus", "regressions"):
            directory = ROOT / "fuzz" / kind / target
            if not directory.is_dir() or not any(p.is_file() for p in directory.iterdir()):
                errors.append(f"fuzz/{kind}/{target} must contain at least one input")
    presets = {p["name"]: p for p in read_json("CMakePresets.json")["testPresets"]}
    expected = settings["sanitizer_options"]
    errors.extend(
        f"Test preset {name} must set the sanitizer options of fuzz/targets.json"
        for name in SANITIZED_TEST_PRESETS
        if presets.get(name, {}).get("environment") != expected
    )
    return errors


def workflow_errors() -> list[str]:
    """External Actions are immutable commits; local composite actions are exempt."""
    errors: list[str] = []
    for path in (ROOT / ".github/workflows").glob("*.yml"):
        text = path.read_text(encoding="utf-8")
        actions = re.findall(r"^\s*-?\s*uses:\s*([^\s#]+)", text, flags=re.MULTILINE)
        errors.extend(
            f"Unpinned action in {path.name}: {action}"
            for action in actions
            if not action.startswith("./") and not re.fullmatch(r"[^@]+@[0-9a-f]{40}", action)
        )
        if "pull_request_target:" in text:
            errors.append("Privileged pull-request trigger prohibited")
    return errors


def check() -> list[str]:
    """Run every structural check."""
    errors = [
        *json_errors(),
        *lock_errors(),
        *metadata_errors(),
        *link_errors(),
        *toolchain_errors(),
        *wiring_errors(),
        *workflow_errors(),
        *fuzz_errors(),
    ]
    spec = subprocess.run(
        [sys.executable, str(ROOT / "tools/generate_spec.py"), "--check"], check=False
    )
    if spec.returncode:
        errors.append("Stale generated contract")
    return errors


if __name__ == "__main__":
    failures = check()
    print("\n".join(failures) if failures else "PASS: project structure, lock, contract and wiring")
    raise SystemExit(bool(failures))
