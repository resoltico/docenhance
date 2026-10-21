# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Static quality schema selectors and finite report vocabularies."""

from __future__ import annotations

from jsonschema import Draft202012Validator

from quality_json import SCHEMA, read_json


def validator(kind: str) -> Draft202012Validator:
    """Use the reviewed closed plan or report shape and common definitions."""
    schema = read_json(SCHEMA.read_bytes())
    return Draft202012Validator({"$defs": schema["$defs"], "$ref": f"#/$defs/{kind}"})


def harm_ids() -> tuple[str, ...]:
    """The reviewed schema owns the finite harm vocabulary used by report accounting."""
    schema = read_json(SCHEMA.read_bytes())
    return tuple(schema["$defs"]["review"]["properties"]["harms"]["items"]["enum"])


def outcome_ids() -> tuple[str, ...]:
    """The schema owns the engineering outcome domain; accounting does not invent another list."""
    schema = read_json(SCHEMA.read_bytes())
    return tuple(schema["$defs"]["role"]["properties"]["outcome"]["enum"])
