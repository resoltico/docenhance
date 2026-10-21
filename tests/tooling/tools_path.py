# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Put tools/ on sys.path so the tests import tool modules by name, exactly as the tools do."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"


def prepare_tool_imports() -> None:
    """Apply this helper's existing idempotent path ownership explicitly for direct tool imports."""
    if str(TOOLS) not in sys.path:
        sys.path.insert(0, str(TOOLS))


prepare_tool_imports()
