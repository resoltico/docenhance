# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Read the application version from its single source, the top-level project() command."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
PROJECT_VERSION = re.compile(r"^project\([^)]*?\bVERSION\s+([^\s)]+)", re.MULTILINE | re.DOTALL)


class VersionError(ValueError):
    """The top-level CMakeLists.txt does not declare one numeric semantic version."""


def project_version(root: Path = ROOT) -> str:
    """Return the version declared by project(... VERSION x.y.z ...)."""
    return version_from_text((root / "CMakeLists.txt").read_text(encoding="utf-8"))


def version_from_text(text: str) -> str:
    """Read the same strict project version from an immutable committed source snapshot."""
    match = PROJECT_VERSION.search(text)
    if match is None or SEMVER.fullmatch(match.group(1)) is None:
        msg = "CMakeLists.txt must declare project(... VERSION <major>.<minor>.<patch> ...)"
        raise VersionError(msg)
    return match.group(1)
