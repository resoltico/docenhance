# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual pre-effect reference acquisition order."""

from __future__ import annotations

from typing import Any


def referenced_order(plan: dict[str, Any]) -> list[dict[str, Any]]:
    """The intended snapshot order follows actual admission and assessment revision order."""
    protocol = plan["protocol"]
    references = [plan["preregistration"], protocol["build_provenance"], protocol["assignment"]]
    if protocol["tuning"]["receipt"] is not None:
        references.append(protocol["tuning"]["receipt"])
    for assessment in plan["assessments"]:
        references.extend(assessment["review_revisions"])
    return references
