# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Completed quality_run reader with its exact four-level enclosure allowance."""

from __future__ import annotations

from typing import Any

import quality_json
import quality_run_validation
import quality_schema
from quality_json import _read_json


def read_run_json(data: bytes) -> dict[str, Any]:
    """Read only closed v3 completed runs with four levels for actual evidence enclosure."""
    value = _read_json(data, quality_json.MAX_JSON_DEPTH + quality_json.COMPLETED_ENCLOSURE_DEPTH)
    validate_run(value)
    return value


def validate_run(value: dict[str, Any]) -> None:
    """A complete static shape also needs actual plan-derived event and snapshot relationships."""
    quality_schema.validator("quality_run").validate(value)
    for entry in value["intended"]["referenced_evidence"]:
        quality_json.validate_depth(entry["value"])
    quality_run_validation.validate_run_relationships(value)
