# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Frozen plan admission before any quality evidence effects."""

from __future__ import annotations

from typing import Any

from quality_json import canonical, frozen_digest, validate_depth
from quality_protocol import validate_relationships
from quality_schema import validator


def validate_plan(plan: dict[str, Any]) -> None:
    """Admit the schema and immutable protocol identity before evidence collection."""
    validator("plan").validate(plan)
    validate_depth(plan)
    canonical(plan)
    protocol = plan["protocol"]
    if (
        frozen_digest(protocol) != plan["protocol_sha256"]
        or frozen_digest(protocol["corpus"]) != protocol["frozen_corpus_sha256"]
    ):
        message = "Frozen protocol or corpus identity differs"
        raise ValueError(message)
    validate_relationships(plan)
