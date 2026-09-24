# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Generate reviewed identities, checked against executable variant registration at compile time."""

from __future__ import annotations

import json
import re
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from pathlib import Path


def implemented(
    entries: list[dict[str, Any]], options: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """Validate identity uniqueness and option references before generating C++ or schemas."""
    ids: set[str] = set()
    selectors: set[str] = set()
    known = {option["name"] for option in options}
    active = []
    for entry in entries:
        identity = entry.get("id", "")
        version = entry.get("method_version")
        if (
            not isinstance(identity, str)
            or not re.fullmatch(r"[A-Z][0-9]{2}", identity)
            or identity in ids
        ):
            msg = "Method IDs must be unique letter/two-digit identifiers"
            raise ValueError(msg)
        ids.add(identity)
        if type(version) is not int or version < 1:
            msg = f"{identity}: method_version must be a positive integer"
            raise ValueError(msg)
        if entry.get("status") not in {"implemented", "not-implemented"}:
            msg = f"{identity}: unknown implementation status"
            raise ValueError(msg)
        if entry["status"] != "implemented":
            continue
        selector = entry.get("selector", "")
        if (
            not isinstance(selector, str)
            or not re.fullmatch(r"[a-z][a-z0-9_]*", selector)
            or selector in selectors
        ):
            msg = f"{identity}: implemented selectors must be unique C++ identifiers"
            raise ValueError(msg)
        selectors.add(selector)
        arguments = entry.get("arguments")
        if not isinstance(arguments, list) or not all(
            isinstance(arg, str) and arg in known for arg in arguments
        ):
            msg = f"{identity}: implemented arguments must exist in the command contract"
            raise ValueError(msg)
        if len(set(arguments)) != len(arguments):
            msg = f"{identity}: method arguments must be unique"
            raise ValueError(msg)
        active.append(entry)
    if not active:
        msg = "The executable must declare at least one implemented method"
        raise ValueError(msg)
    return active


def render_catalog(active: list[dict[str, Any]]) -> str:
    """Emit descriptors only, not an algorithm implementation or a runtime registration bypass."""
    text = (
        "// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis\n"
        "// SPDX-License-Identifier: MIT\n"
        "// Generated from spec/method-contract.json by tools/generate_spec.py.\n"
        "#pragma once\n"
        '#include "docenhance/methods/catalog.hpp"\n\n'
        "#include <array>\n\nnamespace docenhance::methods {\n"
    )
    for entry in active:
        text += (
            f"inline constexpr ImplementedMethod {entry['selector']}_descriptor{{\n"
            f"    .id = {json.dumps(entry['id'])},\n"
            f"    .method_version = {entry['method_version']}U,\n"
            f"    .selector = {json.dumps(entry['selector'])},\n}};\n"
        )
    descriptors = ", ".join(f"{entry['selector']}_descriptor" for entry in active)
    text += (
        "inline constexpr auto reviewed_methods =\n"
        f"    std::to_array<ImplementedMethod>({{{descriptors}}});\n"
    )
    return text + "} // namespace docenhance::methods\n"


def metadata_outputs(
    root: Path, entries: list[dict[str, Any]], options: list[dict[str, Any]]
) -> dict[Path, str]:
    """Produce reviewed descriptors and closed method/version schema alternatives together."""
    active = implemented(entries, options)
    schema = json.loads((root / "spec/command-response.schema.json").read_text(encoding="utf-8"))
    schema["description"] = (
        "Generated from spec/command-response.schema.json and "
        "spec/method-contract.json. Do not edit."
    )
    schema["$defs"]["method"] = {
        "type": "object",
        "required": ["id", "method_version"],
        "additionalProperties": False,
        "properties": {"id": {"type": "string"}, "method_version": {"type": "integer"}},
        "oneOf": [
            {
                "properties": {
                    "id": {"const": entry["id"]},
                    "method_version": {"const": entry["method_version"]},
                }
            }
            for entry in active
        ],
    }
    schema["$defs"]["completed_method"] = {
        "oneOf": [
            {
                "properties": {
                    "method": {"const": entry["id"]},
                    "method_version": {"const": entry["method_version"]},
                },
                "required": ["method", "method_version"],
            }
            for entry in active
        ],
    }
    for branch in schema["oneOf"]:
        if "methods" in branch.get("properties", {}):
            branch["properties"]["methods"].update(maxItems=len(active), uniqueItems=True)
    return {
        root / "include/docenhance/methods/method_catalog.hpp": render_catalog(active),
        root / "schemas/command-response.schema.json": json.dumps(schema, indent=2) + "\n",
    }
