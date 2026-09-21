# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Read the application version from its single source, the top-level project() command."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT_VERSION = re.compile(
    r"^project\([^)]*?\bVERSION\s+(\d+\.\d+\.\d+)", re.MULTILINE | re.DOTALL
)


class VersionError(ValueError):
    """The top-level CMakeLists.txt does not declare one numeric semantic version."""


def project_version(root: Path = ROOT) -> str:
    """Return the version declared by project(... VERSION x.y.z ...)."""
    match = PROJECT_VERSION.search((root / "CMakeLists.txt").read_text(encoding="utf-8"))
    if match is None:
        msg = "CMakeLists.txt must declare project(... VERSION <major>.<minor>.<patch> ...)"
        raise VersionError(msg)
    return match.group(1)
