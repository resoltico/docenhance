# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Expose the existing independent CLI fixture modules to the retained product audit."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI_FIXTURES = ROOT / "tests/cli"
if str(CLI_FIXTURES) not in sys.path:
    sys.path.insert(0, str(CLI_FIXTURES))
