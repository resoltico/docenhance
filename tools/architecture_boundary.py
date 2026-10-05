# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Production-header privacy and explicitly registered client include boundaries."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING

from deps import ROOT

if TYPE_CHECKING:
    from pathlib import Path

    from architecture import Manifest

INCLUDE_DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^">]+)[">]', re.MULTILINE)


def private_header_error(owner: str | None, header: Path, *, public: bool = False) -> str | None:
    """Private production headers belong to their layer, never to clients or public interfaces."""
    source_root = ROOT.resolve() / "src"
    header = header.resolve()
    if header.is_relative_to(source_root):
        parts = header.relative_to(source_root).parts
        if parts and (public or parts[0] != owner):
            return f"private production header {header} escapes its owning layer"
    return None


def private_directive_error(
    owner: str | None, source: Path, spelled: str, *, public: bool = False
) -> str | None:
    """Resolve actual local/absolute spellings, including parent traversal and src-root includes."""
    for candidate in (source.parent / spelled, ROOT / spelled):
        if candidate.is_file():
            return private_header_error(owner, candidate, public=public)
    return None


def resolved_layer(manifest: Manifest, source: Path, spelled: str) -> str | None:
    """Resolve relative public-header spellings before applying layer/client permissions."""
    for candidate in (source.parent / spelled, ROOT / "include" / spelled, ROOT / spelled):
        if not candidate.is_file():
            continue
        resolved = candidate.resolve()
        base = ROOT.resolve() / "include"
        if resolved.is_relative_to(base):
            return manifest.layer_of_include(resolved.relative_to(base).as_posix())
    return manifest.layer_of_include(spelled)


def client_source_errors(manifest: Manifest) -> list[str]:
    """A named client may reach its root's public closure, not arbitrary production effects."""
    errors = []
    for source, root in manifest.clients.items():
        path = ROOT / source
        if not path.is_file() or path.is_symlink():
            errors.append(f"Missing regular architecture client {source}")
            continue
        allowed = manifest.reach(root) | {root}
        packages = manifest.package_reach(root)
        for spelled in INCLUDE_DIRECTIVE.findall(path.read_text(encoding="utf-8")):
            named = resolved_layer(manifest, path, spelled)
            package = manifest.package_of_header(spelled)
            private = private_directive_error(None, path, spelled)
            if private:
                errors.append(f"{source}: {private}")
            elif (named is not None or spelled.startswith("docenhance/")) and named not in allowed:
                errors.append(f"{source}: client of {root} cannot reach {spelled}")
            elif package is not None and package not in packages:
                errors.append(f"{source}: client of {root} cannot reach package {package}")
    return errors
