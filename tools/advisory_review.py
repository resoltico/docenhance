# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Two reviewed source matches excluded by mandatory private dependency recipes."""

from __future__ import annotations

import hashlib
import json
from typing import Any

FEATURE_DIGEST = "380ec21e0dc52e6332b23eb749cca027affb958676b2b37d3c2dc6f18d69b8b2"


def reviewed(dependency: dict[str, Any], advisory: dict[str, Any], features: bytes) -> bool:
    """Changed source, advisory or policy requires review; hashes establish freshness only."""
    if hashlib.sha256(features).hexdigest() != FEATURE_DIGEST:
        return False
    key = dependency["name"], advisory.get("id")
    digest = hashlib.sha256(
        json.dumps(advisory, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    if key == ("zlib", "CVE-2026-76844"):  # advisory-review: CVE-2026-76844
        return dependency.get(
            "object"
        ) == "216c70c020aa53f0c40920d155f808b6b59c9acb" and digest == (
            "1960dafdbff5cf44e39bf48ea9d987e1a80ccbbfd40de67dcd3d7dba97bf9e69"
        )
    if key == ("opencv", "OSV-2023-444"):  # advisory-review: OSV-2023-444
        return dependency.get(
            "object"
        ) == "9e2ede9628a55ec2742a1b180d3e69b0322281b9" and digest == (
            "b4f59c319e4af590d44f501f7e86037bc2d59ee9200ce925e13bdd5657bbedda"
        )
    return False
