# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Validate manifest structure before graph traversal or target indexing can fail open."""

from __future__ import annotations

import re
from pathlib import PurePosixPath
from typing import Any

IDENTIFIER = re.compile(r"[a-z][a-z0-9_]*")


def string_list(value: object, label: str) -> list[str]:
    """Require a duplicate-free list of nonempty strings."""
    if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
        msg = f"{label} must be a list of nonempty strings"
        raise TypeError(msg)
    if len(set(value)) != len(value):
        msg = f"{label} contains duplicates"
        raise ValueError(msg)
    return value


def validate_layer(name: str, layer: dict[str, Any]) -> str:
    """Validate one declared layer and return its target identifier."""
    if not IDENTIFIER.fullmatch(name):
        msg = f"Invalid architecture layer {name!r}"
        raise ValueError(msg)
    for field in ("target", "summary"):
        if not isinstance(layer.get(field), str) or not layer[field]:
            msg = f"layer {name}.{field} must be a nonempty string"
            raise ValueError(msg)
    allowed = {
        "target",
        "summary",
        "uses",
        "external",
        "allow_headers",
        "allow_calls",
        "may_allocate",
        "may_catch",
        "may_thread",
        "public_headers",
        "interface_packages",
    }
    if layer.keys() - allowed:
        msg = f"layer {name} has unknown or obsolete fields"
        raise ValueError(msg)
    for field in ("uses", "external"):
        string_list(layer.get(field), f"layer {name}.{field}")
    for field in ("allow_headers", "allow_calls", "interface_packages"):
        if field in layer:
            string_list(layer[field], f"layer {name}.{field}")
    for field in ("may_allocate", "may_catch", "may_thread", "public_headers"):
        if field in layer and type(layer[field]) is not bool:
            msg = f"layer {name}.{field} must be a boolean"
            raise ValueError(msg)
    return str(layer["target"])


def mapping(value: object, label: str) -> dict[str, Any]:
    """Require a JSON object before indexing any of its entries."""
    if not isinstance(value, dict):
        msg = f"{label} must be an object"
        raise TypeError(msg)
    return value


def validate_policy(data: dict[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    """Validate shared restrictions and explicit client identities before permission checks."""
    restrictions = mapping(data.get("restrictions"), "restrictions")
    if set(restrictions) != {"headers", "calls", "thread_types", "thread_calls"}:
        msg = "Architecture restrictions require the current closed fields"
        raise ValueError(msg)
    for field, value in restrictions.items():
        string_list(value, f"restrictions.{field}")
        if field != "headers" and any(
            not re.fullmatch(r"(?:::)?[A-Za-z_][A-Za-z0-9_:]*", item) for item in value
        ):
            msg = f"Invalid API name in restrictions.{field}"
            raise ValueError(msg)
    clients = mapping(data.get("clients"), "clients")
    for source, owner in clients.items():
        path = PurePosixPath(source)
        if (
            path.is_absolute()
            or path.as_posix() != source
            or ".." in path.parts
            or path.suffix != ".cpp"
            or not isinstance(owner, str)
        ):
            msg = f"Invalid architecture client {source}"
            raise ValueError(msg)
    return restrictions, clients


def validate_permissions(name: str, value: dict[str, Any], restrictions: dict[str, Any]) -> None:
    """Layer exceptions must remove existing bans and interfaces must name usable packages."""
    for field, baseline in (("allow_headers", "headers"), ("allow_calls", "calls")):
        if set(value.get(field, [])) - set(restrictions[baseline]):
            msg = f"layer {name}.{field} names no baseline restriction"
            raise ValueError(msg)
    if set(value.get("interface_packages", [])) - set(value["external"]):
        msg = f"layer {name} exports a package it cannot use"
        raise ValueError(msg)


def validate_shape(data: dict[str, Any]) -> None:
    """Reject malformed layers, packages and target collisions before building dictionaries."""
    data = mapping(data, "manifest")
    if data.keys() - {"$comment", "restrictions", "clients", "layers", "packages"}:
        msg = "Unknown architecture manifest fields"
        raise ValueError(msg)
    restrictions, clients = validate_policy(data)
    layers = mapping(data.get("layers"), "layers")
    packages = mapping(data.get("packages"), "packages")
    if not layers:
        msg = "The architecture manifest must declare at least one layer"
        raise ValueError(msg)
    targets: set[str] = set()
    for name, value in layers.items():
        target = validate_layer(name, mapping(value, f"layer {name}"))
        if target in targets:
            msg = f"Duplicate architecture target {target}"
            raise ValueError(msg)
        targets.add(target)
        validate_permissions(name, value, restrictions)
    if set(clients.values()) - layers.keys():
        msg = "Architecture client names an undeclared root"
        raise ValueError(msg)
    for name, value in packages.items():
        package = mapping(value, f"package {name}")
        declared = string_list(package.get("targets"), f"package {name}.targets")
        string_list(package.get("headers"), f"package {name}.headers")
        if targets.intersection(declared):
            msg = f"Duplicate package target in {name}"
            raise ValueError(msg)
        targets.update(declared)
