# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Source- and evidence-bound reviews of unreachable native dependency paths."""

from __future__ import annotations

import hashlib
import json
import re
from datetime import datetime
from typing import Any

# The reviewed policy removes the external command parser. Retained native objects, features and
# affected call paths are unchanged: stream-only zlib excludes gzip-file APIs, CPU NLM/DFT
# excludes OpenCV imgcodecs/OpenJPEG/Jasper, and classic JPEG decoding excludes TurboJPEG.
# This digest binds the full reviewed policy; native audits still verify its actual build.
FEATURE_DIGEST = "632d35cf5ff116bd7faecf11fd548b33a71447096d3c24a89c8ee7a0f0260afc"


def reviewed(dependency: dict[str, Any], advisory: dict[str, Any], features: bytes) -> bool:
    """Bind evidence except valid modified timestamps; hashes do not prove review quality."""
    # OSV modified is observation metadata, not vulnerability evidence. Never ignore another
    # field (including unknown fields, affected ranges or withdrawal) when binding a review.
    modified = advisory.get("modified")
    if (
        hashlib.sha256(features).hexdigest() != FEATURE_DIGEST
        or not isinstance(modified, str)
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
            "12a786006836a0b529e089dfb4b5991eb5396b42b4024a7ee761441ba2eaacd3"
        )
    # OSV-2026-1068's reported call path uses tj3Compress8 (TurboJPEG API),
    # not the classic JPEG decompression adapter. The private feature policy
    # disables that API, and audit_build.jpeg_turbo_failures independently
    # rejects its compiled translation units and installed archive.
    # jpeg_abort itself is shared code and may remain linked.
    if key == ("jpeg", "OSV-2026-1068"):  # advisory-review: OSV-2026-1068
        return dependency.get(
            "object"
        ) == "c85e6b905bf237038faa936dab160ebfc5da0344" and digest == (
            "bb96bc1003bf11f1d8205488219bceff8417ad58a5265c145fa6c7b16998e814"
        )

    return False
