# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Strict finite quality JSON bytes and original evidence depth ownership."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from types import FunctionType
from typing import Any, TypedDict

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / "spec/document-quality.schema.json"
MAX_JSON_DEPTH = 64
COMPLETED_ENCLOSURE_DEPTH = 4


class CanonicalOptions(TypedDict):
    """The four actual keyword domains supplied to the standard JSON encoder."""

    ensure_ascii: bool
    allow_nan: bool
    sort_keys: bool
    separators: tuple[str, str]


CANONICAL_OPTIONS: CanonicalOptions = {
    "ensure_ascii": False,
    "allow_nan": False,
    "sort_keys": True,
    "separators": (",", ":"),
}


def _parser_defaults() -> None:
    """Refuse callbacks or relaxed decoding before any evidence JSON is interpreted."""
    hooks = ("object_hook", "parse_float", "parse_int", "parse_constant", "object_pairs_hook")
    for function, names in ((json.loads, ("cls", *hooks)), (json.JSONDecoder.__init__, hooks)):
        if not isinstance(function, FunctionType):
            message = "Selected JSON parser owner is not an ordinary Python function"
            raise TypeError(message)
        defaults = function.__kwdefaults__
        if type(defaults) is not dict or any(
            name not in defaults or defaults[name] is not None for name in names
        ):
            message = "Selected JSON parser defaults differ from the ordinary CPython route"
            raise ValueError(message)
    decoder = json.JSONDecoder.__init__
    if not isinstance(decoder, FunctionType):
        message = "Selected JSON decoder owner is not an ordinary Python function"
        raise TypeError(message)
    decoder_defaults = decoder.__kwdefaults__
    if type(decoder_defaults) is not dict or decoder_defaults.get("strict") is not True:
        message = "Selected JSON decoder strict default differs"
        raise ValueError(message)


def _encoder_defaults() -> None:
    """Refuse an alternate encoder dispatch before any actual canonical serialization."""
    encoder = json.dumps
    if not isinstance(encoder, FunctionType):
        message = "Selected JSON encoder owner is not an ordinary Python function"
        raise TypeError(message)
    defaults = encoder.__kwdefaults__
    options = CANONICAL_OPTIONS
    separators = (",", ":")
    if (
        type(defaults) is not dict
        or any(name not in defaults or defaults[name] is not None for name in ("cls", "indent"))
        or type(options) is not dict
        or set(options) != {"ensure_ascii", "allow_nan", "sort_keys", "separators"}
        or options["ensure_ascii"] is not False
        or options["allow_nan"] is not False
        or options["sort_keys"] is not True
        or type(options["separators"]) is not tuple
        or len(options["separators"]) != len(separators)
        or any(type(item) is not str for item in options["separators"])
        or options["separators"] != separators
    ):
        message = "Selected canonical serializer aliases, options or encoder default differ"
        raise ValueError(message)


def canonical(value: object) -> bytes:
    """Strict UTF-8 compact sorted-key JSON; preserve arrays and reject nonfinite values."""
    _encoder_defaults()
    return json.dumps(value, **CANONICAL_OPTIONS).encode("utf-8")


def frozen_digest(value: object) -> str:
    """Corpus, protocol and report identities hash their separate closed objects."""
    return hashlib.sha256(canonical(value)).hexdigest()


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    """Refuse duplicate keys instead of discarding evidence while decoding."""
    result: dict[str, Any] = {}
    for name, value in pairs:
        if name in result:
            message = "Duplicate evidence JSON key"
            raise ValueError(message)
        result[name] = value
    return result


def validate_depth(value: object, maximum: int = MAX_JSON_DEPTH) -> None:
    """Bound malformed evidence nesting before schema diagnostics or canonical serialization."""
    pending: list[tuple[object, int]] = [(value, 0)]
    while pending:
        node, depth = pending.pop()
        if depth > maximum:
            message = "Evidence JSON exceeds its nesting depth"
            raise ValueError(message)
        if isinstance(node, dict):
            pending.extend((child, depth + 1) for child in node.values())
        elif isinstance(node, list):
            pending.extend((child, depth + 1) for child in node)


def _read_json(data: bytes, maximum: int) -> dict[str, Any]:
    """Decode one strict object under its owning original or completed-container extent."""
    _parser_defaults()
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=unique_object)
        validate_depth(value, maximum)
        canonical(value)
    except RecursionError as error:
        message = "Evidence JSON exceeds parser or serialization depth"
        raise ValueError(message) from error
    if not isinstance(value, dict):
        message = "Evidence JSON must be an object"
        raise TypeError(message)
    return value


def read_json(data: bytes) -> dict[str, Any]:
    """Original evidence retains its existing strict64-level admission domain."""
    return _read_json(data, MAX_JSON_DEPTH)
