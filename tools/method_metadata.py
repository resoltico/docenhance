# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Generate reviewed identities, checked against executable variant registration at compile time."""

from __future__ import annotations

import copy
import json
import re
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from pathlib import Path


def validate_arguments(entry: dict[str, Any], known: set[str]) -> None:
    """Validate the implemented method's declared CLI dependency set."""
    identity = entry["id"]
    arguments = entry.get("arguments")
    if not isinstance(arguments, list) or not all(
        isinstance(arg, str) and arg in known for arg in arguments
    ):
        msg = f"{identity}: implemented arguments must exist in the command contract"
        raise ValueError(msg)
    if len(set(arguments)) != len(arguments):
        msg = f"{identity}: method arguments must be unique"
        raise ValueError(msg)


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
        if entry.get("family") not in {"binarization", "illumination", "denoising", "contrast"}:
            msg = f"{identity}: implemented methods require an executable family"
            raise ValueError(msg)
        selector = entry.get("selector", "")
        if (
            not isinstance(selector, str)
            or not re.fullmatch(r"[a-z][a-z0-9_]*", selector)
            or selector in selectors
        ):
            msg = f"{identity}: implemented selectors must be unique C++ identifiers"
            raise ValueError(msg)
        selectors.add(selector)
        validate_arguments(entry, known)
        active.append(entry)
    if not active:
        msg = "The executable must declare at least one implemented method"
        raise ValueError(msg)
    validate_attribution(options, active, ids)
    return active


def validate_attribution(
    options: list[dict[str, Any]], active: list[dict[str, Any]], ids: set[str]
) -> None:
    """Require both reviewed views of method-option applicability to agree."""
    for option in options:
        declared = option.get("methods", "").split(",") if option.get("methods") else []
        if len(set(declared)) != len(declared) or any(identity not in ids for identity in declared):
            msg = f"{option['name']}: invalid method attribution"
            raise ValueError(msg)
        for entry in active:
            if (entry["id"] in declared) != (option["name"] in entry["arguments"]):
                msg = f"{option['name']}: attribution disagrees with {entry['id']} arguments"
                raise ValueError(msg)


def support_matrix(support: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Validate reviewed format admission and derive its wire representation."""
    names: set[str] = set()
    matrix = []
    for item in support:
        name = item.get("format")
        if (
            set(item) != {"format", "binary"}
            or not isinstance(name, str)
            or re.fullmatch(r"[a-z][a-z0-9]*", name) is None
            or name in names
            or type(item["binary"]) is not bool
        ):
            msg = "Input support requires unique formats and explicit binary admission"
            raise ValueError(msg)
        names.add(name)
        matrix.append(
            {
                "format": name,
                "output_modes": ["preserve", "gray", *(["bw"] if item["binary"] else [])],
            }
        )
    if not matrix:
        msg = "Input support must not be empty"
        raise ValueError(msg)
    return matrix


def render_catalog(active: list[dict[str, Any]]) -> str:
    """Emit descriptors only, not an algorithm implementation or a runtime registration bypass."""
    text = (
        "// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis\n"
        "// SPDX-License-Identifier: MPL-2.0\n"
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
    call = f"std::to_array<ImplementedMethod>({{{descriptors}}});"
    line_width = 100
    if len(call) + 4 > line_width:
        text += "inline constexpr auto reviewed_methods = std::to_array<ImplementedMethod>({\n"
        text += "".join(f"    {entry['selector']}_descriptor,\n" for entry in active)
        text += "});\n"
    else:
        text += "inline constexpr auto reviewed_methods =\n    " + call + "\n"
    return text + "} // namespace docenhance::methods\n"


def metadata_outputs(
    root: Path,
    entries: list[dict[str, Any]],
    options: list[dict[str, Any]],
    support: list[dict[str, Any]],
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
            if entry["family"] == "binarization"
        ],
    }
    schema["$defs"]["illumination_method"] = {
        "oneOf": [
            {
                "properties": {
                    "id": {"const": entry["id"]},
                    "method_version": {"const": entry["method_version"]},
                },
                "required": ["id", "method_version"],
                "additionalProperties": False,
            }
            for entry in active
            if entry["family"] == "illumination"
        ]
    }
    schema["$defs"]["denoising_method"] = {
        **schema["$defs"]["method"],
        "oneOf": [
            alternative
            for alternative in schema["$defs"]["method"]["oneOf"]
            if alternative["properties"]["id"]["const"]
            in {entry["id"] for entry in active if entry["family"] == "denoising"}
        ],
    }
    schema["$defs"]["contrast_method"] = {
        **schema["$defs"]["method"],
        "oneOf": [
            alternative
            for alternative in schema["$defs"]["method"]["oneOf"]
            if alternative["properties"]["id"]["const"]
            in {entry["id"] for entry in active if entry["family"] == "contrast"}
        ],
    }
    matrix = support_matrix(support)
    for branch in schema["oneOf"]:
        if "methods" in branch.get("properties", {}):
            branch["properties"]["methods"].update(maxItems=len(active), uniqueItems=True)
            branch["properties"]["input_support"] = {"const": matrix}
            branch["properties"]["supported_formats"] = {
                "const": [item["format"] for item in matrix]
            }
    record = json.loads((root / "spec/run-record.schema.json").read_text(encoding="utf-8"))
    record["description"] = (
        "Generated from spec/run-record.schema.json, spec/command-response.schema.json "
        "and spec/method-contract.json. Runtime typed validation also checks relationships."
    )
    conversion = next(
        branch["properties"]["conversion"]
        for branch in schema["oneOf"]
        if "conversion" in branch.get("properties", {})
    )
    record_conversion = copy.deepcopy(conversion)
    record_conversion["properties"]["verified"] = {"const": True}
    record["$defs"].update(
        source_decoding=schema["$defs"]["source_decoding"],
        tiff_resolution=schema["$defs"]["tiff_resolution"],
        contrast=schema["$defs"]["contrast"],
        contrast_request=schema["$defs"]["contrast_request"],
        contrast_method=schema["$defs"]["contrast_method"],
        denoising=schema["$defs"]["denoising"],
        denoising_request=schema["$defs"]["denoising_request"],
        denoising_method=schema["$defs"]["denoising_method"],
        conversion=record_conversion,
        resolution=conversion["properties"]["resolution"],
        illumination=schema["$defs"]["illumination"],
        illumination_request=schema["$defs"]["illumination_request"],
        illumination_method=schema["$defs"]["illumination_method"],
        binary_method={
            **schema["$defs"]["method"],
            "oneOf": [
                alternative
                for alternative in schema["$defs"]["method"]["oneOf"]
                if alternative["properties"]["id"]["const"]
                in {entry["id"] for entry in active if entry["family"] == "binarization"}
            ],
        },
    )
    return {
        root / "include/docenhance/methods/reviewed_methods.hpp": render_catalog(active),
        root / "schemas/command-response.schema.json": json.dumps(schema, indent=2) + "\n",
        root / "schemas/run-record.schema.json": json.dumps(record, indent=2) + "\n",
    }
