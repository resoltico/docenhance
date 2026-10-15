# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Merge the reviewed request and method options into one generated authority."""

from __future__ import annotations

import re
from typing import Any

from parameter_values import (
    CLI_SCHEMA,
    CPP_KEYWORDS,
    IDENTIFIER,
    METHOD_SCHEMA,
    NUMERIC,
    domain_text,
    invalid,
    validate_value,
)

OPERAND_BOUND_COUNT = 2
PARAMETER_FIELDS = frozenset(
    {
        "option",
        "field",
        "kind",
        "metavar",
        "min",
        "max",
        "default",
        "summary",
        "auto",
        "required",
        "when",
        "unit",
        "values",
    }
)
REQUEST_FIELDS = (PARAMETER_FIELDS - {"option", "field"}) | {"name", "scope", "group", "applies_to"}


def validate_authorities(cli: dict[str, Any], methods: dict[str, Any]) -> None:
    """Reject obsolete roots before merging any executable option declaration."""
    if cli.get("schema_version") != CLI_SCHEMA or methods.get("schema_version") != METHOD_SCHEMA:
        invalid("Expected CLI schema13 and method schema3")
    authored_fields(
        cli,
        frozenset({"$comment", "schema_version", "grammar", "commands", "options", "formats"}),
        "CLI contract",
    )
    authored_fields(
        methods, frozenset({"$comment", "schema_version", "families"}), "Method contract"
    )
    if not methods["families"]:
        invalid("At least one implemented family is required")


def merged_options(cli: dict[str, Any], methods: dict[str, Any]) -> list[dict[str, Any]]:
    """Produce one closed option catalog, rejecting collisions and unreachable conditions."""
    validate_authorities(cli, methods)
    validate_commands(cli["commands"])
    commands = cli["commands"]
    options: list[dict[str, Any]] = []
    names: set[str] = set()
    ids: set[str] = set()
    families: set[str] = set()
    orders: set[int] = set()

    def add(raw: dict[str, Any], **derived: object) -> None:
        item = {**raw, **derived}
        item["name"] = item.pop("option", item.get("name"))
        validate_value(item)
        if any(not isinstance(item.get(key), str) or not item[key] for key in ("summary", "group")):
            invalid(f"{item['name']}: missing or invalid presentation text")
        if item["name"] in names:
            invalid(f"Option name collision: {item['name']}")
        names.add(item["name"])
        validate_scope(item, commands)
        item["domain"] = domain_text(item)
        options.append(item)

    for option in cli["options"]:
        authored_fields(option, REQUEST_FIELDS, "Request option")
        add(option, family="", methods=[])
    for family in methods["families"]:
        authored_fields(
            family,
            frozenset({"family", "order", "selector", "group", "applies_to", "shared", "methods"}),
            "Family",
        )
        name, order = family["family"], family["order"]
        if (
            not isinstance(name, str)
            or IDENTIFIER.fullmatch(name) is None
            or name in families
            or type(order) is not int
            or order < 1
            or order in orders
        ):
            invalid("Families need unique names and positive orders")
        families.add(name)
        orders.add(order)
        family_ids, selectors = family_methods(family, ids)
        attributes = {
            "family": name,
            "scope": ["process"],
            "group": family["group"],
            "applies_to": family["applies_to"],
        }
        selector = family["selector"]
        authored_fields(selector, frozenset({"option", "metavar", "default"}), "Family selector")
        values = selectors if name == "binarization" else ["off", *selectors]
        add(
            selector,
            **attributes,
            methods=family_ids,
            kind="choice",
            values=values,
            summary=f"Select {name}",
            selector_methods={
                spelling: method["id"]
                for method in family["methods"]
                for spelling in method["selectors"]
            },
        )
        for shared in family["shared"]:
            add(shared, **attributes, methods=family_ids)
        for method in family["methods"]:
            validate_fields(family, method)
            for parameter in method["parameters"]:
                add(parameter, **attributes, methods=[method["id"]])
    validate_conditions(options)
    return options


