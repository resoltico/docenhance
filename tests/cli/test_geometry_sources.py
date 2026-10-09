#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Stored binary and JPEG/TIFF rotation with strict geometry option admission."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

from geometry_reference import binary_samples, source_codecs, strict_options


def main() -> None:
    """Exercise this geometry boundary against the built production executable."""
    exe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="docenhance-geometry-sources-") as temporary:
        root = Path(temporary)
        binary_samples(exe, root)
        source_codecs(exe, root)
        strict_options(exe, root)
    print("PASS: G02 binary/JPEG/TIFF samples and strict admission")


if __name__ == "__main__":
    main()
