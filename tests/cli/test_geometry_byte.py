#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Byte-precision orientation composition, singleton axes and geometry record refusal."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from geometry_reference import malformed_records, orientation_composition


def main() -> None:
    """Exercise this geometry boundary against the built production executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-geometry-byte-") as temporary:
        root = Path(temporary)
        retained = orientation_composition(exe, root, 8)
        malformed_records(exe, retained)
    print("PASS: G02 byte samples, G01 composition, singleton axes and records")


if __name__ == "__main__":
    main()
