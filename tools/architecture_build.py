# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""The architecture rules that are checked against a configured build.

A checker that greps source text is easy to fool: a comment, a string, a macro, a type alias or a
qualified name all read the same to a regular expression. These rules ask the build itself instead,
through the compilation database it writes:

The build is read through tools/compile_db.py.

* **Reach rules** run on the transitive include graph the compiler reports (`-M`), so a layer or a
  third-party package reached through three other headers counts exactly as much as a direct one.
* **API rules** run on the real abstract syntax tree through `clang-query`, so a banned call, a
  `throw` or a `catch` is found through any alias, macro or namespace qualification.
* **Link rules** hold the links the build declares against the includes the code writes, so a
  target links what it uses and uses what it links.
* **Self-containment** compiles every public header on its own, and twice, so no header depends on
  what its includer happened to include first.

The rules that need no build live in tools/architecture.py. See docs/architecture.md.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from architecture import INCLUDE_DIRECTIVE, ArchitectureError, Manifest
from compile_db import (
    compilation_database,
    compiler_arguments,
    included_headers,
    layer_of,
    package_roots,
    run_compiler,
)
from deps import ROOT

FIRST_PARTY = "/(src|include/docenhance)"
SUMMARY = re.compile(r"^(\d+) match(?:es)?\.$", re.MULTILINE)
ERROR_LINE = re.compile(r"^(?:.*: )?error: ", re.MULTILINE)


def reach_violations(
    manifest: Manifest, entry: dict[str, str], build: Path
) -> tuple[list[str], set[str]]:
    """Layer and package reach for one translation unit, and the layers it contains."""
    source, errors = entry["file"], []
    layer = layer_of(manifest, source) or ""
    allowed, packages = manifest.reach(layer) | {layer}, manifest.package_reach(layer)
    roots, present = package_roots(entry, build), {layer}
    for header in included_headers(entry):
        reached = layer_of(manifest, header)
        if reached is not None:
            present.add(reached)
            if reached not in allowed:
                errors.append(f"{source}: {layer} reaches {reached} through {header}")
            continue
        root = next((root for root in roots if header.startswith(f"{root}/")), None)
        if root is None:
            continue
        relative = header[len(root) + 1 :]
        package = manifest.package_of_header(relative)
        if package is None:
            errors.append(f"{source}: {relative} belongs to no declared package")
        elif package not in packages:
            errors.append(f"{source}: {layer} reaches the {package} package through {relative}")
    return errors, present


def matchers(manifest: Manifest, present: set[str]) -> list[tuple[str, str]]:
    """The abstract-syntax-tree rules to evaluate on a translation unit, as (rule, matcher)."""
    rules = [
        (
            "throws instead of returning a Result",
            f'cxxThrowExpr(isExpansionInFileMatching("{FIRST_PARTY}/"))',
        )
    ]
    for name in sorted(present):
        location = f"{FIRST_PARTY}/{name}/"
        names = '", "'.join(manifest.layers[name]["forbidden_calls"])
        rules.append(
            (
                f"{name} calls a forbidden function",
                (
                    f'callExpr(callee(functionDecl(hasAnyName("{names}"))), '
                    f'isExpansionInFileMatching("{location}"))'
                ),
            )
        )
        rules.append(
            (
                f"{name} declares a namespace that is not docenhance::{name}",
                (
                    f'namespaceDecl(isExpansionInFileMatching("{location}"), '
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
                        f'isExpansionInFileMatching("{location}"))'
                    ),
                )
            )
        if not manifest.layers[name].get("may_catch"):
            rules.append(
                (
                    f"{name} catches an exception outside an authorized containment boundary",
                    f'cxxCatchStmt(isExpansionInFileMatching("{location}"))',
                )
            )
    return rules


def api_violations(
    clang_query: str, build: Path, entry: dict[str, str], rules: list[tuple[str, str]]
) -> list[str]:
    """Call, throw and catch rules for one translation unit, on its abstract syntax tree."""
    commands = [argument for _, matcher in rules for argument in ("-c", f"match {matcher}")]
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
            *commands,
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
    counts = SUMMARY.findall(result.stdout)
    if len(counts) != len(rules):
        msg = f"clang-query answered {len(counts)} of {len(rules)} rules on {entry['file']}"
        raise ArchitectureError(msg)
    reports = [block.strip() for block in SUMMARY.split(result.stdout)[:-1:2]]
    return [
        f"{entry['file']}: {rule}\n{report}"
        for (rule, _), count, report in zip(rules, counts, reports, strict=True)
        if count != "0"
    ]


def direct_use(manifest: Manifest, files: list[str], layer: str) -> tuple[set[str], set[str]]:
    """The layers and packages a target's own files name directly."""
    layers: set[str] = set()
    packages: set[str] = set()
    for file in files:
        for spelled in INCLUDE_DIRECTIVE.findall(Path(file).read_text(encoding="utf-8")):
            named = manifest.layer_of_include(spelled)
            if named is not None and named != layer:
                layers.add(named)
            package = manifest.package_of_header(spelled)
            if package is not None:
                packages.add(package)
    return layers, packages


def link_violations(manifest: Manifest, build: Path) -> list[str]:
    """Every target links what its files use, and uses what it links."""
    path = build / "architecture-targets.json"
    if not path.is_file():
        msg = f"No target registration at {path}; configure a build first (docs/build.md)"
        raise ArchitectureError(msg)
    errors = []
    for target, record in json.loads(path.read_text(encoding="utf-8")).items():
        layer = record["layer"]
        used, packages = direct_use(manifest, record["files"], layer)
        linked = {
            manifest.target_layer[name] for name in record["links"] if name in manifest.target_layer
        }
        external = {
            manifest.package_target[name]
            for name in record["links"]
            if name in manifest.package_target
        }
        errors += [
            f"{target} uses {missing} but does not link it"
            for missing in sorted((used - linked) | (packages - external))
        ]
        errors += [
            f"{target} links {unused} but no file of its own names it"
            for unused in sorted((linked - used - {layer}) | (external - packages))
        ]
    return errors


def self_containment_violations(manifest: Manifest, entries: list[dict[str, str]]) -> list[str]:
    """Every public header compiles on its own, and twice in the same translation unit."""
    errors = []
    with tempfile.TemporaryDirectory() as directory:
        probe = Path(directory) / "header_probe.cpp"
        for header in sorted((ROOT / "include/docenhance").rglob("*.hpp")):
            relative = header.relative_to(ROOT / "include").as_posix()
            layer = layer_of(manifest, header.as_posix())
            entry = next(
                (item for item in entries if layer_of(manifest, item["file"]) == layer), entries[0]
            )
            probe.write_text(f'#include "{relative}"\n#include "{relative}"\n', encoding="utf-8")
            try:
                run_compiler(
                    [*compiler_arguments(entry), "-fsyntax-only", str(probe)],
                    entry["directory"],
                    f"{relative} does not compile on its own",
                )
            except ArchitectureError as exc:
                errors.append(str(exc))
    return errors


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


def build_violations(manifest: Manifest, build: Path) -> list[str]:
    """Every architecture rule that needs a configured build."""
    entries = compilation_database(manifest, build)
    if not entries:
        msg = f"The compilation database at {build} contains no first-party sources"
        raise ArchitectureError(msg)
    clang_query = find_clang_query()
    errors = link_violations(manifest, build)
    for entry in entries:
        reached, present = reach_violations(manifest, entry, build)
        errors += reached
        errors += api_violations(clang_query, build, entry, matchers(manifest, present))
    return errors + self_containment_violations(manifest, entries)
