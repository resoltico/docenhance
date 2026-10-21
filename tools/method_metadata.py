# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Derive closed response discovery and persistent method identities from implemented families."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING, Any

from method_catalog import catalog_entries, metadata_headers
from parameter_values import invalid

if TYPE_CHECKING:
    from pathlib import Path


def support_matrix(formats: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Keep reviewed format/mode admission exact; do not manufacture additional modes."""
    names: set[str] = set()
    matrix = []
    for item in formats:
        name, modes = item.get("format"), item.get("modes")
        if (
            set(item) != {"format", "modes"}
            or not isinstance(name, str)
            or re.fullmatch(r"[a-z][a-z0-9]*", name) is None
            or name in names
            or modes not in (["preserve", "gray"], ["preserve", "gray", "bw"])
        ):
            invalid("Formats require unique names and complete ordered mode admission")
        names.add(name)
        matrix.append({"format": name, "output_modes": modes})
    if not matrix:
        invalid("Format admission must not be empty")
    return matrix


def metadata_outputs(
    root: Path,
    families: list[dict[str, Any]],
    options: list[dict[str, Any]],
    formats: list[dict[str, Any]],
) -> dict[Path, str]:
    """Generate typed discovery only; actual wire schemas come from native Fields emission."""
    support_matrix(formats)
    return metadata_headers(root, catalog_entries(families, options))
