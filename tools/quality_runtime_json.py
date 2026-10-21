# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual ordinary CPython JSON aliases, serializer options and parser defaults."""

from __future__ import annotations

import _json
import json
import json.decoder
import json.encoder
import json.scanner
from types import FunctionType

import quality_json as evidence_json
from quality_json import CANONICAL_OPTIONS, _encoder_defaults, _parser_defaults, canonical


def _serialization_options() -> None:
    """Bind the actual function aliases and primitive options before canonical serialization."""
    if not isinstance(canonical, FunctionType):
        message = "Selected canonical owner is not an ordinary Python function"
        raise TypeError(message)
    globals_ = canonical.__globals__
    if (
        evidence_json.canonical is not canonical
        or globals_.get("json") is not json
        or globals_.get("CANONICAL_OPTIONS") is not CANONICAL_OPTIONS
    ):
        message = "Selected canonical serializer aliases, options or encoder default differ"
        raise ValueError(message)
    _encoder_defaults()


def _native_route() -> None:
    """Require the actual native JSON aliases and ordinary primitive parser/encoder defaults."""
    _serialization_options()
    for module, name, native in (
        (json.encoder, "c_make_encoder", "make_encoder"),
        (json.encoder, "encode_basestring", "encode_basestring"),
        (json.encoder, "encode_basestring_ascii", "encode_basestring_ascii"),
        (json.scanner, "make_scanner", "make_scanner"),
        (json.decoder, "scanstring", "scanstring"),
    ):
        selected = getattr(_json, native, None)
        if selected is None or getattr(module, name, None) is not selected:
            message = "Selected JSON encoder/decoder is not the reviewed native route"
            raise ValueError(message)
    encoder, decoder = vars(json).get("JSONEncoder"), vars(json).get("JSONDecoder")
    if (
        encoder is None
        or decoder is None
        or encoder is not json.encoder.JSONEncoder
        or decoder is not json.decoder.JSONDecoder
    ):
        message = "Selected JSON classes differ from the reviewed module owners"
        raise ValueError(message)
    _parser_defaults()
