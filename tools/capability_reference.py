# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Render current CLI and method references from the same validated option declarations."""

from __future__ import annotations

from typing import Any

from method_metadata import support_matrix

PREAMBLE = (
    "> Generated from `spec/cli-contract.json` and `spec/method-contract.json` "
    "by `tools/generate_spec.py`. Edit those authorities instead.\n\n"
)


def parameter_text(option: dict[str, Any]) -> str:
    """Describe one domain and its exact conditional admission, including required presence."""
    text = f"**Domain/default:** {option['domain']}.\n\n{option['summary']}\n\n"
    if option.get("auto"):
        text += f"**Automatic resolution:** {option['auto']}.\n\n"
    if option.get("when"):
        conditions = ", ".join(f"`{name}={value}`" for name, value in option["when"].items())
        text += f"**Requires:** {conditions}.\n\n"
    if option.get("required"):
        text += "Explicit presence is required when this option applies.\n\n"
    return text


def render_reference(cli: dict[str, Any], options: list[dict[str, Any]]) -> str:
    """Current syntax and applicability; the manually maintained roadmap is not admission."""
    text = "# Argument contract\n\n" + PREAMBLE + "## Commands\n\n"
    for name, command in cli["commands"].items():
        text += f"- `{name}` — `{command['usage']}`\n"
        if command.get("summary"):
            text += f"  {command['summary']}\n"
    text += "\n## Input support\n\n"
    for item in support_matrix(cli["formats"]):
        modes = ", ".join(f"`{mode}`" for mode in item["output_modes"])
        text += f"- `{item['format']}`: {modes}.\n"
    text += "\n## Grammar\n\n"
    for name, rule in cli["grammar"].items():
        text += f"**{name.capitalize()}.** {rule}\n\n"
    for option in options:
        metavar = f" {option['metavar']}" if option["metavar"] else ""
        text += f"## `{option['name']}{metavar}`\n\n"
        text += "**Scope:** " + ", ".join(f"`{name}`" for name in option["scope"]) + ".\n\n"
        text += parameter_text(option)
        if option["methods"]:
            text += "**Applicable methods:** " + ", ".join(option["methods"]) + ".\n\n"
        if option.get("applies_to"):
            text += "**Output modes:** " + ", ".join(option["applies_to"]) + ".\n\n"
    return text.rstrip() + "\n"


def render_methods(families: list[dict[str, Any]], options: list[dict[str, Any]]) -> str:
    """List actual selectable methods, with shared/private domains and selector semantics."""
    by_name = {option["name"]: option for option in options}
    text = "# Methods\n\n" + PREAMBLE
    text += (
        "Discovery lists implemented processing methods. Metadata orientation and exact "
        "quarter-turn geometry are common operations described in [geometry](pipeline.md). "
        "Future work is tracked in the [roadmap](roadmap.md).\n\n"
    )
    for family in sorted(families, key=lambda item: item["order"]):
        for method in sorted(family["methods"], key=lambda item: item["id"]):
            text += f"## {method['id']} — {method['name']}\n\n"
            text += f"Family: **{family['family']}**. Method version: **{method['version']}**.\n\n"
            selector = family["selector"]["option"]
            for spelling, behavior in method["selectors"].items():
                mode = behavior.get("mode", "explicit")
                text += f"- `{selector} {spelling}`: {mode}.\n"
            text += "\n"
            for parameter in [*family["shared"], *method["parameters"]]:
                option = by_name[parameter["option"]]
                text += f"### `{option['name']} {option['metavar']}`\n\n"
                text += parameter_text(option)
            if method.get("constraints"):
                text += "Cross-field constraints: " + "; ".join(method["constraints"]) + ".\n\n"
    return text.rstrip() + "\n"
