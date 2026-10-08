# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""The architecture rules that are checked against a configured build.

A checker that greps source text is easy to fool: a comment, a string, a macro, a type alias or a
qualified name all read the same to a regular expression. These rules ask the build itself instead,
through the compilation database it writes:

The build is read through tools/compile_db.py.

* **Reach rules** run on the transitive include graph the compiler reports (`-H`), so a layer or a
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
import tempfile
from pathlib import Path

from architecture import ArchitectureError, Manifest
from architecture_api import api_violations, find_clang_query, matchers
from architecture_boundary import INCLUDE_DIRECTIVE, private_header_error
from compile_db import (
    LayerRoots,
    compilation_database,
    compiler_arguments,
    included_headers,
    layer_of,
    package_roots,
    run_compiler,
)
from deps import ROOT
from parallel import ordered_map


def header_violations(
    manifest: Manifest,
    entry: dict[str, str],
    build: Path,
    root: str,
    *,
    owner: str | None,
) -> tuple[list[str], dict[str, set[str]]]:
    """Production and registered clients share the real include and privacy boundary."""
    source, errors = entry["file"], []
    allowed, packages = manifest.reach(root) | {root}, manifest.package_reach(root)
    roots = package_roots(entry, build)
    present: dict[str, set[str]] = {}
    layers = LayerRoots()
    # Production syntax is checked by the mandatory AST pass; clients have no such pass.
    for spelled, header in included_headers(entry, syntax=owner is None):
        reached = layers.layer_of(manifest, header)
        if reached is not None:
            present.setdefault(reached, set()).update((spelled, header))
            private = private_header_error(owner, Path(header))
            if private:
                errors.append(f"{source}: {private}")
            if reached not in allowed:
                errors.append(f"{source}: {owner or 'client'} reaches {reached} through {header}")
            continue
        package_root = next(
            (candidate for candidate in roots if header.startswith(f"{candidate}/")),
            None,
        )
        if package_root is None:
            continue
        relative = header[len(package_root) + 1 :]
        package = manifest.package_of_header(relative)
        if package is None:
            errors.append(f"{source}: {relative} belongs to no declared package")
        elif package not in packages:
            errors.append(
                f"{source}: {owner or 'client'} reaches the {package} package through {relative}"
            )
    return errors, present


def reach_violations(
    manifest: Manifest, entry: dict[str, str], build: Path
) -> tuple[list[str], dict[str, set[str]]]:
    """Layer and package reach for one translation unit, and the layers it contains."""
    layer = layer_of(manifest, entry["file"]) or ""
    errors, present = header_violations(
        manifest,
        entry,
        build,
        layer,
        owner=layer,
    )
    present.setdefault(layer, set()).update((entry["file"], str(Path(entry["file"]).resolve())))
    return errors, present


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


def self_containment_violations(
    manifest: Manifest, entries: list[dict[str, str]], jobs: int = 1
) -> list[str]:
    """Every public header compiles on its own, and twice in the same translation unit."""
    headers = sorted((ROOT / "include/docenhance").rglob("*.hpp"))
    with tempfile.TemporaryDirectory() as directory:

        def check(item: tuple[int, Path]) -> list[str]:
            index, header = item
            relative = header.relative_to(ROOT / "include").as_posix()
            layer = layer_of(manifest, header.as_posix())
            entry = next(
                (item for item in entries if layer_of(manifest, item["file"]) == layer),
                entries[0],
            )
            # One probe per header, so checks running side by side never share a file.
            probe = Path(directory) / f"header_probe_{index}.cpp"
            probe.write_text(f'#include "{relative}"\n#include "{relative}"\n', encoding="utf-8")
            try:
                run_compiler(
                    [*compiler_arguments(entry), "-fsyntax-only", str(probe)],
                    entry["directory"],
                    f"{relative} does not compile on its own",
                )
            except ArchitectureError as exc:
                return [str(exc)]
            return []

        found = ordered_map(check, list(enumerate(headers)), jobs)
    return [error for group in found for error in group]


def entry_violations(
    manifest: Manifest, entry: dict[str, str], build: Path, clang_query: str
) -> list[str]:
    """Reach and API rules for one translation unit."""
    reached, present = reach_violations(manifest, entry, build)
    return reached + api_violations(clang_query, build, entry, matchers(manifest, present))


def client_link_errors(manifest: Manifest, root: str, record: dict[str, str]) -> list[str]:
    """A client links only layer/package members of its reviewed public closure."""
    errors = []
    allowed, packages = manifest.reach(root) | {root}, manifest.package_reach(root)
    for link in filter(None, record["links"].split(";")):
        if link == "DocEnhance::options":
            continue
        layer, package = (
            manifest.target_layer.get(link),
            manifest.package_target.get(link),
        )
        if layer not in allowed and package not in packages:
            errors.append(f"{record['target']}: client of {root} cannot link {link}")
    return errors


def client_violations(manifest: Manifest, build: Path) -> list[str]:
    """Observe registered clients' actual compiler includes and generator-time direct links."""
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    errors, observed, targets = [], set(), set()
    for path in sorted(build.glob("architecture-client-*.json")):
        record = json.loads(path.read_text(encoding="utf-8"))
        if (
            not isinstance(record, dict)
            or set(record) != {"target", "source", "links"}
            or not all(isinstance(v, str) and v for v in record.values())
        ):
            msg = f"Invalid client registration: {path}"
            raise ArchitectureError(msg)
        source = record["source"]
        if source not in manifest.clients or record["target"] in targets:
            msg = f"Unknown or repeated architecture client: {path}"
            raise ArchitectureError(msg)
        targets.add(record["target"])
        root = manifest.clients[source]
        errors += client_link_errors(manifest, root, record)
        if source in observed:
            continue
        observed.add(source)
        matching = [
            item for item in entries if Path(item["file"]).resolve() == (ROOT / source).resolve()
        ]
        if not matching:
            msg = f"Client {source} has {len(matching)} compiler entries"
            raise ArchitectureError(msg)
        for entry in matching:
            found, _ = header_violations(manifest, entry, build, root, owner=None)
            errors += found
    if observed != set(manifest.clients):
        errors.append("Architecture client registrations differ from the reviewed manifest")
    return errors


def build_violations(manifest: Manifest, build: Path, jobs: int = 1) -> list[str]:
    """Every architecture rule that needs a configured build, on at most `jobs` workers."""
    entries = compilation_database(manifest, build)
    if not entries:
        msg = f"The compilation database at {build} contains no first-party sources"
        raise ArchitectureError(msg)
    clang_query = find_clang_query()
    errors = link_violations(manifest, build) + client_violations(manifest, build)
    for found in ordered_map(
        lambda entry: entry_violations(manifest, entry, build, clang_query),
        entries,
        jobs,
    ):
        errors += found
    return errors + self_containment_violations(manifest, entries, jobs)
