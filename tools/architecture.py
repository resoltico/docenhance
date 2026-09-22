# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""The layer manifest, and the rules that can be checked without a build.

`spec/architecture.json` is the single statement of the layer graph: which layers exist, which
layers and which third-party packages each may use, and what each is for. CMake enforces link
edges from it at configure time, tools/check_architecture.py enforces the real include graph and
abstract syntax tree against it inside a build, and this module enforces what can be read from the
sources alone: the directory layout, the include directives first-party files write, and the layer
table in the documentation.
"""

from __future__ import annotations

import json
import re
from pathlib import PurePosixPath
from typing import Any

from architecture_manifest import validate_shape
from deps import ROOT

MANIFEST = ROOT / "spec/architecture.json"
DOCUMENT = ROOT / "docs/architecture.md"
SOURCE_ROOTS = ("src", "include/docenhance")
SOURCE_SUFFIXES = (".hpp", ".cpp")
# Written by the build; a header generated from the version is first-party but owned by no layer.
GENERATED_INCLUDES = ("docenhance/version.hpp",)
INCLUDE_DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^">]+)[">]', re.MULTILINE)
TABLE_ROW = re.compile(r"^\| `([a-z_]+)` \| ([^|]*?) \| ([^|]*?) \|$", re.MULTILINE)


class ArchitectureError(RuntimeError):
    """The architecture rules could not be evaluated."""


class Manifest:
    """The reviewed layer graph, and the questions the rules ask of it."""

    def __init__(self, data: dict[str, Any]) -> None:
        """Index the manifest by layer and by the targets that stand for layers and packages."""
        try:
            validate_shape(data)
        except (TypeError, ValueError) as exc:
            raise ArchitectureError(str(exc)) from exc
        self.layers: dict[str, dict[str, Any]] = data["layers"]
        self.packages: dict[str, dict[str, list[str]]] = data["packages"]
        self.target_layer = {layer["target"]: name for name, layer in self.layers.items()}
        self.package_target = {
            target: name for name, package in self.packages.items() for target in package["targets"]
        }

    def uses(self, layer: str) -> set[str]:
        """The layers this one may name directly."""
        return set(self.layers[layer]["uses"])

    def external(self, layer: str) -> set[str]:
        """The packages this layer may include and link directly."""
        return set(self.layers[layer]["external"])

    def reach(self, layer: str) -> set[str]:
        """Every layer this one may reach, directly or through the layers it uses."""
        seen: set[str] = set()
        pending = [layer]
        while pending:
            current = pending.pop()
            for used in self.uses(current) - seen:
                seen.add(used)
                pending.append(used)
        return seen

    def package_reach(self, layer: str) -> set[str]:
        """Every package this layer may reach, including through the layers it uses."""
        return set().union(*(self.external(name) for name in self.reach(layer) | {layer}))

    def layer_of_include(self, spelled: str) -> str | None:
        """The layer a first-party include names, if it names one."""
        prefix, _, tail = spelled.partition("/")
        if prefix != "docenhance" or not tail:
            return None
        candidate = tail.partition("/")[0]
        return candidate if candidate in self.layers else None

    def package_of_header(self, relative: str) -> str | None:
        """The package a third-party header belongs to, by its path below an include root."""
        for name, package in self.packages.items():
            for pattern in package["headers"]:
                if pattern.endswith("/"):
                    if relative.startswith(pattern) or f"/{pattern}" in relative:
                        return name
                elif PurePosixPath(relative).name == pattern:
                    return name
        return None


def load_manifest() -> Manifest:
    """Read and validate the manifest itself."""
    try:
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        msg = f"Cannot read {MANIFEST.name}: {exc}"
        raise ArchitectureError(msg) from exc
    manifest = Manifest(data)
    errors = manifest_errors(manifest)
    if errors:
        raise ArchitectureError("; ".join(errors))
    return manifest


def manifest_errors(manifest: Manifest) -> list[str]:
    """The manifest names only layers and packages it declares, and its graph is acyclic."""
    errors = []
    for name, layer in manifest.layers.items():
        errors += [
            f"layer {name} uses undeclared layer {used}"
            for used in layer["uses"]
            if used not in manifest.layers
        ]
        errors += [
            f"layer {name} declares undeclared package {package}"
            for package in layer["external"]
            if package not in manifest.packages
        ]
    if errors:
        return errors
    errors += [
        f"layer {name} depends on itself through {sorted(manifest.reach(name) & {name})}"
        for name in manifest.layers
        if name in manifest.reach(name)
    ]
    return errors


def source_files(manifest: Manifest) -> dict[str, list[PurePosixPath]]:
    """Every first-party source and header, grouped by the layer that owns it."""
    owned: dict[str, list[PurePosixPath]] = {name: [] for name in manifest.layers}
    for root in SOURCE_ROOTS:
        for path in sorted((ROOT / root).rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            relative = PurePosixPath(path.relative_to(ROOT).as_posix())
            parts = relative.parts[len(PurePosixPath(root).parts) :]
            if len(parts) <= 1 or parts[0] not in owned:
                msg = f"{relative} has no declared source-layer owner"
                raise ArchitectureError(msg)
            owned[parts[0]].append(relative)
    return owned


def layout_errors(manifest: Manifest) -> list[str]:
    """Every layer has a directory of its own, and no directory escapes the manifest."""
    errors = []
    for root in SOURCE_ROOTS:
        directories = {path.name for path in (ROOT / root).iterdir() if path.is_dir()}
        errors += [
            f"{root}/{name} is not a layer in {MANIFEST.name}"
            for name in sorted(directories - set(manifest.layers))
        ]
    for name, layer in manifest.layers.items():
        headers = ROOT / "include/docenhance" / name
        if layer.get("public_headers", True) and not headers.is_dir():
            errors.append(f"layer {name} has no include/docenhance/{name} directory")
        elif not layer.get("public_headers", True) and headers.exists():
            errors.append(f"entry-only layer {name} must not export public headers")
    try:
        source_files(manifest)
    except ArchitectureError as exc:
        errors.append(str(exc))
    return errors


def include_errors(manifest: Manifest) -> list[str]:
    """What a first-party file names directly must be allowed for the layer that owns it."""
    errors = []
    for layer, files in source_files(manifest).items():
        for relative in files:
            text = (ROOT / relative).read_text(encoding="utf-8")
            errors += [
                f"{relative}: {reason}"
                for reason in file_errors(
                    manifest, layer, text, public=relative.parts[0] == "include"
                )
            ]
    return errors


def file_errors(manifest: Manifest, layer: str, text: str, *, public: bool) -> list[str]:
    """Every include directive one first-party file writes, judged against its layer."""
    errors = []
    for spelled in INCLUDE_DIRECTIVE.findall(text):
        reasons = directive_errors(manifest, layer, spelled)
        if not reasons and public and manifest.package_of_header(spelled) is not None:
            # A package may be used, but never in the types a layer hands to its callers.
            reasons = [f"a public header may not name the third-party header {spelled}"]
        errors += reasons
    return errors


def directive_errors(manifest: Manifest, layer: str, spelled: str) -> list[str]:
    """Whether one include directive is allowed for the layer that writes it."""
    if spelled in GENERATED_INCLUDES:
        return []
    if spelled.startswith("docenhance/"):
        named = manifest.layer_of_include(spelled)
        if named is None:
            return [f"includes {spelled}, which belongs to no layer"]
        if named != layer and named not in manifest.uses(layer):
            return [f"{layer} may not use {named} ({spelled})"]
        return []
    package = manifest.package_of_header(spelled)
    if package is not None:
        allowed = package in manifest.external(layer)
        return [] if allowed else [f"{layer} may not use the {package} package ({spelled})"]
    forbidden = spelled in manifest.layers[layer]["forbidden_headers"]
    return [f"{layer} may not include <{spelled}>"] if forbidden else []


def document_errors(manifest: Manifest) -> list[str]:
    """The layer table in the documentation says exactly what the manifest says."""
    try:
        text = DOCUMENT.read_text(encoding="utf-8")
    except OSError as exc:
        return [f"Cannot read {DOCUMENT.name}: {exc}"]
    documented = {
        target: (uses.strip(), summary.strip()) for target, uses, summary in TABLE_ROW.findall(text)
    }
    errors = []
    for layer in manifest.layers.values():
        expected = (
            ", ".join(f"`{manifest.layers[used]['target']}`" for used in layer["uses"]) or "—",
            layer["summary"],
        )
        actual = documented.pop(layer["target"], None)
        if actual is None:
            errors.append(f"{DOCUMENT.name}: the layer table has no row for {layer['target']}")
        elif actual != expected:
            errors.append(
                f"{DOCUMENT.name}: the row for {layer['target']} says {actual},"
                f" the manifest says {expected}"
            )
    return errors + [
        f"{DOCUMENT.name}: the layer table has a row for {target}, which is not a layer"
        for target in documented
        if target != "docenhance"
    ]


def source_violations() -> list[str]:
    """Every architecture rule that needs no build."""
    manifest = load_manifest()
    return layout_errors(manifest) + include_errors(manifest) + document_errors(manifest)
