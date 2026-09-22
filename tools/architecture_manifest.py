# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Validate manifest structure before graph traversal or target indexing can fail open."""

from __future__ import annotations

import re
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
    for field in ("uses", "external", "forbidden_headers", "forbidden_calls"):
        string_list(layer.get(field), f"layer {name}.{field}")
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


def validate_shape(data: dict[str, Any]) -> None:
    """Reject malformed layers, packages and target collisions before building dictionaries."""
    data = mapping(data, "manifest")
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
    for name, value in packages.items():
        package = mapping(value, f"package {name}")
        declared = string_list(package.get("targets"), f"package {name}.targets")
        string_list(package.get("headers"), f"package {name}.headers")
        if targets.intersection(declared):
            msg = f"Duplicate package target in {name}"
            raise ValueError(msg)
        targets.update(declared)
