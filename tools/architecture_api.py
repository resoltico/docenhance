# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Evaluate manifest-owned API permissions on parsed first-party C++ syntax."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

from architecture import ArchitectureError, Manifest
from architecture_matches import commands, violations
from deps import ROOT

ERROR_LINE = re.compile(r"^(?:.*: )?(?:fatal )?error: ", re.MULTILINE)


def matchers(manifest: Manifest, files: dict[str, set[str]]) -> list[tuple[str, str]]:
    """The abstract-syntax-tree rules to evaluate on a translation unit, as (rule, matcher)."""

    def location(paths: set[str]) -> str:
        return '"^(' + "|".join(re.escape(path) for path in sorted(paths)) + ')$"'

    all_files = set().union(*files.values())
    rules = [
        (
            "throws instead of returning a Result",
            f"cxxThrowExpr(isExpansionInFileMatching({location(all_files)}))",
        )
    ]
    for name in sorted(files):
        owned = location(files[name])
        names = '", "'.join(sorted(manifest.forbidden(name, "calls")))
        rules.append(
            (
                f"{name} calls or references a forbidden function",
                (
                    f'declRefExpr(to(functionDecl(hasAnyName("{names}"))), '
                    f"isExpansionInFileMatching({owned}))"
                ),
            )
        )
        if not manifest.layers[name].get("may_thread"):
            types = '", "'.join(manifest.restrictions["thread_types"])
            calls = '", "'.join(manifest.restrictions["thread_calls"])
            rules += [
                (
                    f"{name} constructs a thread outside the scheduler",
                    (
                        f"cxxConstructExpr(hasDeclaration(cxxConstructorDecl(ofClass("
                        f'cxxRecordDecl(hasAnyName("{types}"))))), '
                        f"isExpansionInFileMatching({owned}))"
                    ),
                ),
                (
                    f"{name} references a thread-launch API outside the scheduler",
                    (
                        f'declRefExpr(to(functionDecl(hasAnyName("{calls}"))), '
                        f"isExpansionInFileMatching({owned}))"
                    ),
                ),
            ]
        rules.append(
            (
                f"{name} declares a namespace that is not docenhance::{name}",
                (
                    f"namespaceDecl(isExpansionInFileMatching({owned}), "
                    f'unless(isAnonymous()), unless(hasName("docenhance")), '
                    f'unless(hasName("{name}")))'
                ),
            )
        )
        if not manifest.layers[name].get("may_allocate"):
            rules.append(
                (
                    f"{name} allocates directly instead of taking memory from a budget",
                    (
                        "expr(anyOf(cxxNewExpr(), cxxDeleteExpr(), "
                        'callExpr(callee(functionDecl(hasAnyName("operator new", "operator new[]", '
                        '"operator delete", "operator delete[]"))))), '
                        f"isExpansionInFileMatching({owned}))"
                    ),
                )
            )
        if not manifest.layers[name].get("may_catch"):
            rules.append(
                (
                    f"{name} catches an exception outside an authorized containment boundary",
                    f"cxxCatchStmt(isExpansionInFileMatching({owned}))",
                )
            )
    return rules


def api_violations(
    clang_query: str, build: Path, entry: dict[str, str], rules: list[tuple[str, str]]
) -> list[str]:
    """Call, throw and catch rules for one translation unit, on its abstract syntax tree."""
    query_commands = commands(rules)
    result = subprocess.run(
        # clang-query parses GCC's compile commands too, and those carry GCC-only warning options.
        # With -Werror an option clang does not know is an error, and a translation unit that fails
        # to parse answers every rule with no matches, so this diagnostic is disabled for the parse
        # exactly as cmake/ProjectOptions.cmake disables it for clang-tidy.
        [
            clang_query,
            "-p",
            str(build),
            "--extra-arg=-Wno-unknown-warning-option",
            entry["file"],
            *query_commands,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    diagnostics = f"{result.stdout}\n{result.stderr}"
    if result.returncode != 0 or ERROR_LINE.search(diagnostics):
        # A rule cannot be trusted on a translation unit clang could not parse.
        msg = f"clang-query failed on {entry['file']}: {diagnostics.strip()[:400]}"
        raise ArchitectureError(msg)
    return violations(entry["file"], rules, result.stdout)


def find_clang_query() -> str:
    """Locate a clang-query of the pinned major version, preferring the pinned LLVM directory."""
    major = json.loads((ROOT / "deps/tools.json").read_text(encoding="utf-8"))["clang_tidy"][
        "minimum"
    ]
    hint = os.environ.get("DE_CLANG_TIDY_DIR")
    names = [f"clang-query-{major}", "clang-query"]
    # Keep this lookup aligned with cmake/ProjectOptions.cmake.  Homebrew keeps
    # versioned LLVM keg-only, so its clang-query is intentionally absent from
    # PATH even after tools/install_llvm.py installs the pinned formula.
    directories = [
        hint,
        f"/opt/homebrew/opt/llvm@{major}/bin",
        f"/usr/local/opt/llvm@{major}/bin",
        "/opt/homebrew/opt/llvm/bin",
        "/usr/local/opt/llvm/bin",
    ]
    candidates = [f"{directory}/{name}" for directory in directories if directory for name in names]
    candidates += names
    for candidate in candidates:
        found = shutil.which(candidate)
        if found is None:
            continue
        version = subprocess.run(
            [found, "--version"], capture_output=True, text=True, check=False
        ).stdout
        if re.search(rf"version {major}\.", version):
            return found
    msg = (
        f"No clang-query {major}.x found; matchers depend on the release, so install the pinned"
        " LLVM (tools/install_llvm.py)"
    )
    raise ArchitectureError(msg)
