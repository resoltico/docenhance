#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Geometry protection frames and post-rotation CLAHE/restoration processing."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from geometry_reference import processing_frame, protection_frame


def main() -> None:
    """Exercise this geometry boundary against the built production executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-geometry-protection-") as temporary:
        root = Path(temporary)
        protection_frame(exe, root)
        processing_frame(exe, root)
    print("PASS: G02 protection frames and C-frame CLAHE/restoration")


if __name__ == "__main__":
    main()