def family_methods(family: dict[str, Any], ids: set[str]) -> tuple[list[str], list[str]]:
    """Validate method identities and selectors within their owning family."""
    family_ids, selectors = [], []
    if not family["methods"]:
        invalid("An implemented family cannot be empty")
    for method in family["methods"]:
        authored_fields(
            method,
            frozenset({"id", "name", "version", "selectors", "parameters", "constraints"}),
            "Method",
        )
        identity = method["id"]
        if (
            not isinstance(identity, str)
            or re.fullmatch(r"[A-Z][0-9]{2}", identity) is None
            or identity in ids
            or type(method["version"]) is not int
            or method["version"] < 1
        ):
            invalid("Methods need unique IDs and positive versions")
        ids.add(identity)
        family_ids.append(identity)
        if not isinstance(method["selectors"], dict) or not method["selectors"]:
            invalid("An implemented method requires a selector")
        for selector, behavior in method["selectors"].items():
            if IDENTIFIER.fullmatch(selector) is None or selector in selectors or selector == "off":
                invalid("Selectors must be unique within their family")
            if (
                not isinstance(behavior, dict)
                or set(behavior) - {"mode"}
                or behavior.get("mode", "explicit") not in {"explicit", "automatic"}
            ):
                invalid("Selector behavior must be explicit or automatic")
            selectors.append(selector)
    return family_ids, selectors


def validate_conditions(options: list[dict[str, Any]]) -> None:
    """Refuse unknown, cross-method or unreachable conditional options."""
    by_name = {option["name"]: option for option in options}
    for option in options:
        when = option.get("when", {})
        if not isinstance(when, dict):
            invalid(f"{option['name']}: conditions must be an object")
        for reference, value in when.items():
            other = by_name.get(reference)
            if (
                other is None
                or other["kind"] != "choice"
                or value not in other["values"]
                or other["family"] != option["family"]
                or reference == option["name"]
                or not set(option["methods"]) <= set(other["methods"])
            ):
                invalid(f"{option['name']}: unknown or unreachable when condition")
            selected = other.get("selector_methods")
            if selected is not None and selected.get(value) not in option["methods"]:
                invalid(f"{option['name']}: unreachable method selector condition")


def validate_fields(family: dict[str, Any], method: dict[str, Any]) -> None:
    """Each typed parameter member has exactly one declaration."""
    fields: set[str] = set()
    for parameter in [*family["shared"], *method["parameters"]]:
        authored_fields(parameter, PARAMETER_FIELDS, "Method parameter")
        field = parameter.get("field")
        if (
            not isinstance(field, str)
            or IDENTIFIER.fullmatch(field) is None
            or field in fields
            or field in CPP_KEYWORDS
            or "__" in field
        ):
            invalid(f"{method['id']}: duplicate or invalid parameter field")
        fields.add(field)
        if parameter["kind"] in NUMERIC and "default" not in parameter:
            invalid(f"{method['id']}: numeric parameter needs a supported typed default")


def validate_scope(option: dict[str, Any], commands: dict[str, Any]) -> None:
    """Reject unknown command scopes before creating descriptors."""
    scopes = option["scope"]
    if (
        not isinstance(scopes, list)
        or not scopes
        or any(not isinstance(scope, str) or scope not in commands for scope in scopes)
        or len(set(scopes)) != len(scopes)
    ):
        invalid(f"{option['name']}: unknown command scope")


def validate_commands(commands: dict[str, Any]) -> None:
    """Command cardinality must have a safe, explicit representation in the generated vocabulary."""
    for name, command in commands.items():
        if IDENTIFIER.fullmatch(name) is None or name in CPP_KEYWORDS:
            invalid(f"{name}: unsupported command identifier")
        bounds = command.get("operands")
        pair = [bounds, bounds] if type(bounds) is int else bounds
        if (
            not isinstance(pair, list)
            or len(pair) != OPERAND_BOUND_COUNT
            or any(type(value) is not int or value < 0 for value in pair)
            or pair[0] > pair[1]
        ):
            invalid(f"{name}: expected nonnegative ordered operand bounds")
        if not isinstance(command.get("usage"), str) or not command["usage"]:
            invalid(f"{name}: missing command usage")


def authored_fields(raw: dict[str, Any], allowed: frozenset[str], owner: str) -> None:
    """Refuse obsolete bindings, taxonomy and hand-authored derived metadata."""
    unknown = set(raw) - allowed
    if unknown:
        invalid(f"{owner}: unsupported authored fields {sorted(unknown)}")
