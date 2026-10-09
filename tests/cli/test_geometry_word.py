#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Word-precision exact geometry composition and physical resolution axes."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from geometry_reference import orientation_composition


def main() -> None:
    """Exercise this geometry boundary against the built production executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-geometry-word-") as temporary:
        root = Path(temporary)
        orientation_composition(exe, root, 16)
    print("PASS: G02 word samples, G01 composition and resolution")


if __name__ == "__main__":
    main()
