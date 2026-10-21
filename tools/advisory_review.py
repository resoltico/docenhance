# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Source- and evidence-bound reviews of unreachable native dependency paths."""

from __future__ import annotations

import hashlib
import json
import re
from datetime import datetime
from typing import Any

# The exact feature policy binds the retained native source pins and selected call paths.
# Stream-only zlib excludes gzip-file APIs; classic JPEG decoding excludes TurboJPEG.
# Actual native audits independently verify those exclusions. Changed feature bytes invalidate
# these reviews; this source binding is not a declaration of vulnerability-free dependencies.
FEATURE_DIGEST = "36a33fd8b2b0e73b9eaf4a8450a35b43dc1d26cf2d4285c69ebbbc4c82cdc0e3"


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
    # OSV-2026-1068's reported call path uses tj3Compress8 (TurboJPEG API),
    # not the classic JPEG decompression adapter. The private feature policy
    # disables that API, and audit_build.jpeg_turbo_failures independently
    # rejects its compiled translation units and installed archive.
    # jpeg_abort itself is shared code and may remain linked. The current evidence
    # differs only in OSV schema_version 1.9.0 -> 1.9.1; all other bound fields
    # are identical. Keep that exact new fingerprint, not a schema-version exemption.
    if key == ("jpeg", "OSV-2026-1068"):  # advisory-review: OSV-2026-1068
        return dependency.get(
            "object"
        ) == "c85e6b905bf237038faa936dab160ebfc5da0344" and digest == (
            "87afd0b015021306ab5148093a72e28490ab31569b6934d342b6402ba5c334cb"
        )

    return False
