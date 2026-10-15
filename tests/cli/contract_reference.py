# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent projections of reviewed authoring facts for real executable contract tests."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]


def reviewed_options() -> list[dict[str, Any]]:
    """Read current authoring shapes directly, without importing the production compiler."""
    cli = json.loads((ROOT / "spec/cli-contract.json").read_text())
    methods = json.loads((ROOT / "spec/method-contract.json").read_text())
    result = [{**option, "methods": []} for option in cli["options"]]
    for family in sorted(methods["families"], key=lambda item: item["order"]):
        identities = [method["id"] for method in family["methods"]]
        selector = family["selector"]
        values = [name for method in family["methods"] for name in method["selectors"]]
        result.append(
            {
                **selector,
                "name": selector["option"],
                "scope": ["process"],
                "kind": "choice",
                "values": values if family["family"] == "binarization" else ["off", *values],
                "methods": identities,
                "summary": "Select " + family["family"],
            }
        )
        result.extend(
            {**parameter, "name": parameter["option"], "scope": ["process"], "methods": identities}
            for parameter in family["shared"]
        )
        for method in family["methods"]:
            result.extend(
                {
                    **parameter,
                    "name": parameter["option"],
                    "scope": ["process"],
                    "methods": [method["id"]],
                }
                for parameter in method["parameters"]
            )
    return result


def domain(option: dict[str, Any]) -> str:
    """Compare rendered domains independently of generated C++ metadata and Python emitters."""
    kind = option["kind"]
    if kind == "choice":
        text = " | ".join(option["values"])
    elif "min" in option:
        prefix = {
            "decimal": "finite ",
            "integer": "integer ",
            "odd_integer": "odd integer ",
            "auto_or_integer": "auto | integer ",
            "source_or_decimal": "source | finite ",
            "grid": "CxR, each integer ",
        }[kind]
        text = f"{prefix}[{option['min']},{option['max']}]"
    else:
        text = "flag" if kind == "flag" else "well-formed UTF-8 path"
    initial = option.get("default")
    return (
        (initial if initial is not None else "required" if option.get("required") else "optional")
        + "; "
        + text
    )


def option_projection(option: dict[str, Any]) -> dict[str, Any]:
    """The public help projection deliberately excludes private field/slot implementation facts."""
    return {
        "name": option["name"],
        "metavar": option["metavar"],
        "description": option["summary"],
        "domain": domain(option),
        "methods": ",".join(option["methods"]),
    }


def reviewed_methods() -> list[dict[str, Any]]:
    """Families determine ordering; one ID owns every explicit/automatic selector of its type."""
    methods = json.loads((ROOT / "spec/method-contract.json").read_text())
    options = {option["name"]: option for option in reviewed_options()}
    result = []
    for family in sorted(methods["families"], key=lambda item: item["order"]):
        for method in sorted(family["methods"], key=lambda item: item["id"]):
            parameters = []
            for parameter in [*family["shared"], *method["parameters"]]:
                option = options[parameter["option"]]
                parameters.append(
                    {
                        "option": option["name"],
                        "kind": option["kind"],
                        "domain": domain(option),
                        "description": option["summary"],
                    }
                )
            result.append(
                {
                    "id": method["id"],
                    "method_version": method["version"],
                    "name": method["name"],
                    "family": family["family"],
                    "selectors": list(method["selectors"]),
                    "parameters": parameters,
                }
            )
    return result
