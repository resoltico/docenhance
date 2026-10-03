# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MIT
"""Check concrete local Markdown links against the actual source or delivered file tree."""

from __future__ import annotations

import re
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path


def local_link_errors(root: Path) -> list[str]:
    """Inspect reviewed Markdown, excluding upstream license text and heading-only links."""
    paths = [
        *root.glob("*.md"),
        *(root / "docs").rglob("*.md"),
        *(root / ".github").glob("*.md"),
        *(root / "fuzz").glob("*.md"),
    ]
    errors = []
    for path in paths:
        for link in re.findall(r"\]\(([^)\s]+)\)", path.read_text(encoding="utf-8")):
            target = link.split("#", 1)[0]
            external = "://" in link or link.startswith(("#", "mailto:"))
            if not external and target and not (path.parent / target).exists():
                errors.append(f"Broken local link: {path.relative_to(root)} -> {link}")
    return errors
