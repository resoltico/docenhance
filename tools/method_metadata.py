# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Derive closed response discovery and persistent method identities from implemented families."""

from __future__ import annotations

import copy
import json
import re
from typing import TYPE_CHECKING, Any

from method_catalog import catalog_entries, catalog_projection, metadata_headers
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
    """Discovery is rich metadata; existing run-record method identities remain id/version."""
    active = catalog_entries(families, options)
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
    for family in ("denoising", "restoration", "contrast", "sharpening"):
        admitted = {entry["id"] for entry in active if entry["family"] == family}
        schema["$defs"][family + "_method"] = {
            **schema["$defs"]["method"],
            "oneOf": [
                alternative
                for alternative in schema["$defs"]["method"]["oneOf"]
                if alternative["properties"]["id"]["const"] in admitted
            ],
        }
    matrix = support_matrix(formats)
    for branch in schema["oneOf"]:
        if "methods" in branch.get("properties", {}):
            branch["properties"]["methods"].update(
                items={"$ref": "#/$defs/catalog_entry"}, maxItems=len(active), uniqueItems=True
            )
            branch["properties"]["input_support"] = {"const": matrix}
            branch["properties"]["supported_formats"] = {
                "const": [item["format"] for item in matrix]
            }
    schema["$defs"]["catalog_entry"] = {
        "oneOf": [{"const": catalog_projection(entry)} for entry in active]
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
        otsu_observation=schema["$defs"]["otsu_observation"],
        source_decoding=schema["$defs"]["source_decoding"],
        tiff_resolution=schema["$defs"]["tiff_resolution"],
        restoration=schema["$defs"]["restoration"],
        restoration_request=schema["$defs"]["restoration_request"],
        restoration_method=schema["$defs"]["restoration_method"],
        restoration_psf=schema["$defs"]["restoration_psf"],
        restoration_parameters=schema["$defs"]["restoration_parameters"],
        sharpening=schema["$defs"]["sharpening"],
        sharpening_request=schema["$defs"]["sharpening_request"],
        sharpening_method=schema["$defs"]["sharpening_method"],
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
        **metadata_headers(root, active),
        root / "schemas/command-response.schema.json": json.dumps(schema, indent=2) + "\n",
        root / "schemas/run-record.schema.json": json.dumps(record, indent=2) + "\n",
    }
