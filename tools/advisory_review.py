# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Two reviewed source matches excluded by mandatory private dependency recipes."""

from __future__ import annotations

import hashlib
import json
import re
from datetime import datetime
from typing import Any

FEATURE_DIGEST = "4bad5ddc536b41d080fbf87e535d2bc695ab5f5521580703a6f11f2ddd1e7eb9"


def reviewed(dependency: dict[str, Any], advisory: dict[str, Any], features: bytes) -> bool:
    """Bind evidence except valid modified timestamps; hashes do not prove review quality."""
    if hashlib.sha256(features).hexdigest() != FEATURE_DIGEST:
        return False
    # OSV modified is observation metadata, not vulnerability evidence. Never ignore another
    # field (including unknown fields, affected ranges or withdrawal) when binding a review.
    modified = advisory.get("modified")
    if (
        not isinstance(modified, str)
        or re.fullmatch(
            r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]+)?Z", modified
        )
        is None
    ):
        return False
    try:
        datetime.fromisoformat(modified)
    except ValueError:
        return False
    evidence = {key: value for key, value in advisory.items() if key != "modified"}
    key = dependency["name"], advisory.get("id")
    digest = hashlib.sha256(
        json.dumps(evidence, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    if key == ("zlib", "CVE-2026-76844"):  # advisory-review: CVE-2026-76844
        return dependency.get(
            "object"
        ) == "216c70c020aa53f0c40920d155f808b6b59c9acb" and digest == (
            "9157e2317af5754627743a6505e394b87b7170c04490e9924a997980fe35d792"
        )
    if key == ("opencv", "OSV-2023-444"):  # advisory-review: OSV-2023-444
        return dependency.get(
            "object"
        ) == "9e2ede9628a55ec2742a1b180d3e69b0322281b9" and digest == (
            "dc33bbb2b4f178061f6d7e5f4eaf95bbf34ab3ee2e5f190a02e8fb4ac4e3b9a6"
        )
    return False
